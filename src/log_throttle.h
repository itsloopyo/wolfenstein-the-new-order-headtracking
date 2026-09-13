// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

namespace wolf_ht {

// The per-frame log schedule the render-view diagnostic runs on: a burst of
// unconditional opening lines, then a dense interval while the run is still
// young, then a thin one for the rest of the session.
//
// The opening burst is where an install-time fault shows (a wrong offset, a
// profile that does not fit). The thin steady trickle is what a late report is
// read from - "it drifted after an hour", "it stopped when I loaded a save" -
// which a burst that goes silent cannot see. Thinning is also what keeps the
// render thread out of the log mutex, and what bounds the file: at the steady
// tier a 144Hz session writes a line every 14 seconds, not one per frame.
//
// Counted in two currencies on purpose: `m_frame` advances every call so the
// intervals are frames, while `m_lines` advances only when a line is actually
// emitted, so the tiers are lines. Render-thread only, like its caller.
//
// Unsigned because `m_frame` advances once per rendered frame for the whole
// session and nothing bounds it. Signed overflow is undefined, and past the
// wrap `m_frame % interval` would go negative and the steady trickle - the part
// a late report is read from - would stop. Unsigned wrap is defined and the
// modulo stays in range.
class LogThrottle {
public:
    // `burst` opening lines are emitted unconditionally. Until `earlyLines`
    // have been emitted the interval is `earlyIntervalFrames`, and from then on
    // `steadyIntervalFrames`. Passing burst == earlyLines is the two-tier case.
    constexpr LogThrottle(unsigned burst, unsigned earlyLines, unsigned earlyIntervalFrames,
                          unsigned steadyIntervalFrames)
        : m_burst(burst),
          m_earlyLines(earlyLines),
          m_earlyIntervalFrames(earlyIntervalFrames),
          m_steadyIntervalFrames(steadyIntervalFrames) {}

    // Call once per frame from the path being logged. True on the frames whose
    // line should be written.
    bool ShouldLog() {
        ++m_frame;
        if (m_lines < m_burst) {
            ++m_lines;
            return true;
        }
        const unsigned interval =
            m_lines < m_earlyLines ? m_earlyIntervalFrames : m_steadyIntervalFrames;
        if (m_frame % interval != 0) return false;
        ++m_lines;
        return true;
    }

private:
    const unsigned m_burst;
    const unsigned m_earlyLines;
    const unsigned m_earlyIntervalFrames;
    const unsigned m_steadyIntervalFrames;

    unsigned m_frame = 0;
    unsigned m_lines = 0;
};

}  // namespace wolf_ht
