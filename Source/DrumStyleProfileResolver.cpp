#include "DrumStyleProfileResolver.h"

// include all helpers
DrumStyleRhythmProfile makeHipHopProfile();
DrumStyleRhythmProfile makeTrapProfile();
DrumStyleRhythmProfile makeDrillProfile();
DrumStyleRhythmProfile makeReggaetonProfile();
DrumStyleRhythmProfile makeEdmProfile();
DrumStyleRhythmProfile makePopProfile();
DrumStyleRhythmProfile makeRnBProfile();
DrumStyleRhythmProfile makeRockProfile();
DrumStyleRhythmProfile makeWxstieProfile();

DrumStyleRhythmProfile getDrumStyleProfile(DrumStyle style)
{
    switch (style)
    {
    case DrumStyle::HipHop:     return makeHipHopProfile();
    case DrumStyle::Trap:       return makeTrapProfile();
    case DrumStyle::Drill:      return makeDrillProfile();
    case DrumStyle::Reggaeton:  return makeReggaetonProfile();
    case DrumStyle::EDM:        return makeEdmProfile();
    case DrumStyle::Pop:        return makePopProfile();
    case DrumStyle::RnB:        return makeRnBProfile();
    case DrumStyle::Rock:       return makeRockProfile();
    case DrumStyle::Wxstie:     return makeWxstieProfile();
    }

    // This should NEVER happen, but we return hip-hop as a safety net
    return makeHipHopProfile();
}
