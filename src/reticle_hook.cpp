// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "reticle_hook.h"

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>

#include "aim_projection.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "headtracking_mod.h"
#include "logging.h"

namespace wolf_ht {
namespace {
using HudFn = void(__fastcall*)(void*, void*, void*, int);
using SpriteFn = void(__fastcall*)(void*, void*, void*, float*, int, unsigned char);
using DimensionFn = float(__fastcall*)(void*);
using MarkerFn = void(__fastcall*)(void*, int);
using UpdateMatrixFn = void(__fastcall*)(void*);
using TraceFn = std::uint64_t*(__fastcall*)(void*, std::uint64_t*, void*,
    const idtech::Vec3*, const idtech::Vec3*, void*, const idtech::Mat3*,
    unsigned, int, unsigned char, const char*);

std::atomic<HudFn> g_hudOriginal{nullptr};
std::atomic<SpriteFn> g_spriteOriginal{nullptr};
std::atomic<MarkerFn> g_markerOriginal{nullptr};
std::atomic<HeadTrackingMod*> g_mod{nullptr};
const builds::ReticleOffsets* g_offsets = nullptr;
std::uintptr_t g_base = 0;

struct Context {
    void* sprite = nullptr;
    void* interaction = nullptr;
    void* description = nullptr;
    bool apply = false;
    bool visible = false;
    float x = 0, y = 0;
    float offsetX = 0, offsetY = 0;
};
thread_local Context g_context;

struct Trace {
    float fraction;
    idtech::Vec3 end;
    unsigned char remaining[112];
};
static_assert(sizeof(Trace) == 128);

void Prepare(void* hud, void* view, int renderTime, HeadTrackingMod& mod) {
    g_context = {};
    const auto player = reinterpret_cast<std::uintptr_t>(hud) - 0x7CF0;
    const auto cleanView = player + 0x4780;
    if (!mod.UpdateForFrame()) return;
    g_context.sprite = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(hud) + 0x538);
    g_context.interaction = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(hud) + 0x6C0);
    g_context.description = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(hud) + 0x6C8);
    const auto cleanOrigin = *reinterpret_cast<const idtech::Vec3*>(cleanView + 0x60);
    const auto cleanAxis = *reinterpret_cast<const idtech::Mat3*>(cleanView + 0x6C);
    idtech::Vec3 origin = cleanOrigin;
    idtech::Mat3 axis = cleanAxis;
    if (!mod.BuildTrackedView(origin, axis)) return;
    g_context.apply = true;

    const auto clientGame = *reinterpret_cast<const std::uintptr_t*>(
        g_base + g_offsets->client_game_pointer_rva);
    const auto passEntity = *reinterpret_cast<const std::uintptr_t*>(player + 0x10AB8);
    const float fovX = *reinterpret_cast<const float*>(cleanView + 0x10);
    const float fovY = *reinterpret_cast<const float*>(cleanView + 0x14);
    const bool explicitProjection = *reinterpret_cast<const unsigned char*>(cleanView + 0x5C) != 0;
    const float width = reinterpret_cast<DimensionFn>(g_base + g_offsets->view_width_rva)(view);
    const float height = reinterpret_cast<DimensionFn>(g_base + g_offsets->view_height_rva)(view);
    static ULONGLONG lastLog = 0;
    const ULONGLONG now = GetTickCount64();
    const bool log = now - lastLog >= 1000;
    if (log) lastLog = now;
    if (!clientGame || !passEntity || explicitProjection ||
        !idtech::IsOrthonormal(cleanAxis) || !idtech::IsOrthonormal(axis) ||
        !idtech::IsFinite3(&origin.x) || !idtech::IsFinite3(&cleanOrigin.x) ||
        !(width > 0 && height > 0)) {
        if (log) Log::Line("[reticle] unavailable: client=%p pass=%p explicit=%d size=%gx%g",
            reinterpret_cast<void*>(clientGame), reinterpret_cast<void*>(passEntity),
            explicitProjection, width, height);
        return;
    }
    constexpr float range = 131072.0f;
    const idtech::Vec3 end = {
        cleanOrigin.x + cleanAxis.m[0] * range,
        cleanOrigin.y + cleanAxis.m[1] * range,
        cleanOrigin.z + cleanAxis.m[2] * range,
    };
    const idtech::Mat3 identity;
    Trace trace{};
    std::uint64_t query = 0;
    // A non-null result requests the synchronous path. 0x802085 is the weapon
    // translation mask; the focus tracker uses 0x21B8C1 and an earlier query.
    reinterpret_cast<TraceFn>(g_base + g_offsets->translation_rva)(
        reinterpret_cast<void*>(clientGame + 0x4248), &query, &trace, &cleanOrigin,
        &end, nullptr, &identity, 0x802085u,
        *reinterpret_cast<const int*>(passEntity + 0x218), 0, "HeadTracking aim");
    if (!(trace.fraction >= 0.0f && trace.fraction <= 1.0f) ||
        !idtech::IsFinite3(&trace.end.x)) {
        if (log) Log::Line("[reticle] invalid trace: fraction=%g end=(%g %g %g)",
            trace.fraction, trace.end.x, trace.end.y, trace.end.z);
        return;
    }
    const bool hit = trace.fraction < 1.0f;
    const idtech::Vec3 aim = hit
        ? idtech::Vec3{trace.end.x - origin.x, trace.end.y - origin.y, trace.end.z - origin.z}
        : idtech::Vec3{cleanAxis.m[0], cleanAxis.m[1], cleanAxis.m[2]};
    float ndcX = 0, ndcY = 0;
    g_context.visible = ProjectAim(aim, axis, fovX, fovY, ndcX, ndcY) &&
                        std::fabs(ndcX) <= 1.0f && std::fabs(ndcY) <= 1.0f;
    g_context.x = (ndcX + 1.0f) * width * 0.5f;
    g_context.y = (1.0f - ndcY) * height * 0.5f;
    g_context.offsetX = ndcX * width * 0.5f;
    g_context.offsetY = -ndcY * height * 0.5f;
    if (log) Log::Line("[reticle] time=%d hit=%d point=(%.2f %.2f %.2f) clean=(%.2f %.2f %.2f) "
        "eye=(%.2f %.2f %.2f) fwd=(%.4f %.4f %.4f) fov=%.2fx%.2f size=%.0fx%.0f "
        "ndc=(%.4f %.4f) pixel=(%.1f %.1f) visible=%d",
        renderTime, hit, trace.end.x, trace.end.y, trace.end.z,
        cleanOrigin.x, cleanOrigin.y, cleanOrigin.z, origin.x, origin.y, origin.z,
        axis.m[0], axis.m[1], axis.m[2], fovX, fovY, width, height,
        ndcX, ndcY, g_context.x, g_context.y, g_context.visible);
}

