#include "DrumStyleRhythmProfile.h"

DrumStyleRhythmProfile makeTrapProfile()
{
    DrumStyleRhythmProfile p;

    // Snare: beat 3 only
    p.snare.mandatorySteps = { 8 };
    p.snare.forbiddenSteps = { 4, 12 };

    // Hats: everywhere
    for (int i = 0; i < 16; ++i)
        p.hats.preferredSteps.insert(i);

    p.allowTriplets = true;
    return p;
}

DrumStyleRhythmProfile makeDrillProfile()
{
    DrumStyleRhythmProfile p;

    // Avoid clean backbeats
    p.snare.forbiddenSteps = { 4, 12 };
    p.snare.preferredSteps = { 6, 10, 14 };

    // Kick fights snare
    p.kick.preferredSteps = { 3, 7, 11 };

    return p;
}

DrumStyleRhythmProfile makeEdmProfile()
{
    DrumStyleRhythmProfile p;

    // Kick every quarter
    p.kick.mandatorySteps = { 0, 4, 8, 12 };

    // Snare on 2 & 4
    p.snare.mandatorySteps = { 4, 12 };

    // Hats offbeats
    p.hats.preferredSteps = { 2, 6, 10, 14 };

    return p;
}

DrumStyleRhythmProfile makeReggaetonProfile()
{
    DrumStyleRhythmProfile p;

    // Reggaeton is rigid and pattern-authoritative
    p.forceRigidGrid = true;
    p.allowSwing = false;

    // ------------------------------------------------
    // KICK — dembow foundation
    // Typical hits: 1 and the "and" before 3
    // Steps: 0 and 7 (16-step grid)
    // ------------------------------------------------
    p.kick.mandatorySteps = { 0, 7 };
    p.kick.forbiddenSteps = { 4, 8, 12 }; // avoid backbeat feel
    p.kick.densityBias = -20;
    p.kick.velocityBias = +10;

    // ------------------------------------------------
    // SNARE — dembow clap
    // Classic dembow lands late in bar
    // ------------------------------------------------
    p.snare.mandatorySteps = { 4, 12 };
    p.snare.forbiddenSteps = { 0, 8 };
    p.snare.velocityBias = +15;

    // ------------------------------------------------
    // HI-HAT — light, steady, not busy
    // ------------------------------------------------
    p.hiHat.preferredSteps = { 2, 6, 10, 14 };
    p.hiHat.densityBias = -30;
    p.hiHat.allowRolls = false;
    p.hiHat.allowTriplets = false;

    // ------------------------------------------------
    // OPEN HAT — sparse accents only
    // ------------------------------------------------
    p.openHat.preferredSteps = { 10 };
    p.openHat.densityBias = -60;

    // ------------------------------------------------
    // PERC — syncopated embellishments
    // ------------------------------------------------
    p.perc.preferredSteps = { 3, 11 };
    p.perc.densityBias = -10;

    return p;
}


DrumStyleRhythmProfile makeRnBProfile()
{
    DrumStyleRhythmProfile p;

    // Snare: backbeat, but flexible
    p.snare.mandatorySteps = { 4, 12 };
    p.snare.preferredSteps = { 6, 14 }; // late feel

    // Kick: syncopated, rarely square
    p.kick.preferredSteps = { 0, 7, 11 };

    // Hats: expressive 16ths
    p.hats.preferredSteps = { 2, 6, 10, 14 };

    p.allowSwing = true;
    p.allowTriplets = false;

    return p;
}

DrumStyleRhythmProfile makePopProfile()
{
    DrumStyleRhythmProfile p;

    // Snare: strict backbeat
    p.snare.mandatorySteps = { 4, 12 };

    // Kick: strong 1, often 3
    p.kick.preferredSteps = { 0, 8 };

    // Hats: steady 8ths
    p.hats.mandatorySteps = { 0, 2, 4, 6, 8, 10, 12, 14 };

    p.allowSwing = false;
    p.allowTriplets = false;

    return p;
}

DrumStyleRhythmProfile makeRockProfile()
{
    DrumStyleRhythmProfile p;

    // Snare: absolute backbeat
    p.snare.mandatorySteps = { 4, 12 };

    // Kick: 1 and 3 are king
    p.kick.mandatorySteps = { 0, 8 };

    // Hats: quarters or 8ths, not busy
    p.hats.mandatorySteps = { 0, 4, 8, 12 };

    p.allowSwing = false;
    p.allowTriplets = false;

    return p;
}

DrumStyleRhythmProfile makeWxstieProfile()
{
    DrumStyleRhythmProfile p;

    // ---- SNARE ----
    // Not a traditional backbeat
    // Often late, often sparse, sometimes missing
    p.snare.preferredSteps = { 6, 10, 14 }; // late + unsettling
    p.snare.forbiddenSteps = { 4, 12 };     // no clean 2 & 4 anchors

    // ---- KICK ----
    // Sparse but intentional
    // Often reinforces emptiness
    p.kick.preferredSteps = { 0, 7, 11 };
    p.kick.forbiddenSteps = { 4, 12 };

    // ---- HATS ----
    // Texture, not groove
    // Irregular placement encouraged
    p.hats.preferredSteps = { 1, 5, 9, 13 };

    // ---- FEEL ----
    p.allowSwing = false;
    p.allowTriplets = false;

    return p;
}

DrumStyleRhythmProfile makeHipHopProfile()
{
    DrumStyleRhythmProfile p;

    // Snare: 2 & 4
    p.snare.mandatorySteps = { 4, 12 };

    // Kick
    p.kick.preferredSteps = { 0 };

    // Hats
    p.hats.preferredSteps = { 2, 6, 10, 14 };

    p.allowSwing = true;
    return p;
}