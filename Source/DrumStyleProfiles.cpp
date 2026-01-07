#include "DrumStyleRhythmProfile.h"

DrumStyleRhythmProfile makeTrapProfile()
{
    DrumStyleRhythmProfile p;

    // Snare: beat 3 only
    p.snare.mandatorySteps = { 8 };
    p.snare.forbiddenSteps = { 4, 12 };

    // Hats: everywhere
    for (int i = 0; i < 16; ++i)
        p.hiHat.preferredSteps.push_back(i);

    p.hiHat.allowTriplets = true;
    p.kick.allowTriplets = true;
    p.snare.allowTriplets = true;
    return p;
}

DrumStyleRhythmProfile makeDrillProfile()
{
    DrumStyleRhythmProfile p;

    // DRILL GOALS:
    // - Snare/clap "anchor" feels like beat 3 (step 8), but not always perfectly clean
    // - Hats are the main character: triplets + rolls allowed
    // - Kicks are syncopated and reactive
    // - Avoid classic 2&4 backbeat feel

    p.forceRigidGrid = false;
    p.allowSwing = false;
    p.allowTriplets = true;

    // -------------------------
    // SNARE: prefer beat 3, avoid clean 2&4
    // -------------------------
    p.snare.mandatorySteps.clear();          // "mostly", not "always"
    p.snare.forbiddenSteps = { 4, 12 };      // avoid 2 & 4 anchors
    p.snare.preferredSteps = { 8, 7, 9, 10, 14, 6 }; // beat3 + slight shifts/late hits
    p.snare.densityBias = -15;               // drill snares are often sparse
    p.snare.velocityBias = +15;
    p.snare.allowRolls = false;
    p.snare.allowTriplets = true;

    // -------------------------
    // KICK: syncopated, lots of valid placements
    // -------------------------
    p.kick.mandatorySteps.clear();
    p.kick.forbiddenSteps.clear();
    p.kick.preferredSteps =
    {
        0,  // downbeat
        3,  // before 2
        5,  // after 2
        7,  // before 3
        9,  // after 3
        11, // late 3
        13, // after 4
        15, // pickup
        14  // late pickup
    };
    p.kick.densityBias = 0;      // let your rest slider control sparseness
    p.kick.velocityBias = +10;
    p.kick.allowRolls = false;
    p.kick.allowTriplets = true;

    // -------------------------
    // HI-HAT: dense + triplet-friendly, rolls allowed
    // -------------------------
    // Give it a wide preferred set so the hat engine has freedom.
    p.hiHat.mandatorySteps.clear();
    p.hiHat.forbiddenSteps.clear();
    p.hiHat.preferredSteps.clear();
    for (int i = 0; i < 16; ++i)
        p.hiHat.preferredSteps.push_back(i);

    p.hiHat.densityBias = +15;     // hats are busy in drill
    p.hiHat.velocityBias = -10;
    p.hiHat.allowRolls = true;
    p.hiHat.allowTriplets = true;

    // -------------------------
    // OPEN HAT: occasional accents (not constant)
    // -------------------------
    p.openHat.mandatorySteps.clear();
    p.openHat.forbiddenSteps.clear();
    p.openHat.preferredSteps = { 2, 10, 14, 15, 6 };
    p.openHat.densityBias = -40;
    p.openHat.velocityBias = +5;

    // -------------------------
    // PERC: supportive syncopation
    // -------------------------
    p.perc.mandatorySteps.clear();
    p.perc.forbiddenSteps.clear();
    p.perc.preferredSteps = { 1, 5, 9, 11, 13, 15, 3, 7 };
    p.perc.densityBias = -25;
    p.perc.velocityBias = -5;

    return p;
}