void __fastcall Hud(void* hud, void* view, void* info, int time) {
    const Context previous = g_context;
    HeadTrackingMod* mod = g_mod.load(std::memory_order_acquire);
    if (mod) Prepare(hud, view, time, *mod);
    __try {
        g_hudOriginal.load(std::memory_order_acquire)(hud, view, info, time);
    } __finally {
        g_context = previous;
    }
}

void __fastcall Sprite(void* swf, void* view, void* sprite, float* state,
                       int material, unsigned char flags) {
    const SpriteFn original = g_spriteOriginal.load(std::memory_order_acquire);
    const bool interaction = sprite == g_context.interaction || sprite == g_context.description;
    if (!g_context.apply || (sprite != g_context.sprite && !interaction)) {
        original(swf, view, sprite, state, material, flags);
        return;
    }
    if (!g_context.visible) return;
    const float x = state[4], y = state[5];
    state[4] = interaction ? x + g_context.offsetX : g_context.x;
    state[5] = interaction ? y + g_context.offsetY : g_context.y;
    if (interaction) {
        static ULONGLONG lastLog = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - lastLog >= 1000) {
            lastLog = now;
            Log::Line("[interaction] pixel=(%.1f %.1f) -> (%.1f %.1f)", x, y, state[4], state[5]);
        }
    }
    __try {
        original(swf, view, sprite, state, material, flags);
    } __finally {
        state[4] = x;
        state[5] = y;
    }
}

