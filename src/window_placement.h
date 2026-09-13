// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <windows.h>

#include <cstdlib>

namespace wolf_ht {

// What should happen to the window the poll settled on. Pure geometry, which is
// the only part of the centring that can be checked without a running game -
// tests/window_placement_tests.cpp is that check.
enum class Placement {
    AlreadyCentered,
    TooLargeForWorkArea,
    Center,
};

inline int CenteredOrigin(int areaStart, int areaExtent, int windowExtent) {
    return areaStart + (areaExtent - windowExtent) / 2;
}

inline bool IsCenteredOn(const RECT& window, const RECT& area) {
    // A game that centres its own window rounds the odd half-pixel up where the
    // integer maths here rounds it down, so an exact comparison would move the
    // window one pixel and report that as a fix.
    constexpr int kTolerance = 2;
    const int dx = static_cast<int>(window.left) -
                   CenteredOrigin(static_cast<int>(area.left),
                                  static_cast<int>(area.right - area.left),
                                  static_cast<int>(window.right - window.left));
    const int dy = static_cast<int>(window.top) -
                   CenteredOrigin(static_cast<int>(area.top),
                                  static_cast<int>(area.bottom - area.top),
                                  static_cast<int>(window.bottom - window.top));
    return std::abs(dx) <= kTolerance && std::abs(dy) <= kTolerance;
}

// Decides where the window belongs. `target` is written only for Placement::Center.
inline Placement DecidePlacement(const RECT& window, const RECT& work, const RECT& monitor,
                                 POINT& target) {
    // Either reading counts as centred. A game centres on the monitor, this mod
    // centres on the work area, and the two differ by half the taskbar. Moving a
    // window that is already centred trades a visible jump for nothing, and a
    // fullscreen or borderless window is centred on its monitor by definition,
    // which is how windowed mode ends up being the only case that moves.
    if (IsCenteredOn(window, work) || IsCenteredOn(window, monitor)) {
        return Placement::AlreadyCentered;
    }

    const int width = static_cast<int>(window.right - window.left);
    const int height = static_cast<int>(window.bottom - window.top);
    const int workWidth = static_cast<int>(work.right - work.left);
    const int workHeight = static_cast<int>(work.bottom - work.top);

    // Centring a window bigger than the work area puts its title bar behind the
    // taskbar or off the top of the screen, and the player cannot then drag it
    // back.
    if (width >= workWidth || height >= workHeight) return Placement::TooLargeForWorkArea;

    target.x = CenteredOrigin(static_cast<int>(work.left), workWidth, width);
    target.y = CenteredOrigin(static_cast<int>(work.top), workHeight, height);
    return Placement::Center;
}

}  // namespace wolf_ht
