// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "builds/build_profile.h"
#include "idtech/game_singletons.h"

namespace wolf_ht {

// Whether the player is in control of the view right now.
//
// Head tracking is suppressed everywhere else - the shell, a level load, the
// pause menu, a bink video, an alt-tab - so a menu the player is reading does
// not swim about while they look at the keyboard.
class GameState {
public:
    explicit GameState(const builds::BuildProfile& profile);

    // Two guarded loads off a pointer another thread resolved, so this is cheap
    // enough for the per-frame call the render hook makes.
    bool IsGameplay();

    // Whether the iron sights or a scope are up right now.
    //
    // POLLED, never latched on an edge. The engine has several paths that take
    // the sights down without an event a mod could hook - a weapon switch, a
    // sprint, a scripted takedown - and a flag that missed one of them would
    // strand the player in ADS behaviour for the rest of the level. An
    // unreadable frame answers false: failing toward stock is the safe way to
    // be wrong.
    bool IsAiming();
    bool GetFrameNumber(int& frame);

    // The last state that was actually observed, for the log line.
    const char* LastReason() const { return m_reason; }

private:
    const builds::BuildProfile& m_profile;
    idtech::GameLocalResolver m_resolver;
    idtech::PresentablePlayerResolver m_playerResolver;
    const char* m_reason = "";
};

}  // namespace wolf_ht
