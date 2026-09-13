// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "builds/build_registry.h"

#include <windows.h>

#include "logging.h"

namespace wolf_ht::builds {

extern const BuildProfile kGdkProfile_20210413;
extern const BuildProfile kSteamProfile_20140618;

namespace {

// Newest first. The head of this array is the diagnostic primary: when nothing
// matches, it is what the running EXE is compared against to say whether the
// game is newer or older than anything this mod knows about.
const BuildProfile* const kKnownProfiles[] = {
    &kGdkProfile_20210413,
    &kSteamProfile_20140618,
};

bool ReadRunningFingerprint(cameraunlock::memory::PeFingerprint& out) {
    return cameraunlock::memory::ReadPeFingerprint(GetModuleHandleW(nullptr), out);
}

}  // namespace

const BuildProfile* ResolveRunningBuild() {
    cameraunlock::memory::PeFingerprint running{};
    if (!ReadRunningFingerprint(running)) return nullptr;
    for (const BuildProfile* profile : kKnownProfiles) {
        if (running.Matches(profile->fingerprint)) return profile;
    }
    return nullptr;
}

void LogBuildResolution() {
    cameraunlock::memory::PeFingerprint running{};
    if (!ReadRunningFingerprint(running)) {
        Log::Line("[build] could not read the PE header of the running EXE; staying dormant");
        return;
    }

    if (const BuildProfile* profile = ResolveRunningBuild()) {
        Log::Line("[build] matched %s (TimeDateStamp 0x%08X SizeOfImage 0x%08X CheckSum 0x%08X)",
                  profile->name, running.TimeDateStamp, running.SizeOfImage, running.CheckSum);
        return;
    }

    const BuildProfile* primary = kKnownProfiles[0];
    switch (cameraunlock::memory::ClassifyMismatch(running, primary->fingerprint)) {
        case cameraunlock::memory::FingerprintMismatch::Newer:
            Log::Line("[build] this WolfNewOrder_x64.exe is newer than any build this mod knows "
                      "about (running 0x%08X, newest known %s 0x%08X). Head tracking is off and "
                      "the game is unmodified; check the releases page for an updated mod.",
                      running.TimeDateStamp, primary->name, primary->fingerprint.TimeDateStamp);
            break;
        case cameraunlock::memory::FingerprintMismatch::Older:
            Log::Line("[build] this WolfNewOrder_x64.exe is older than the newest build this mod "
                      "knows about (running 0x%08X, newest known %s 0x%08X). Head tracking is off "
                      "and the game is unmodified; let the store finish updating.",
                      running.TimeDateStamp, primary->name, primary->fingerprint.TimeDateStamp);
            break;
        case cameraunlock::memory::FingerprintMismatch::Differs:
            Log::Line("[build] this WolfNewOrder_x64.exe has the expected build date but a "
                      "different size or checksum (SizeOfImage 0x%08X vs 0x%08X, CheckSum "
                      "0x%08X vs 0x%08X). The mod does not engage on a modified binary.",
                      running.SizeOfImage, primary->fingerprint.SizeOfImage,
                      running.CheckSum, primary->fingerprint.CheckSum);
            break;
    }
}

}  // namespace wolf_ht::builds
