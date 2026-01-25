#pragma once
#include <JuceHeader.h>

enum class HatStyle
{
    Trap,
    Drill,
    HipHop,
    Wxstie,
    Rock,
    Pop,
    RnB,
    Reggaeton,
    EDM
};

struct HatStyleRuleSet
{

        int steadyPulsePct;     // chance to generate a steady pulse
        int gapPct;             // chance to allow gaps
        int offKilterPct;       // risky / syncopated rhythms

        int rollChancePct;      // chance to insert a roll
        int ascendPct;          // % of rolls ascending
        int descendPct;         // % of rolls descending
        int stationaryPct;      // % stationary rolls

        bool favorTriplets;     // stylistic bias toward triplets

    // -----------------------------
    // Pulse behavior
    // -----------------------------
    float steadyPulseChance;        // 0.0–1.0
    bool  allowPulseGaps;
    float pulseGapChance;

    // -----------------------------
    // Rhythmic grid preference
    // -----------------------------
    float tripletDominance;          // 0.0 = straight, 1.0 = mostly triplets
    bool  allowStraight;
    bool  allowHybrid;

    // -----------------------------
    // Rolls
    // -----------------------------
    float rollChance;
    float ascendingRollChance;
    float descendingRollChance;
    float stationaryRollChance;

    // -----------------------------
    // Second-note overlays
    // -----------------------------
    float secondNoteChance;
    float secondNoteOffsetRisk;

    // -----------------------------
    // Risk / left-field behavior
    // -----------------------------
    float riskChance;
    int   riskCooldownGenerations;

    // -----------------------------
    // Special cases
    // -----------------------------
    bool  allowRareSteadyPulse;
    int   rareSteadyPulseInterval;
    int innerDensityPct;
};

const HatStyleRuleSet& getHatStyleRuleSet(HatStyle style);
