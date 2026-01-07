#pragma once
#include <array>

struct BassStyleRuleSet
{
    // ---- Core density / placement ----
    int stepHitBiasOddPct = 0;     // boosts hits on odd steps (trap/drill bounce)
    int stepHitBiasEvenPct = 0;    // boosts hits on even steps (rare, but available)

    // ---- Octave / range ----
    int baseOctave = 2;            // base octave before UI octave offset
    int minOctave = 1;
    int maxOctave = 6;

    // ---- Sustains ----
    int sustainMinSteps = 1;       // step length range when NOT bursting
    int sustainMaxSteps = 2;

    // ---- Burst / ratchet behavior ----
    int burstPct = 30;             // chance we do a sub-division burst
    int burstMinSteps = 1;         // burst total duration in steps
    int burstMaxSteps = 3;
    std::array<int, 5> burstSubTickPool{ 24, 12, 8, 6, 4 }; // allowed sub ticks (filtered by allowTriplets)

    // ---- Melodic motion weights (sum is arbitrary; we compare by cumulative) ----
    int wStay = 50;                // 0 delta
    int wFifth = 25;               // +4 scale degrees (approx)
    int wSmallStep = 15;           // +/-1
    int wDownStep = 10;            // -2 or -3 depending on style config below
    int wOctLeap = 0;              // +/-7 (rare unless drill/trap)

    // style-specific: what “down step” means (2 or 3)
    int downStepSize = 2;

    // ---- Downbeat enforcement ----
    int forceDownbeatPct = 35;     // if pattern is sparse, force a note on beat 1
    int downbeatRootPct = 75;      // on downbeat, choose root vs fifth etc.
};
#pragma once
