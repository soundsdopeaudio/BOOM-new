#pragma once
#include "HatStyleRuleSet.h"
#include <vector>

/*
    Drum grid rows (authoritative):

    0 = Kick
    1 = Snare
    2 = HiHat
    3 = OpenHat
    4 = Perc 1
    5 = Perc 2
    6 = Perc 3
*/

// ------------------------------------------------------------
// Logical roles (not rows)
// ------------------------------------------------------------
enum class DrumRole
{
    Kick,
    Snare,
    HiHat,
    OpenHat,
    Perc
};

// ------------------------------------------------------------
// Per-role rhythmic behavior rules
// ------------------------------------------------------------
struct DrumRoleRules
{
    // Step indices within a 16-step bar (0..15)
    std::vector<int> mandatorySteps;   // MUST exist every bar
    std::vector<int> forbiddenSteps;   // MUST NOT exist
    std::vector<int> preferredSteps;   // probabilistic bias

    // Density shaping after generation
    // negative = thin notes
    // positive = add notes
    int densityBias = 0;               // -100 .. +100

    // Velocity shaping
    int velocityBias = 0;              // -100 .. +100

    // Articulation permissions
    bool allowRolls = false;           // hats only, mostly
    bool allowTriplets = false;
};

// ------------------------------------------------------------
// Complete style profile
// ------------------------------------------------------------
struct DrumStyleRhythmProfile
{
    DrumRoleRules kick;
    DrumRoleRules snare;
    DrumRoleRules hiHat;
    DrumRoleRules openHat;
    DrumRoleRules perc;

    // Global style traits
    bool forceRigidGrid = false;   // Reggaeton / EDM
    bool allowSwing = false;
    HatStyleRuleSet hats;
    bool allowTriplets = false;
};