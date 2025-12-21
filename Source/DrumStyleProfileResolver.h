#pragma once

#include "DrumStyleRhythmProfile.h"

// This enum MUST match whatever style enum BOOM already uses.
// If you already have one, USE IT instead of this.
enum class DrumStyle
{
    HipHop,
    Trap,
    Drill,
    Reggaeton,
    EDM,
    Pop,
    RnB,
    Rock,
    Wxstie
};

DrumStyleRhythmProfile getDrumStyleProfile(DrumStyle style);

