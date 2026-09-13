// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstdint>

#include "idtech/resolver_thread.h"

namespace wolf_ht::idtech {

// gameState_t, in declaration order. Only ACTIVE is gameplay: STARTUP covers
// the load, NOMAP the shell with no level, and PLAYVIDEO the bink cutscenes.
// PLAYVIDEO being last is also the upper bound a candidate idGameLocal's
// gamestate has to sit inside to be believed.
enum GameStateValue {
    kGameStateUninitialized = 0,
    kGameStateNoMap = 1,
    kGameStateStartup = 2,
    kGameStateActive = 3,
    kGameStateShutdown = 4,
    kGameStatePlayVideo = 5,
};

// What a candidate idGameLocal has to look like to be accepted.
struct GameLocalShape {
    std::uint32_t vtable_rva;
    std::uint32_t gamestate_offset;
    std::uint32_t player_list_offset;  // idList<idPlayer*>: T* list, then int num
};

// Resolves the one live idGameLocal and keeps it resolved across map changes.
//
// The object is heap allocated and no global in the EXE points at it, so the
// only handle on it from inside the process is its vtable: a sweep of committed
// memory for that pointer finds every object of the class. Several are found -
// the engine keeps stale copies whose memory still carries the vtable - and
// they are told apart by state, not by address: the live one is the only one
// reporting GAMESTATE_ACTIVE with a player in its entity list. Accepting the
// first candidate instead picks a zeroed copy that reports "no level loaded"
// for the whole session.
//
// Nothing is written and nothing is called; the object is only read.
class GameLocalResolver {
public:
    void Start(const GameLocalShape& shape);

    // The live object, or null while no level is loaded. Cheap: one atomic read
    // plus the revalidation, and it asks the worker to sweep again when the
    // object it had has gone.
    void* Get();

private:
    GameLocalShape m_shape{};
    ResolverThread m_worker;
};

// Whether @p object still looks like the live idGameLocal: right vtable, a
// gameState_t in range, and a player list whose count and storage agree.
bool IsLiveGameLocal(void* object, const GameLocalShape& shape);

// What a candidate idPresentablePlayer has to look like to be accepted.
struct PresentablePlayerShape {
    std::uint32_t vtable_rva;
    std::uint32_t hands_offset;       // embedded idHands
    std::uint32_t hands_vtable_rva;
    std::uint32_t hands_owner_offset;  // idPresentablePlayer* back-pointer
};

// Resolves the live idPresentablePlayer the same way and for the same reason as
// GameLocalResolver above: heap allocated, nothing in the EXE points at it, so
// the vtable is the only handle on it from inside the process.
//
// The acceptance test is stronger here than a vtable match, because the engine
// leaves freed copies behind whose memory still carries one. idHands is an
// embedded member rather than a separate allocation and holds a back-pointer to
// its owner, so a candidate is accepted only when its own vptr, the embedded
// hands' vptr and that back-pointer all agree - a triple no reused block
// satisfies by accident.
class PresentablePlayerResolver {
public:
    void Start(const PresentablePlayerShape& shape);

    // The live object, or null while no level is loaded.
    void* Get();

private:
    PresentablePlayerShape m_shape{};
    ResolverThread m_worker;
};

// Whether @p object still looks like the live idPresentablePlayer.
bool IsLivePresentablePlayer(void* object, const PresentablePlayerShape& shape);

}  // namespace wolf_ht::idtech
