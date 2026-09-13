// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "builds/build_profile.h"

namespace wolf_ht::builds {

namespace {
constexpr ReticleOffsets kReticle = {
    0x5F13B0u, 0x2725D0u, 0x215320u, 0x1D7A360u, 0x313790u, 0x3137D0u,
    0x1187A8u, 0x5FFD50u, 0x9609F0u,
};
}

// Steam build of Wolfenstein: The New Order, WolfNewOrder_x64.exe, the 2014
// release. Steam has shipped no patch since, so this is the only profile.
//
// The EXE is wrapped in Steam's `.bind` DRM stub, so its .text is encrypted on
// disk and everything below was derived from the decrypted image as it exists
// in the running process. The stub decrypts in place, so these RVAs are the
// file's RVAs too - only the bytes at them differ before the stub has run.
//
// render_view_setup_rva is idRenderView::Setup, found from the only two
// functions that reference the string "idRenderView: MVP Matrix Invert
// failed!". The renderView_t member offsets come from the same function: it
// passes (this + 0x1AA0, this + 0x1AAC, this + 0x3440) to the view-matrix
// builder, and 0x1A40 is the render copy `r`, so vieworg sits 0x60 into
// renderView_t and viewaxis 0x6C. The idRenderView offsets are corroborated by
// the game's own reflection tables, which name +6528 m_pModelEnvOverride,
// +6720 r, +13376 viewMatrix and +13504 worldSpaceMVPMatrix - every one of
// which this function writes at the matching address.
//
// The FOV offsets come from the projection builder Setup calls (RVA 0x473740).
// It reads +0x10 and +0x14 off the render copy, refuses the frame through
// "idRenderWorldLocal::Render: bad FOVs: %f, %f" if either is not positive, and
// feeds each through tan(x * 0.01745329 * 0.5) - so both are full angles in
// degrees, and both are what the frame's projection matrix is built from. The
// same function's other branch, taken when the bool at +0x5C is set, memcpy's
// 0x40 bytes from +0x1C as the projection matrix and never looks at the two
// angles at all, which is why that bool is read alongside them.
extern const BuildProfile kSteamProfile_20140618 = {
    "steam-win64-20140618",
    { 0x53A185CFu, 0x02280000u, 0x018B09B3u },

    0x474130u,  // idRenderView::Setup

    0x60u,      // renderView_t::vieworg
    0x6Cu,      // renderView_t::viewaxis

    0x10u,      // renderView_t::fov_x
    0x14u,      // renderView_t::fov_y
    0x5Cu,      // renderView_t::useExplicitProjectionMatrix

    0x1000AC8u, // idGameLocal vtable

    1508892u,   // idGameLocal::gamestate
    1508587u,   // idGameLocal::gameIsPaused
    189440u,    // idGameLocal::playerEntities

    // The aim-down-sights side. idPresentablePlayer's and idHands' vtables were
    // read out of the image's MSVC RTTI - the type descriptors .?AVidPresentable
    // Player@@ and .?AVidHands@@, through their complete-object locators - by the
    // same routine that produced the idGameLocal vtable above, which it
    // reproduces exactly. The three struct offsets come from the engine's own
    // reflection tables: idPresentablePlayer carries `idHands hands` at +55280 as
    // an embedded member and `bool wantZoom` at +67596, and idHands carries
    // `idPresentablePlayer owner` at +24.
    //
    // wantZoom is the engine's held aim state rather than an animation flag.
    // Its setter (RVA 0x60CC40) writes the aim input into it and clears it on
    // every path where zooming is refused, and the per-frame reader at RVA
    // 0x60F4E0 is what actually raises and lowers the sights from it. The
    // neighbouring idHands::isZooming was rejected for this: it is only touched
    // inside the weapon-fire animation path, so it does not answer "are the
    // sights up" on a frame where nothing was fired.
    0xF4B228u,  // idPresentablePlayer vtable
    55280u,     // idPresentablePlayer::hands (embedded idHands)
    0x1022F38u, // idHands vtable
    24u,        // idHands::owner
    67596u,     // idPresentablePlayer::wantZoom
    &kReticle,
};

}  // namespace wolf_ht::builds
