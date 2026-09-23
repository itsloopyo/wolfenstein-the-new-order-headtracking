// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace wolf_ht::builds {

struct ReticleOffsets {
    std::uint32_t hud_render_rva;
    std::uint32_t sprite_render_rva;
    std::uint32_t translation_rva;
    std::uint32_t client_game_pointer_rva;
    std::uint32_t view_width_rva;
    std::uint32_t view_height_rva;
    std::uint32_t game_frame_counter_offset;
    std::uint32_t marker_render_rva;
    std::uint32_t view_update_matrix_rva;
};

// Everything this mod needs to know about one shipped build of
// WolfNewOrder_x64.exe. Routed by PE fingerprint, never by a version string:
// the mod has to identify the build from inside the running process, and the
// PE header is the only thing there that says which binary this is.
//
// Profiles are APPEND-ONLY. A patch that moves an RVA gets a NEW profile added
// to the top of kKnownProfiles; the existing one stays exactly as it is, so a
// player who has not taken the patch keeps matching it. Never edit an RVA in
// place - that strands every user on the older build with no fix available to
// them at all.
struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint fingerprint;

    // idRenderView::Setup(int windowW, int windowH, int renderW, int renderH).
    // Copies the game-supplied renderView_t (`g`, at offset 0 of idRenderView)
    // into the render copy (`r`), then derives the projection, view and MVP
    // matrices from `r`. Modifying `g` in a pre-hook is therefore the whole
    // render-phase injection: every matrix the frame is drawn and culled with
    // comes out of it, and the game's own copy is put back before the hook
    // returns.
    std::uint32_t render_view_setup_rva;

    // Byte offsets inside renderView_t.
    std::uint32_t rv_vieworg_offset;   // idVec3, world position of the eye
    std::uint32_t rv_viewaxis_offset;  // idMat3, rows are forward / left / up

    // The frame's field of view, as full angles in degrees. Setup hands the
    // render copy to the projection builder, which reads exactly these two and
    // builds the matrix from tan(fov/2) - so they are the FOV the frame is
    // actually drawn with, not a stored preference. The mod never writes them:
    // the game has its own FOV slider. fov_y is read to scale the head pose to
    // the zoom, against g_fov_value_rva below.
    std::uint32_t rv_fov_x_offset;
    std::uint32_t rv_fov_y_offset;

    // bool: when set, the projection builder ignores fov_x / fov_y entirely and
    // memcpy's an explicit 4x4 matrix out of renderView_t instead. Read so the
    // log cannot report two angles the frame was not drawn with.
    std::uint32_t rv_explicit_projection_offset;

    // idGameLocal's vtable, in .rdata. The object itself is heap allocated, so
    // this is what the runtime scan for the singleton keys on.
    std::uint32_t game_local_vtable_rva;

    // Byte offsets inside idGameLocal, taken from the reflection tables the
    // engine ships for its own type dump.
    std::uint32_t game_gamestate_offset;     // gameState_t, GAMESTATE_ACTIVE while playing
    std::uint32_t game_is_paused_offset;     // bool gameIsPaused
    // idList<idPlayer*> playerEntities: a T* then an int count. Read as a shape
    // check when picking the live idGameLocal out of the stale copies that
    // share its vtable, never for the players themselves.
    std::uint32_t game_player_list_offset;

    // idPresentablePlayer's vtable, in .rdata. Like idGameLocal the object is
    // heap allocated with no global pointing at it, so this is what the runtime
    // sweep keys on. It carries the aim-down-sights state the lean fade reads.
    std::uint32_t presentable_player_vtable_rva;

    // idHands is an EMBEDDED member of idPresentablePlayer, not a separate
    // allocation, and it carries its own vtable plus an `owner` back-pointer to
    // the player. That triple - the player's vptr, the embedded hands' vptr and
    // the back-pointer resolving to the player again - is what tells the live
    // object from a freed one whose memory still holds the vtable.
    std::uint32_t player_hands_offset;
    std::uint32_t hands_vtable_rva;
    std::uint32_t hands_owner_offset;

    // bool idPresentablePlayer::wantZoom. The engine's own held aim-down-sights
    // state: set from the aim input, and forced back to false by the engine on
    // every path where zooming is not allowed. Polled once per frame, never
    // latched on an edge.
    std::uint32_t player_want_zoom_offset;

    // The float value of the static idCVar g_fov, the game's own FOV setting in
    // degrees: a nominal angle, horizontal at a 16:9 reference. The player view's
    // FOV is this at the hip and narrows from it as the sights come up, so it is
    // the base the zoom compensation measures a zoom against.
    std::uint32_t g_fov_value_rva;
    const ReticleOffsets* reticle = nullptr;
};

}  // namespace wolf_ht::builds
