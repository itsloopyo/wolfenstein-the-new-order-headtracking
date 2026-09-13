// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "game_state.h"

#include <cstdint>

#include "cameraunlock/memory/safe_memory.h"

namespace wolf_ht {

GameState::GameState(const builds::BuildProfile& profile) : m_profile(profile) {
    m_resolver.Start({profile.game_local_vtable_rva, profile.game_gamestate_offset,
                      profile.game_player_list_offset});
    m_playerResolver.Start({profile.presentable_player_vtable_rva, profile.player_hands_offset,
                            profile.hands_vtable_rva, profile.hands_owner_offset});
}

bool GameState::IsAiming() {
    void* player = m_playerResolver.Get();
    if (player == nullptr) return false;

    // idPresentablePlayer::wantZoom. The engine's own held aim state: the aim
    // input writes it, and the engine clears it itself on every path where
    // zooming is refused, so it cannot be left set by a transition the mod did
    // not see. The per-frame engine code that actually raises and lowers the
    // sights reads this same byte.
    const auto base = reinterpret_cast<std::uintptr_t>(player);
    std::uint8_t wantZoom = 0;
    if (!cameraunlock::memory::SafeRead(base + m_profile.player_want_zoom_offset, wantZoom)) {
        return false;
    }
    return wantZoom != 0;
}

bool GameState::GetFrameNumber(int& frame) {
    if (!m_profile.reticle) return false;
    void* game = m_resolver.Get();
    if (!game) return false;
    // The idView frame stamp uses idGameLocal's virtual accessor at +0x168.
    frame = *reinterpret_cast<const int*>(reinterpret_cast<std::uintptr_t>(game) +
        m_profile.reticle->game_frame_counter_offset);
    return true;
}

bool GameState::IsGameplay() {
    void* gameLocal = m_resolver.Get();
    if (gameLocal == nullptr) {
        m_reason = "no level loaded";
        return false;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(gameLocal);

    std::int32_t gamestate = 0;
    if (!cameraunlock::memory::SafeRead(base + m_profile.game_gamestate_offset, gamestate)) {
        m_reason = "no level loaded";
        return false;
    }
    if (gamestate != idtech::kGameStateActive) {
        // Everything else is the shell, a level load, or a bink video.
        m_reason = "not in a level";
        return false;
    }

    // gameIsPaused covers the pause menu and every shell screen opened over a
    // live level, and it is also what the engine sets when the window loses
    // focus - so an alt-tab holds the view still too, which is what a player
    // coming back to the game expects.
    //
    // The idMainMenu pointer next to it is deliberately NOT part of this test:
    // it is allocated for the whole session and is non-null throughout ordinary
    // gameplay, so gating on it would suppress tracking permanently.
    std::uint8_t paused = 0;
    if (!cameraunlock::memory::SafeRead(base + m_profile.game_is_paused_offset, paused)) {
        // Same direction as the gamestate read above, and for the same reason: a
        // frame we cannot read answers "not gameplay". Folding the failed read
        // into "not paused" would answer gameplay instead, and a menu with an
        // unreadable pause flag gets a swimming camera.
        //
        // Its own reason string, not the gamestate read's. The level IS loaded
        // here - the object has already passed the vtable, gamestate-range and
        // player-list checks - so "no level loaded" would send a report about a
        // wrong game_is_paused_offset looking in the wrong place entirely.
        m_reason = "the pause flag could not be read";
        return false;
    }
    if (paused != 0) {
        m_reason = "paused";
        return false;
    }

    m_reason = "gameplay";
    return true;
}

}  // namespace wolf_ht