DrumStyleRhythmProfile makeEdmProfile()
{
    DrumStyleRhythmProfile p;

    // EDM foundation (house/club-ish):
    // - Kick: 4-on-the-floor (every quarter)
    // - Snare/Clap: 2 & 4
    // - OpenHat: offbeats
    // - Hats: 8ths/16ths texture
    p.forceRigidGrid = true;
    p.allowSwing = false;
    p.allowTriplets = false;

    // KICK: 4 on the floor, with optional extra pushes
    p.kick.mandatorySteps = { 0, 4, 8, 12 };
    p.kick.preferredSteps = { 15, 3, 7, 11, 14 }; // pickups into the next beat
    p.kick.forbiddenSteps.clear();
    p.kick.densityBias = +15;
    p.kick.velocityBias = +10;
    p.kick.allowRolls = false;
    p.kick.allowTriplets = false;

    // SNARE: strong 2 & 4
    p.snare.mandatorySteps = { 4, 12 };
    p.snare.preferredSteps = { 6, 14 }; // occasional late push
    p.snare.forbiddenSteps.clear();
    p.snare.densityBias = -10;
    p.snare.velocityBias = +15;
    p.snare.allowTriplets = false;

    // HI-HAT: mostly steady drive; allow occasional 16ths via preference
    p.hiHat.mandatorySteps = { 0, 2, 4, 6, 8, 10, 12, 14 }; // steady 8ths
    p.hiHat.preferredSteps = { 1, 3, 5, 7, 9, 11, 13, 15 }; // occasional 16ths
    p.hiHat.forbiddenSteps.clear();
    p.hiHat.densityBias = +10;
    p.hiHat.velocityBias = -5;
    p.hiHat.allowRolls = true;     // for builds (your generator decides how/when)
    p.hiHat.allowTriplets = false;

    // OPEN HAT: classic offbeats + occasional end-of-bar lift
    p.openHat.mandatorySteps.clear();
    p.openHat.preferredSteps = { 2, 6, 10, 14, 15 };
    p.openHat.forbiddenSteps.clear();
    p.openHat.densityBias = -25;
    p.openHat.velocityBias = +5;

    // PERC: fills and light syncopation (avoid crowding)
    p.perc.mandatorySteps.clear();
    p.perc.preferredSteps = { 3, 7, 11, 15, 5, 13 };
    p.perc.forbiddenSteps.clear();
    p.perc.densityBias = -20;
    p.perc.velocityBias = 0;

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
    // Classic dembow: "2-and" beat (step 6 in 16-step grid)
    // This is the signature dembow "chick" halfway between beats 2 and 3
    // ------------------------------------------------
    p.snare.mandatorySteps = { 6 };  // The "2-and" dembow accent
    p.snare.forbiddenSteps = { 0, 4, 8, 12 };  // Forbid all strong backbeats
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

    // R&B foundation:
    // - Backbeat is present, but pocket/late accents + syncopated kick
    // - More hat texture, ghost accents, tasteful percs
    p.forceRigidGrid = false;
    p.allowSwing = true;
    p.allowTriplets = true;

    // SNARE: 2 & 4, plus ghost/late accents (NOT mandatory)
    p.snare.mandatorySteps = { 4, 12 };
    p.snare.preferredSteps = { 6, 14, 15, 3, 11 }; // late + ghost-ish spots
    p.snare.forbiddenSteps.clear();
    p.snare.densityBias = -15;
    p.snare.velocityBias = +10;
    p.snare.allowTriplets = true;

    // KICK: syncopated, "meaningful" hits
    p.kick.mandatorySteps.clear();
    p.kick.preferredSteps = { 0, 7, 11, 13, 15, 2, 10, 14, 8 };
    p.kick.forbiddenSteps.clear();
    p.kick.densityBias = -10;
    p.kick.velocityBias = +5;
    p.kick.allowTriplets = true;

    // HI-HAT: texture (8ths + selective 16ths), with rolls allowed
    p.hiHat.mandatorySteps = { 0, 2, 4, 6, 8, 10, 12, 14 };
    p.hiHat.preferredSteps = { 1, 3, 5, 7, 9, 11, 13, 15 };
    p.hiHat.forbiddenSteps.clear();
    p.hiHat.densityBias = -20;
    p.hiHat.velocityBias = -10;
    p.hiHat.allowRolls = true;
    p.hiHat.allowTriplets = true;

    // OPEN HAT: sparse accents (late/offbeat)
    p.openHat.mandatorySteps.clear();
    p.openHat.preferredSteps = { 2, 10, 14, 15, 6 };
    p.openHat.forbiddenSteps.clear();
    p.openHat.densityBias = -45;

    // PERC: tasteful syncopation
    p.perc.mandatorySteps.clear();
    p.perc.preferredSteps = { 1, 5, 9, 11, 14, 15, 3, 7, 13 };
    p.perc.forbiddenSteps.clear();
    p.perc.densityBias = -20;

    return p;
}


