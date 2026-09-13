// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "builds/build_profile.h"

namespace wolf_ht {
class HeadTrackingMod;

bool InstallReticleHook(const builds::BuildProfile& profile, HeadTrackingMod& mod);
}