void __fastcall Markers(void* player, int time) {
    const MarkerFn original = g_markerOriginal.load(std::memory_order_acquire);
    HeadTrackingMod* mod = g_mod.load(std::memory_order_acquire);
    if (!mod || !mod->UpdateForFrame()) {
        original(player, time);
        return;
    }
    const auto view = reinterpret_cast<std::uintptr_t>(player) + 0x4760;
    auto* origin = reinterpret_cast<idtech::Vec3*>(view + 0x80);
    auto* axis = reinterpret_cast<idtech::Mat3*>(view + 0x8C);
    const idtech::Vec3 cleanOrigin = *origin;
    const idtech::Mat3 cleanAxis = *axis;
    idtech::Vec3 trackedOrigin = cleanOrigin;
    idtech::Mat3 trackedAxis = cleanAxis;
    if (!idtech::IsFinite3(&cleanOrigin.x) || !idtech::IsOrthonormal(cleanAxis) ||
        !mod->BuildTrackedView(trackedOrigin, trackedAxis) ||
        !idtech::IsFinite3(&trackedOrigin.x) || !idtech::IsOrthonormal(trackedAxis)) {
        original(player, time);
        return;
    }
    auto* matrix = reinterpret_cast<float*>(view + 0x317C);
    auto* stamp = reinterpret_cast<int*>(view + 0x31BC);
    float cleanMatrix[16];
    std::memcpy(cleanMatrix, matrix, sizeof(cleanMatrix));
    const int cleanStamp = *stamp;
    // Marker clipping and placement consume this cache. Build it with the
    // tracked eye, then restore the camera before any other game code runs.
    __try {
        *origin = trackedOrigin;
        *axis = trackedAxis;
        *stamp = -1;
        __try {
            reinterpret_cast<UpdateMatrixFn>(g_base + g_offsets->view_update_matrix_rva)(
                reinterpret_cast<void*>(view));
        } __finally {
            *origin = cleanOrigin;
            *axis = cleanAxis;
        }
        static ULONGLONG lastLog = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - lastLog >= 1000) {
            lastLog = now;
            Log::Line("[markers] frame=%d clean=(%.2f %.2f %.2f) eye=(%.2f %.2f %.2f) "
                "fwd=(%.4f %.4f %.4f)", *stamp, cleanOrigin.x, cleanOrigin.y, cleanOrigin.z,
                trackedOrigin.x, trackedOrigin.y, trackedOrigin.z,
                trackedAxis.m[0], trackedAxis.m[1], trackedAxis.m[2]);
        }
        original(player, time);
    } __finally {
        std::memcpy(matrix, cleanMatrix, sizeof(cleanMatrix));
        *stamp = cleanStamp;
    }
}
}

bool InstallReticleHook(const builds::BuildProfile& profile, HeadTrackingMod& mod) {
    if (!profile.reticle) {
        Log::Line("[reticle] correction is not available for %s", profile.name);
        return false;
    }
    g_offsets = profile.reticle;
    g_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    void* hud = reinterpret_cast<void*>(g_base + g_offsets->hud_render_rva);
    void* sprite = reinterpret_cast<void*>(g_base + g_offsets->sprite_render_rva);
    void* marker = reinterpret_cast<void*>(g_base + g_offsets->marker_render_rva);
    HudFn hudOriginal = nullptr;
    SpriteFn spriteOriginal = nullptr;
    MarkerFn markerOriginal = nullptr;
    auto status = hooks.CreateHook(hud, reinterpret_cast<void*>(&Hud), reinterpret_cast<void**>(&hudOriginal));
    g_hudOriginal.store(hudOriginal, std::memory_order_release);
    if (status == cameraunlock::hooks::HookStatus::Ok) {
        status = hooks.CreateHook(sprite, reinterpret_cast<void*>(&Sprite), reinterpret_cast<void**>(&spriteOriginal));
        g_spriteOriginal.store(spriteOriginal, std::memory_order_release);
    }
    if (status == cameraunlock::hooks::HookStatus::Ok) {
        status = hooks.CreateHook(marker, reinterpret_cast<void*>(&Markers), reinterpret_cast<void**>(&markerOriginal));
        g_markerOriginal.store(markerOriginal, std::memory_order_release);
    }
    if (status == cameraunlock::hooks::HookStatus::Ok) status = hooks.EnableHook(marker);
    if (status == cameraunlock::hooks::HookStatus::Ok) status = hooks.EnableHook(sprite);
    if (status == cameraunlock::hooks::HookStatus::Ok) status = hooks.EnableHook(hud);
    if (status != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("[reticle] hook installation failed: %s", cameraunlock::hooks::HookStatusToString(status));
        return false;
    }
    g_mod.store(&mod, std::memory_order_release);
    Log::Line("[reticle] hooked HUD +0x%X and sprite render +0x%X", g_offsets->hud_render_rva, g_offsets->sprite_render_rva);
    Log::Line("[markers] hooked world marker render +0x%X", g_offsets->marker_render_rva);
    return true;
}
}