DrumStyleRhythmProfile makePopProfile()
{
    DrumStyleRhythmProfile p;

    // Pop foundation:
    // - Snare: 2 & 4
    // - Kick: strong 1 (+ common variations)
    // - Hats: steady 8ths, occasional 16ths
    p.forceRigidGrid = false;
    p.allowSwing = false;
    p.allowTriplets = false;

    // SNARE: strict backbeat, plus occasional pushes
    p.snare.mandatorySteps = { 4, 12 };
    p.snare.preferredSteps = { 14, 6 }; // little pushes / late feel sometimes
    p.snare.forbiddenSteps.clear();
    p.snare.densityBias = -5;
    p.snare.velocityBias = +10;

    // KICK: 1 is king; 3 is common; plus modern pop variations
    p.kick.mandatorySteps = { 0 };
    p.kick.preferredSteps = { 8, 10, 14, 15, 3, 7, 11, 12 };
    p.kick.forbiddenSteps.clear();
    p.kick.densityBias = +5;
    p.kick.velocityBias = +5;

    // HI-HAT: steady 8ths, occasional 16ths for lift
    p.hiHat.mandatorySteps = { 0, 2, 4, 6, 8, 10, 12, 14 };
    p.hiHat.preferredSteps = { 1, 7, 9, 15, 3, 5, 11, 13 }; // occasional 16th texture
    p.hiHat.forbiddenSteps.clear();
    p.hiHat.densityBias = 0;
    p.hiHat.velocityBias = -5;
    p.hiHat.allowRolls = true;
    p.hiHat.allowTriplets = false;

    // OPEN HAT: occasional offbeat lift / chorus energy
    p.openHat.mandatorySteps.clear();
    p.openHat.preferredSteps = { 6, 14, 15, 2, 10 };
    p.openHat.forbiddenSteps.clear();
    p.openHat.densityBias = -35;

    // PERC: light embellishment, not constant
    p.perc.mandatorySteps.clear();
    p.perc.preferredSteps = { 3, 7, 11, 15, 5, 13 };
    p.perc.forbiddenSteps.clear();
    p.perc.densityBias = -25;

    return p;
}


DrumStyleRhythmProfile makeRockProfile()
{
    DrumStyleRhythmProfile p;

    // Rock foundation (standard groove):
    // - Kick: 1 & 3
    // - Snare: 2 & 4
    // - Hats: regular 8ths, sometimes quarters, not overly busy
    p.forceRigidGrid = false;
    p.allowSwing = false;
    p.allowTriplets = false;

    // SNARE: backbeat
    p.snare.mandatorySteps = { 4, 12 };
    p.snare.preferredSteps = { 14, 6 }; // occasional pushes
    p.snare.forbiddenSteps.clear();
    p.snare.densityBias = -5;
    p.snare.velocityBias = +15;

    // KICK: 1 & 3 + common rock variations (extra 16th pickups)
    p.kick.mandatorySteps = { 0, 8 };
    p.kick.preferredSteps = { 3, 7, 11, 15, 14, 4, 12 }; // pickups + occasional reinforcement
    p.kick.forbiddenSteps.clear();
    p.kick.densityBias = 0;
    p.kick.velocityBias = +10;

    // HI-HAT: mostly 8ths; allow occasional quarters by keeping density slightly negative
    p.hiHat.mandatorySteps = { 0, 2, 4, 6, 8, 10, 12, 14 };
    p.hiHat.preferredSteps = { 0, 4, 8, 12, 15, 7 }; // quarter anchors + occasional end lift
    p.hiHat.forbiddenSteps.clear();
    p.hiHat.densityBias = -10;
    p.hiHat.velocityBias = 0;
    p.hiHat.allowRolls = false;
    p.hiHat.allowTriplets = false;

    // OPEN HAT: occasional open accents
    p.openHat.mandatorySteps.clear();
    p.openHat.preferredSteps = { 6, 14, 15 };
    p.openHat.forbiddenSteps.clear();
    p.openHat.densityBias = -50;

    // PERC: usually minimal in rock (keep it rare)
    p.perc.mandatorySteps.clear();
    p.perc.preferredSteps = { 3, 7, 11, 15 };
    p.perc.forbiddenSteps.clear();
    p.perc.densityBias = -60;

    return p;
}


