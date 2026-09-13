// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "idtech/game_singletons.h"

#include <windows.h>

#include <mutex>
#include <vector>

#include "cameraunlock/memory/safe_memory.h"

namespace wolf_ht::idtech {

namespace {

// The largest region the sweep reads in one go. The game has allocations of
// several hundred megabytes and reading one whole is a large transient commit,
// so regions are walked in slices.
constexpr SIZE_T kSliceBytes = 4u << 20;

// idGameLocal::playerEntities holds one player in this game. A count outside
// this range is a freed object whose memory has been reused.
constexpr std::int32_t kMaxPlayers = 8;

// Where WolfNewOrder_x64.exe is loaded, which every RVA below is measured from.
// Resolved once: it is fixed for the life of the process, and both live-object
// checks run on the render thread on every rendered frame.
std::uintptr_t ExeBase() {
    static const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return base;
}

bool ReadState(std::uintptr_t object, const GameLocalShape& shape, std::int32_t& gamestate,
               std::int32_t& players) {
    if (!cameraunlock::memory::SafeRead(object + shape.gamestate_offset, gamestate)) return false;
    if (gamestate < kGameStateUninitialized || gamestate > kGameStatePlayVideo) return false;

    std::uintptr_t list = 0;
    if (!cameraunlock::memory::SafeRead(object + shape.player_list_offset, list)) return false;
    if (!cameraunlock::memory::SafeRead(object + shape.player_list_offset + sizeof(void*),
                                       players)) {
        return false;
    }
    if (players < 0 || players > kMaxPlayers) return false;
    // A list with entries must have storage, and one with none must not claim any.
    return (players == 0) == (list == 0);
}

// The live object is the one in a loaded level with a player in it. Nothing
// else qualifies: the stale copies read GAMESTATE_UNINITIALIZED with an empty
// list, and the shell's own copy reports no players.
bool IsLiveCandidate(std::uintptr_t object, const GameLocalShape& shape) {
    std::int32_t gamestate = 0;
    std::int32_t players = 0;
    if (!ReadState(object, shape, gamestate, players)) return false;
    return gamestate == kGameStateActive && players > 0;
}

// A sweep reads into a heap buffer that is itself committed, readable memory,
// so the other resolver's concurrent sweep walks it and finds a verbatim copy
// of whatever it holds, vtable pointer included. An object address computed
// inside that copy is a phantom: it passes the acceptance test, gets published,
// and evaporates the moment the buffer is refilled. Both resolvers sweep at
// once for as long as neither has published, which at the shell is the whole
// session, so this is the ordinary case rather than a rare one. Registering
// each buffer for the length of its sweep is what keeps one sweep out of the
// other's scratch space.
class ScanBuffers {
public:
    static ScanBuffers& Instance() {
        // Leaked, like the mod object itself. A resolver thread is detached and
        // never joined, so one can still be sweeping - and so calling Overlaps -
        // while the module's static destructors run. A destroyed mutex and
        // vector under a live sweeper is a crash on the way out of the game.
        static ScanBuffers* instance = new ScanBuffers();
        return *instance;
    }

    void Add(std::uintptr_t base, SIZE_T size) {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_ranges.push_back(Range{base, size});
    }

    void Remove(std::uintptr_t base) {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (auto it = m_ranges.begin(); it != m_ranges.end(); ++it) {
            if (it->base == base) {
                m_ranges.erase(it);
                return;
            }
        }
    }

    bool Overlaps(std::uintptr_t base, SIZE_T size) const {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (const Range& range : m_ranges) {
            if (base < range.base + range.size && range.base < base + size) return true;
        }
        return false;
    }

private:
    struct Range {
        std::uintptr_t base;
        SIZE_T size;
    };

    mutable std::mutex m_mutex;
    std::vector<Range> m_ranges;
};

class RegisteredScanBuffer {
public:
    RegisteredScanBuffer(std::uintptr_t base, SIZE_T size) : m_base(base) {
        ScanBuffers::Instance().Add(base, size);
    }
    ~RegisteredScanBuffer() { ScanBuffers::Instance().Remove(m_base); }

    RegisteredScanBuffer(const RegisteredScanBuffer&) = delete;
    RegisteredScanBuffer& operator=(const RegisteredScanBuffer&) = delete;

private:
    std::uintptr_t m_base;
};

// One pass over committed memory for objects carrying `vtable_rva`, accepting
// the first that `accept` recognises. Shared by both resolvers below: they
// differ only in the vtable they look for and what makes a candidate live, and
// a second copy of the region walk is a second place for the slicing to be got
// wrong.
template <typename Accept>
void* SweepForVtable(std::uint32_t vtable_rva, Accept accept) {
    const std::uintptr_t vtable = ExeBase() + vtable_rva;

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto address = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    const auto limit = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);

    // Typed as uintptr_t rather than bytes so the word-at-a-time scan below
    // reads the storage as the type it was written as, and lands aligned by
    // construction rather than by luck.
    std::vector<std::uintptr_t> slice(kSliceBytes / sizeof(std::uintptr_t));
    const RegisteredScanBuffer registration(reinterpret_cast<std::uintptr_t>(slice.data()),
                                            kSliceBytes);

    while (address < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &region, sizeof(region)) == 0) break;
        const auto regionBase = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const std::uintptr_t next = regionBase + region.RegionSize;

