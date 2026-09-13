// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "builds/build_profile.h"

namespace wolf_ht::builds {

namespace {
constexpr ReticleOffsets kReticle = {
    0x5F5010u, 0x19E700u, 0x3EAE10u, 0x1CECD98u, 0x232470u, 0x232430u,
    0x1187A8u, 0x608A70u, 0x9456A0u,
};
}

// Xbox Game Pass / Microsoft Store build of Wolfenstein: The New Order,
// package BethesdaSoftworks.WolfensteinTNO-PC, WolfNewOrder_x64.exe. A 2021
// GDK port of the 2014 game: same engine and same class layouts, relinked with
// a newer toolchain, so every struct offset below is the Steam profile's and
// every RVA is its own.
//
// The packaged EXE cannot be read off disk - the licensing layer denies it even
// though the rest of the package reads fine - so, as with the Steam copy's DRM
// stub, everything here was derived from the image in the running process.
//
// The CheckSum field is genuinely 0 in this build's optional header. That is
// what the running EXE reports, so it routes correctly; it just means the
// fingerprint leans on TimeDateStamp and SizeOfImage for its tamper check.
//
// Every struct offset is byte-identical to kSteamProfile_20140618, which is a
// measurement rather than an assumption: the engine's own field reflection
// tables were dumped from both images and the renderView_t, idRenderView,
// idGameLocal, idPresentablePlayer and idHands tables compare equal on field
// name, offset and size throughout. The only differences in those tables are
// declared TYPE spellings, where the 2021 toolchain prints default template
// arguments the 2014 one elided (idStaticList < T , 9 > against
// idStaticList < T , 9 , false , TAG_IDLIST >).
//
// render_view_setup_rva is idRenderView::Setup, reached the same way as on
// Steam - the function referencing "idRenderView: MVP Matrix Invert failed!" -
// with one extra step: in this build the two matrix-invert failure paths are
// split into a cold fragment at 0x474BEE, so the .pdata entry covering the
// string reference is a UNW_FLAG_CHAININFO stub and has to be followed to its
// primary. Setup's body then matches the Steam function instruction for
// instruction: copy `g` into `r` at this + 0x1A40, build the projection into
// this + 0x33C0, invert, carry on at this + 0x3400.
//
// The three vtable RVAs come from the image's MSVC RTTI, by the same route as
// the Steam profile's: type descriptor .?AVidGameLocal@@ / .?AVidPresentable
// Player@@ / .?AVidHands@@, to its complete object locator, to the .rdata slot
// holding that locator, whose next qword is the vtable.
extern const BuildProfile kGdkProfile_20210413 = {
    "gdk-win64-20210413",
    { 0x6075C22Au, 0x0219A000u, 0x00000000u },

    0x4749C0u,  // idRenderView::Setup

    0x60u,      // renderView_t::vieworg
    0x6Cu,      // renderView_t::viewaxis

    0x10u,      // renderView_t::fov_x
    0x14u,      // renderView_t::fov_y
    0x5Cu,      // renderView_t::useExplicitProjectionMatrix

    0xF2FEB0u,  // idGameLocal vtable

    1508892u,   // idGameLocal::gamestate
    1508587u,   // idGameLocal::gameIsPaused
    189440u,    // idGameLocal::playerEntities

    0xE8A318u,  // idPresentablePlayer vtable
    55280u,     // idPresentablePlayer::hands (embedded idHands)
    0xF4D2E8u,  // idHands vtable
    24u,        // idHands::owner
    67596u,     // idPresentablePlayer::wantZoom
    &kReticle,
};

}  // namespace wolf_ht::builds