DrumStyleRhythmProfile makeWxstieProfile()
{
    DrumStyleRhythmProfile p;

    // WXSTIE GOALS (per your spec):
    // - Snares MOSTLY (not always) on 2 & 4.
    // - Hats: trap-influenced, but with lots of gaps most of the time.
    // - Chance of rolls (ascending/descending/stationary handled in generator).
    // - Allow occasional triplet-heavy behavior (generator decides how often).
    // - Kicks / open hats / percs should be allowed to vary widely (no hard forbids).

    // -------------------------
    // SNARE (mostly 2 & 4)
    // -------------------------
    // IMPORTANT: do NOT make these mandatory if you want "mostly but not always".
    // Mandatory would force them every bar.
    p.snare.mandatorySteps.clear();
    p.snare.forbiddenSteps.clear();
    p.snare.preferredSteps = { 4, 12, 14 };   // 2 & 4 + optional late accent
    p.snare.densityBias = -10;                // slightly thinner so it doesn't become "always"
    p.snare.velocityBias = +10;

    // -------------------------
    // HI-HAT (trap-influenced but gappy)
    // -------------------------
    // Prefer an 8th-grid foundation, but globally thin it so gaps happen often.
    p.hiHat.mandatorySteps.clear();
    p.hiHat.forbiddenSteps.clear();
    p.hiHat.preferredSteps = { 0, 2, 4, 6, 8, 10, 12, 14 }; // trap-ish anchors
    p.hiHat.densityBias = -45;        // THIS is what creates lots of gaps most of the time
    p.hiHat.velocityBias = 0;
    p.hiHat.allowRolls = true;
    p.hiHat.allowTriplets = true;

    // -------------------------
    // KICK (wide variance, sometimes sparse)
    // -------------------------
    p.kick.mandatorySteps.clear();
    p.kick.forbiddenSteps.clear();
    p.kick.preferredSteps = { 0, 7, 11, 14, 15 }; // flexible pockets
    p.kick.densityBias = -10;                      // slight thinning baseline
    p.kick.velocityBias = +5;
    p.kick.allowTriplets = true;

    // -------------------------
    // OPEN HAT (sometimes a lot, sometimes little -> don't restrict)
    // -------------------------
    p.openHat.mandatorySteps.clear();
    p.openHat.forbiddenSteps.clear();
    p.openHat.preferredSteps = { 2, 10, 15 };
    p.openHat.densityBias = -20;

    // -------------------------
    // PERC (free to vary)
    // -------------------------
    p.perc.mandatorySteps.clear();
    p.perc.forbiddenSteps.clear();
    p.perc.preferredSteps = { 1, 3, 6, 9, 11, 14 };
    p.perc.densityBias = -10;

    // Feel
    p.allowSwing = false;     // wxstie tends to be tighter; swing can still come from your swing slider
    p.allowTriplets = true;   // global permission; generator still decides frequency

    return p;
}



DrumStyleRhythmProfile makeHipHopProfile()
{
    DrumStyleRhythmProfile p;

    // HIP-HOP GOALS:
    // - Snare mainly on 2 & 4, with occasional late/ghost accents
    // - Kicks syncopated (not just "always on 1")
    // - Hats mostly steady 8ths, sometimes a little 16th texture
    // - Swing is allowed (pocket)

    p.forceRigidGrid = false;
    p.allowSwing = true;
    p.allowTriplets = false; // hip-hop: triplets not the default

    // -------------------------
    // SNARE: strong backbeat + optional ghosts
    // -------------------------
    p.snare.mandatorySteps = { 4, 12 };            // 2 & 4
    p.snare.forbiddenSteps.clear();
    p.snare.preferredSteps = { 6, 14, 15, 3, 11 }; // late/ghost-ish accents
    p.snare.densityBias = -10;                      // stops it from over-filling
    p.snare.velocityBias = +10;
    p.snare.allowRolls = false;
    p.snare.allowTriplets = false;

    // -------------------------
    // KICK: syncopated pockets (lots of valid options)
    // -------------------------
    p.kick.mandatorySteps.clear(); // don't hard-force; let it breathe
    p.kick.forbiddenSteps.clear();
    p.kick.preferredSteps =
    {
        0,  // downbeat
        7,  // before 3
        8,  // beat 3
        10, // after 3
        11, // late 3
        13, // after 4
        14, // late 4
        15, // pickup
        2,  // early movement
        5,  // mid movement
        12  // can sometimes reinforce 4
    };
    p.kick.densityBias = -5;     // slightly sparse by default
    p.kick.velocityBias = +5;
    p.kick.allowRolls = false;
    p.kick.allowTriplets = false;

    // -------------------------
    // HI-HAT: mostly steady 8ths, sometimes 16th texture
    // -------------------------
    // Make 8ths the base, but don't force every 8th if restDensity is high.
    p.hiHat.mandatorySteps = { 0, 2, 4, 6, 8, 10, 12, 14 };     // steady 8ths
    p.hiHat.forbiddenSteps.clear();
    p.hiHat.preferredSteps = { 1, 3, 7, 9, 11, 13, 15, 5 };     // occasional 16ths
    p.hiHat.densityBias = -10;
    p.hiHat.velocityBias = -10;
    p.hiHat.allowRolls = true;       // occasional hat rolls are allowed
    p.hiHat.allowTriplets = false;

    // -------------------------
    // OPEN HAT: sparse accents (not constant)
    // -------------------------
    p.openHat.mandatorySteps.clear();
    p.openHat.forbiddenSteps.clear();
    p.openHat.preferredSteps = { 6, 14, 15, 2, 10 }; // small lifts
    p.openHat.densityBias = -45;
    p.openHat.velocityBias = 0;

    // -------------------------
    // PERC: tasteful syncopation
    // -------------------------
    p.perc.mandatorySteps.clear();
    p.perc.forbiddenSteps.clear();
    p.perc.preferredSteps = { 3, 7, 11, 15, 5, 13, 1, 9 };
    p.perc.densityBias = -30;
    p.perc.velocityBias = -5;

    return p;
}