        // Masked rather than compared whole, so a readable page is still
        // recognised when the allocator has combined the access with
        // PAGE_NOCACHE or PAGE_WRITECOMBINE. PAGE_GUARD survives the mask on
        // purpose: touching a guard page raises.
        //
        // The writable set plus PAGE_READONLY, and no wider. Both targets are
        // heap objects in PAGE_READWRITE, so admitting the executable and
        // copy-on-write protections would only pull every module's .text through
        // ReadProcessMemory twice every three seconds for as long as the player
        // sits at the shell, and find nothing.
        const DWORD access = region.Protect & ~static_cast<DWORD>(PAGE_NOCACHE | PAGE_WRITECOMBINE);
        const bool readable =
            region.State == MEM_COMMIT &&
            (access == PAGE_READWRITE || access == PAGE_EXECUTE_READWRITE ||
             access == PAGE_READONLY);
        if (readable) {
            for (SIZE_T done = 0; done < region.RegionSize; done += kSliceBytes) {
                const SIZE_T remaining = region.RegionSize - done;
                const SIZE_T want = remaining < kSliceBytes ? remaining : kSliceBytes;
                const std::uintptr_t sliceBase = regionBase + done;
                if (ScanBuffers::Instance().Overlaps(sliceBase, want)) continue;
                // Zeroed here, and that initialiser is what makes the scan
                // below safe: the kernel writes the copied count on success and
                // on a partial copy, and does not touch it if it never got that
                // far, so an unwritten `got` has to already read zero.
                SIZE_T got = 0;
                // The return value is deliberately not tested. A region can
                // shrink or be reprotected between the VirtualQuery above and
                // this read, which fails the call with ERROR_PARTIAL_COPY after
                // it has already filled `got` bytes. Those bytes are valid and
                // the live object may be among them, so the prefix is scanned
                // either way.
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(sliceBase),
                                  slice.data(), want, &got);
                const SIZE_T count = got / sizeof(std::uintptr_t);
                for (SIZE_T i = 0; i < count; ++i) {
                    if (slice[i] != vtable) continue;
                    const std::uintptr_t object = sliceBase + i * sizeof(std::uintptr_t);
                    if (accept(object)) return reinterpret_cast<void*>(object);
                }
            }
        }
        if (next <= address) break;
        address = next;
    }
    return nullptr;
}

// The player's own vptr, the embedded idHands' vptr, and the hands'
// back-pointer resolving to the player again. A freed block that still holds
// the first of those does not hold the other two.
bool HasPresentablePlayerShape(std::uintptr_t object, const PresentablePlayerShape& shape) {
    const std::uintptr_t base = ExeBase();

    std::uintptr_t vptr = 0;
    if (!cameraunlock::memory::SafeRead(object, vptr)) return false;
    if (vptr != base + shape.vtable_rva) return false;

    const std::uintptr_t hands = object + shape.hands_offset;
    std::uintptr_t handsVptr = 0;
    if (!cameraunlock::memory::SafeRead(hands, handsVptr)) return false;
    if (handsVptr != base + shape.hands_vtable_rva) return false;

    std::uintptr_t owner = 0;
    if (!cameraunlock::memory::SafeRead(hands + shape.hands_owner_offset, owner)) return false;
    return owner == object;
}

}  // namespace

bool IsLiveGameLocal(void* object, const GameLocalShape& shape) {
    if (object == nullptr) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(object);

    std::uintptr_t vptr = 0;
    if (!cameraunlock::memory::SafeRead(address, vptr)) return false;
    if (vptr != ExeBase() + shape.vtable_rva) return false;

    std::int32_t gamestate = 0;
    std::int32_t players = 0;
    // Deliberately NOT the stricter live-candidate test: once the object is
    // known, leaving a level takes gamestate away from ACTIVE and empties the
    // player list without freeing it, and re-sweeping on every menu screen
    // would cost a gigabyte of reads for a pointer that has not moved.
    return ReadState(address, shape, gamestate, players);
}

void GameLocalResolver::Start(const GameLocalShape& shape) {
    m_shape = shape;
    m_worker.Start([this] {
        return SweepForVtable(m_shape.vtable_rva,
                              [this](std::uintptr_t object) {
                                  return IsLiveCandidate(object, m_shape);
                              });
    });
}

void* GameLocalResolver::Get() {
    void* object = m_worker.Published();
    if (object == nullptr) return nullptr;
    if (!IsLiveGameLocal(object, m_shape)) {
        // Freed under us by a map change. Clearing it is what asks the worker
        // for another sweep.
        m_worker.Clear();
        return nullptr;
    }
    return object;
}

bool IsLivePresentablePlayer(void* object, const PresentablePlayerShape& shape) {
    if (object == nullptr) return false;
    return HasPresentablePlayerShape(reinterpret_cast<std::uintptr_t>(object), shape);
}

void PresentablePlayerResolver::Start(const PresentablePlayerShape& shape) {
    m_shape = shape;
    m_worker.Start([this] {
        return SweepForVtable(m_shape.vtable_rva,
                              [this](std::uintptr_t object) {
                                  return HasPresentablePlayerShape(object, m_shape);
                              });
    });
}

void* PresentablePlayerResolver::Get() {
    void* object = m_worker.Published();
    if (object == nullptr) return nullptr;
    if (!IsLivePresentablePlayer(object, m_shape)) {
        // Freed by a map change. Clearing it is what asks the worker to sweep
        // again; unlike idGameLocal there is no weaker "still allocated" test to
        // fall back on, because the whole shape is what identifies the object.
        m_worker.Clear();
        return nullptr;
    }
    return object;
}

}  // namespace wolf_ht::idtech
