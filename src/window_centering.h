// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

namespace wolf_ht {

// Waits for the game to bring its render window up and hold it still, then
// centres it on the work area of the monitor it is already on. A window that
// fills the screen, or that the game placed centred itself, is left alone - so
// this only ever moves a windowed-mode game, and it moves it once.
//
// Blocks until the window settles or the wait times out, so call it after
// everything else the startup thread has to do.
void CenterWindowWhenReady();

}  // namespace wolf_ht
