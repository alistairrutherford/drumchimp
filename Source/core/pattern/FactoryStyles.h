// The factory styles (PLAN.md §3.4): twenty, in the text format of Style.h. Each lane is written
// a beat at a time (4 sixteenths, or 3 triplet eighths on a 12-step grid). At Complexity 0 and
// Density 50% or more a style plays exactly its X, x and r steps: its groove as written. The
// order is the Style parameter's: new styles go at the end, so saved sets keep theirs.
#pragma once

#include <array>

namespace drumchimp
{
    constexpr std::array<const char*, 20> factoryStyles {

R"(name      = Rock
tempo     = 110
swing     = 50
families  = acoustic acoustified session bluesbreak kindified plymouth
fill      = toms
crash     = one
kick      = X... ..o. X.o. ....
snare     = ..g. X.g. ..g. X..g
closedHat = x.x. x.x. x.x. x.x.
openHat   = .... .... .... ..o.
)",

R"(name      = Pop
tempo     = 100
swing     = 50
families  = acoustified kindified session acoustic powered
fill      = snare
crash     = one
kick      = X... ..x. X... ..o.
snare     = .... X... .... X...
clap      = .... o... .... o...
closedHat = x.x. x.xo x.x. x.xo
openHat   = .... .... .... ..o.
perc      = .... x... .... x...
)",

R"(name      = Funk
tempo     = 100
swing     = 56
families  = vinyl dmx sp kindified vintage
fill      = snare
crash     = fills
kick      = X... ..x. ..x. .o..
snare     = .... X..g .g.. X..g
closedHat = xxox xxox xxox xxox
openHat   = .... ...o .... ...o
extras    = perc
perc      = .... .... .... ....
)",

R"(name      = Hip-Hop
tempo     = 90
swing     = 58
families  = vinyl sp dmx mpc chop
fill      = none
crash     = fills
kick      = X... ...x .xo. ....
snare     = .... X... .... X...
closedHat = x.x. x.x. x.x. x..o
openHat   = .... .... .... ..o.
perc      = .... .... ...g ....
)",

R"(name      = Trap
tempo     = 140
swing     = 50
families  = 808
fill      = snare
crash     = fills
kick      = X... ...x ..o. ..o.
snare     = .... .... X... ....
clap      = .... .... X... ....
closedHat = x.xr x.x. x.xr x.r.
openHat   = .... .... .... ...o
perc      = .... o... .... o...
)",

R"(name      = House
tempo     = 124
swing     = 54
families  = 909 808 707
fill      = snare
crash     = one
extras    = closedHat perc
kick      = X... X... X... X...
clap      = .... X... .... X...
snare     = .... .... .... o..o
closedHat = xox. xox. xox. xox.
openHat   = ..X. ..X. ..X. ..X.
perc      = ...o ..o. ..o. .o..
)",

R"(name      = Techno
tempo     = 130
swing     = 50
families  = 909 808 machine sp
fill      = toms
crash     = fills
extras    = closedHat perc
kick      = X... X... X... X...
clap      = .... x... .... x...
closedHat = o.x. o.x. o.x. o.x.
openHat   = .... .... .... ..o.
perc      = .o.. .o.g ..o. .o..
tom       = .... .... ...o ....
)",

R"(name      = Drum & Bass
tempo     = 174
swing     = 50
families  = vinyl sp jungle dmx
fill      = snare
crash     = one
kick      = X... .... ..x. ...o
snare     = .... X..g .... X..g
closedHat = xoxo xoxo xoxo xoxo
openHat   = .... ..o. .... ....
)",

R"(name      = Breakbeat
tempo     = 130
swing     = 52
families  = vinyl sp dmx chop
fill      = snare
crash     = fills
kick      = X.x. .... ..xo ....
snare     = .... X..x .x.. X..x
closedHat = x.x. x.x. x.x. x.x.
openHat   = .... .... .... ..o.
)",

R"(name      = Reggae
tempo     = 75
swing     = 62
families  = vinyl dub sp acoustic
fill      = toms
crash     = fills
kick      = .... .... X... ....
rim       = .... .... X... ....
closedHat = x.x. x.x. x.x. x.xo
perc      = ..o. ..o. ..o. ..o.
)",

R"(name      = Disco
tempo     = 120
swing     = 50
families  = 707 909 dmx vinyl acoustic
fill      = toms
crash     = one
kick      = X... X... X... X...
snare     = .... X... .... X...
clap      = .... o... .... o...
closedHat = x..o x..o x..o x..o
openHat   = ..X. ..X. ..X. ..X.
perc      = .o.. .o.. .o.. .o..
)",

R"(name      = Shuffle
tempo     = 96
grid      = 12
swing     = 50
families  = acoustic session bluesbreak vintage
fill      = toms
crash     = one
kick      = X.. ... X.o ...
snare     = ... X.. ..g X..
closedHat = x.x x.x x.x x.x
openHat   = ... ... ... ..o
)",

R"(name      = Jazz
tempo     = 140
grid      = 12
swing     = 50
families  = acoustic session brush jazz
fill      = snare
crash     = fills
ride      = X.. X.x X.. X.x
closedHat = ... x.. ... x..
kick      = g.. ... g.. ..o
snare     = ..g ..o ... .og
)",

R"(name      = Bossa Nova
tempo     = 120
swing     = 50
families  = acoustic session vinyl
fill      = none
crash     = fills
kick      = X..x X..x X..x X..x
rim       = x..x ..x. ..x. .x.. | ..x. .x.. ..x. ....
closedHat = x.x. x.x. x.x. x.x.
)",

R"(name      = Afrobeat
tempo     = 110
swing     = 52
families  = vinyl acoustic sp dmx
fill      = snare
crash     = fills
kick      = X... ..x. ..x. .o..
snare     = .... x..g .g.. x...
closedHat = x.xx x.xx x.xx x.xx
openHat   = .... ..o. .... ..o.
perc      = x.x. x.x. x.x. x.x.
)",

R"(name      = 2-Step
tempo     = 132
swing     = 60
families  = 909 808 sp
fill      = none
crash     = fills
kick      = X... .... ..x. ...o
snare     = .... X... .... X...
closedHat = ..x. ..x. ..x. ..xo
perc      = .x.. ...x .x.. ...x
)",

R"(name      = Dembow
tempo     = 100
swing     = 50
families  = 808 909
fill      = snare
crash     = fills
kick      = X... X... X... X...
snare     = ...x ..x. ...x ..x.
closedHat = x.x. x.x. x.x. x.x.
perc      = ..o. ..o. ..o. ..o.
)",

R"(name      = Dubstep
tempo     = 140
swing     = 50
families  = 808 909 sp
fill      = snare
crash     = one
kick      = X... .... ..o. ....
snare     = .... .... X... ....
closedHat = x.x. x.xr x.x. x.r.
openHat   = .... .... .... ..o.
tom       = .... .... .... ...o
)",

R"(name      = Motown
tempo     = 104
swing     = 50
families  = vinyl acoustic session dmx
fill      = snare
crash     = one
kick      = X... ..x. X... ..x.
snare     = .... X... .... X...
perc      = X... X... X... X...
closedHat = x.x. x.x. x.x. x.x.
)",

R"(name      = Punk
tempo     = 180
swing     = 50
families  = acoustic acoustified session powered
fill      = snare
crash     = one
kick      = X... ..x. X.x. ....
snare     = .... X... .... X...
ride      = x.x. x.x. x.x. x.x.
)",

    };
}
