// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "builds/build_profile.h"

namespace wolf_ht::builds {

// The profile matching the running WolfNewOrder_x64.exe, or nullptr when no
// known build fits. A null result is not an error the mod recovers from: it
// leaves every hook uninstalled, so the game runs exactly as it would without
// the mod. Hooking against RVAs derived from a different build crashes the
// player's game seconds in, which is the one outcome worth staying dormant for.
const BuildProfile* ResolveRunningBuild();

// Writes one line saying which profile matched, or - when none did - which way
// the running EXE differs from the newest profile the mod knows about, so a
// report arrives with the reason already in it.
void LogBuildResolution();

}  // namespace wolf_ht::builds
