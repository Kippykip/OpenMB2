# Magic Ball 2 gameplay rules

What the original does each frame, worked out from its exe one function at a
time. Addresses are in the Big Fish 2005 `MagicBall2.exe` and are only there
to find the code again; no code was copied.

## Timing

The main loop runs a frame only when more than 17 ms have passed since the
last one (so at most about 55 frames a second). Every update gets the elapsed
milliseconds `ms` and `dt = ms * 0.01`. Speeds below are in units per `dt`,
so a speed of 1 moves 10 units a second.

Order in a frame (0x41c9b0): read input, update the bat, update every ball
(move, then the brick in the ball's cell), update bullets and prizes, draw,
then check balls against the board walls and the bat (0x40e600).

## Board (0x406b80, 0x407070)

Board files are read top to bottom, so `scalex:` multiplies the `halfwidth`
and `radius` read before it, and `scalez:` multiplies `halfheight` and
`batline`. Defaults: halfwidth 10, halfheight 10, radius 16. The bat sits at
z = batline.

Ball against the walls, by shape:

- **rect**: x > halfwidth sends the ball left (vx = -|vx|), x < -halfwidth
  sends it right, z > halfheight sends it back (vz = -|vz|). There is no wall
  behind the bat.
- **round**: a circle of `radius` around (0, 0, 7.5 * scalez). Outside it the
  ball goes back to where it was last frame and its velocity is mirrored
  about the circle's normal.
- **compaund**: below z = halfheight + 0.25 the rect side walls; above it the
  round rule with the circle at z = 12.5 * scalez. After more than 3 bounces
  in a row there, the ball is also pulled 5% towards the circle's centre so
  it can't get stuck.

Every wall bounce plays `ball1.ogg`, panned by x / halfwidth * 100. After a
bounce the speed's z part is kept at least 0.15 (0x401f90).

When the ball is within 1.3 of the bat line it is checked against the bat;
if it misses and is behind the bat line, the ball is lost.

## Ball (0x402480, update 0x402060)

- Model `s_ball.x`, collision radius 0.8, flies at y = -0.5.
- New balls start stuck to the bat (flag 2), 1.35 in front of it. A left
  click releases every stuck ball. Starting velocity is
  (-0.35, 0, 0.65) * scalex.
- Each frame: position += velocity * dt, then the brick in the ball's grid
  cell gets a chance to bounce it (see Bricks), then creatures and obstacles.
- Speed after a bat hit is clamped to [0.85 * scalex, 2 * scalex].
- Flags: 0x02 stuck to the bat, 0x04 magnet, 0x08 waiting, 0x10 power ball
  (goes through bricks, bigger), 0x20 explosive, 0x40 hypnotic (wobbles),
  0x8000 lost.

## Bat (0x404020, move 0x4042f0, ball 0x405450)

- Half width 3. The mouse moves it by dx * 0.18 * msensivity (params.txt,
  default 0.75), kept inside ±(board halfwidth - half width).
- Moving tilts the model: tilt -= dx * 70, kept within ±299, and it eases
  back by 3 each frame.
- A ball between bat x ± half width bounces off at angle
  (ball.x - bat.x) / halfwidth * 60° from straight ahead, its speed goes up by
  0.01 (then clamped as above), and `ball2.ogg` plays.
- Every hit plays a bounce on the model (0x4054f3): Plashka, L_Hand, R_Hand,
  L_Ressore and R_Ressore are set to a time that runs at a third of real
  time (0x404f4f): 1-150 for a middle hit, 1000-1150 within 1 of the right
  end, 1150-1300 within 1 of the left end, then back to 0.

## Bricks (0x412180 factory, 0x4036b0 base)

The level grid has cells 1 wide in x, 2 high and 2 deep: cell = (floor(x) +
17, int((y + 0.9) / 2), floor(z / 2)), 34 × 15 × 18 cells. A brick is 2
units on each side and fills two cells in x. Record y is always even and z
always odd.

Ball against a brick (0x403110), only for bricks at y <= 0.5 (the bottom
layer): it checks whether the ball crossed one of the brick's faces since
last frame (z = bz ± 1, x = bx - 0.99 or bx + 1.01) and flips that velocity
part. If the brick's hit rule says bounce, the ball goes back to last
frame's position with the new velocity.

Hit rules (a power ball always breaks a breakable brick and keeps going; an
explosive ball blows it up):

| type | brick | score | hits | sounds | counts for level end |
| --- | --- | --- | --- | --- | --- |
| 0x00 | normal | 500 | 1 | Normal.ogg | yes |
| 0x10 | colour changing | 1000 | 1 | Normal.ogg | yes |
| 0x18 | glass, invisible | 700 | 2 | Glass1.ogg, then Glass2.ogg | no |
| 0x20 | explosive | 800 | 1 | Explosive.ogg, blows up its neighbours | yes |
| 0x28 | strong | (none) | 4 | Strong1.ogg, then Strong2.ogg | yes |
| 0x30 | stone | 1200 | 6 | Stone.ogg | no |

The score is the brick's points value × 100. Break sounds play at volume
100 - |2z| and pan x / halfwidth * 100.

An explosion breaks the bricks next to the one it hits: left and right, and
the ones above, below, in front and behind.

The level is finished when the counted bricks are all gone (0x49fb50 reaches
0). While 5 or fewer are left, a next-level medal comes out of (0, 0, 10):
at once, then every 9 to 14 seconds (0x4197fa).

## Falling bricks (0x402bb0, support 0x40de00, movement 0x402e40)

Every frame, each brick that is not dying and not sliding sideways looks
at the cells around it (x is its left cell, y its bottom layer):

| bit | cell |
| --- | --- |
| 0x400 | y < 1: on the floor, nothing more to do |
| 0x02, 0x01 | (x, y-1), (x+1, y-1): under it |
| 0x08, 0x04 | (x-1, y-1), (x-1, y): to the left |
| 0x20, 0x10 | (x+2, y-1), (x+2, y): to the right |
| 0x40, 0x80 | (x, y+1), (x+1, y+1): on top of it |
| 0x100, 0x200 | at the grid's left / right edge |

- Nothing under it (bits & 3 == 0): it falls a layer (y - 2). A tall brick
  moves all its layers.
- A one-layer brick held only under its right half, with the left side
  clear (bits & 0xe == 0), tips off to the left: one cell left and a layer
  down, turning 90 degrees. It doesn't if only its right half has something
  on top (bits & 0xc0 == 0x80).
- The same the other way round: held only under its left half, right side
  clear (bits & 0x31 == 0), unless only its left half is loaded (0x40).

The grid changes at once; the drawn brick follows (0x402e40). A falling
brick drops 0.003 units a ms. A tipping brick first turns 0.005 radians a
ms about the middle of its bottom edge, (x, y - 1, z), until it has turned
90 degrees; then it steps one unit across and one down and falls the last
unit. A brick isn't checked again until it has caught up: same x as its
target and within 0.5 of its target y (0x402750). Bricks at y > 0.5 are out of the ball's reach, so a level can only be
finished because upper bricks come down. Glass (invisible) bricks hold
others up like any brick; 028_Taxi stands its car on them.

## Bat model (draw 0x404d62)

pad.x holds every look of the bat. The plain bat draws the frames Corpus
(with its turbines and fans), L_Hand and R_Hand (with the springs) and
Plashka, the plate. With the magnet (mode 2) Magnit is drawn instead of
Plashka. The gun (mode 4) adds L/R_Pulemet, L/R_Priklad and L/R_Wing; the
bazooka (8) L/R_Stvol, L/R_Derjatel and the wings; the cannon (0x10)
For_Pushka and Pushka. The plate (or magnet) is stretched in x by the bat's
half width / 3; the hands only when that is below 1, so a wide bat's plate
reaches past its hands. With the magnet the hands, springs and magnet stay
at time 0 instead of bouncing.

## Drawing order (0x410d70)

The original draws everything texture by texture, in the order the
texture set registers them, setting each texture's states as it goes
(alpha: blending on; zdisable: no depth writes, the depth test stays).
Glass is tileset 4, so bricks from tilesets 5 and 6 drawn later cover
glass in front of them, while tilesets 1 to 3 show through it.

## Music (player 0x425360)

All the tunes are in one module, Music/mb01.mo3, played with BASS. Each
Music/*.txt names the module and where its tune starts (`order:`, `row:`);
the tune then loops by itself through the pattern jumps in the module.
mb0 to mb3 are the level tunes; menu, enter and intro the others. A level
starts a random one of the four (rand() % 4), and F6 moves on to the next
(0x41bfe8). The volume is params.txt's musicvolume. OpenMB2 plays the
module with libopenmpt.

## Lives and score

A new game starts with 3 lives and score 0. Losing the last ball loses a
life; at below 0 the game ends. Prizes can give lives, up to 9. Finishing a
level does not give a life: in a recording of the original, Lives shows 2
on level 48 and still 2 on level 49 after finishing it. (The same recording
shows Lives 2 with no lives lost, so the HUD may count spare lives; not yet
checked.)

## Breaking bricks

A broken brick (0x403d20) is marked dying for 2000 ms. Each frame it shrinks
by a factor of (1 - 0.5*dt) about its centre, dt = ms*0.01 (0x402e66). It
spawns an explosion (0x405fb0): a billboard of half size 2 playing the 4x4
sheet explose.png at 75 ms a frame, and a group of 5 crumbs (0x425bd0).
Each crumb starts with velocity ((r*0.02-1)*0.35, (r*0.01+0.5)*0.35,
(r*0.02-1)*0.35), r = rand()%100, falls at 0.1*dt, bounces off y = -0.75
keeping 75% while the bounce is above 0.05, spins about a random axis and
shrinks by 0.98 a frame (0x425a00). OpenMB2 skips the crumbs' wall test.

## Animated models

Models in SkinMeshes carry AnimationSets of matrix keys, about one a
millisecond. Setting a frame's time (0x421a90 by name, 0x421090) poses it
and all its children; the time loops at the last key, and a frame that was
never set keeps its FrameTransformMatrix. Objects set their whole model to
their age in ms (0x401960), so the ball spins on its own 1000 ms loop.

The bat (0x40514d) sets R_Fan and L_Fan to its clock every frame. While its
tilt is beyond ±5 it also sets L_Turbine and R_Turbine to |tilt| + 200
(tilt < 0) or |tilt| + 600, which leans them; otherwise they keep their
last pose. The code sets the turbines after the fans, which would freeze
the fans while leaning, but in the original they keep spinning, so OpenMB2
poses the turbines first.

The ball is turned about y by pi - atan2(vz, vx) before its spin is applied
(0x402309), so it rolls the way it moves.

## Shadows and infravision

Every ball has a shadow (0x40ec70): bshadow.png on a square of half size
1.5 lying at y = -1 under it, blended, without depth writes. With
infravision on in params.txt, infravis.png is also drawn facing the camera
at the ball's centre, half size 0.25, with depth testing off (0x4111b0), so
the ball shows through bricks.

## Cracks

A strong brick (0x28) that survives a hit moves its texture coordinates
down by its texture's yfactor (0x4033c0), so the sheet's next row shows
the next crack. Stone (0x30) bricks show nothing.

## Losing the bat (0x4043ff, pieces 0x4048d8, drawing 0x404cb6)

When the last ball is lost the bat blows up: batkilled.ogg plays, ten
explosions start at whole-number positions (x + random(-halfwidth,
halfwidth), y, z + random(-1, 1)), and ten frames of the model (Corpus,
Plashka, L/R_Ressore, L/R_Hand, L/R_Turbine, L/R_Fan) fly off as pieces.
Each starts at the bat with velocity (random(-0.1, 0.1), random(0.3, 0.6),
random(-0.1, 0)), an axis (random(-0.3, 0.3), random(0.3, 0.6),
random(-0.3, 0.3)) and spin random(0.3, 0.6). Every frame: velocity.y -=
0.001 * ms, position += velocity, angle += ms * spin * 0.0025. A piece is
drawn as rotation about its axis, then its offset, then the bat's matrix,
blended at (time left) / 1500 for 1.5 s. random(a, b) here is
a + (rand() % 10000) * 0.0001 * (b - a).

## Prizes (roll 0x411890, prize 0x41f800, update 0x41f910, catch 0x404730, apply 0x41fae0)

Every brick taken may drop a prize; each brick's byte 17 names the kind, and
every level uses 0xff, which means roll for one. No prize comes within a
second of the last one. If more than 3 s have passed since then the "last
kind" is forgotten, and r = rand() % 8000, or % 4000 after 5 s. The kind is
picked by r strictly between these bounds (anything else drops nothing):

| r | prize | r | prize |
| --- | --- | --- | --- |
| 0-80 | incbat | 2000-2080 | speedup |
| 100-170 | decbat | 2100-2170 | speeddown |
| 200-260 | topowerfull | 2200-2260 | trippleball |
| 300-360 | tonormal | 2900-2960 | rubber |
| 400-450 | tosimple | 3000-3080 | gun |
| 500-570 | toexplosive | 3100-3170 | bazuka |
| 600-670 | togipnoz | 3200-3260 | cannon |
| 700-710 | killbat | 3300-3370 | magnit |
| 800-805 | life | | |

The same kind never comes twice running, and none drops while 4 are
falling. Each kind's file is Prizes/<kind>.txt: score, sound, grdata (the
model), glowsize, glowoffset and messagecolor (the glow's tint).

A prize starts with velocity (0, 0.75, -0.65), or (0, 0.5, -0.45) if it is
less than 5 in front of the bat, times random(0.85, 1.15). Each step (dt =
ms * 0.01) it moves by velocity * dt and falls by 0.1 * dt. On landing (y <=
-0.5) it is put at -0.49; while its bounce velocity, starting at (0, 0.75,
-0.65), is at least 0.1 up, that shrinks by 0.75 and becomes the velocity,
with z at most -0.25; after that it slides. Behind the bat it shrinks by
0.2 * dt and its glow by (1 - 0.3 * dt), and is gone below 0.25. The model
plays its animation from its age, drawn scaled at its position, with a
white shadow of half size 1 and an infravis.png glow.

The bat catches a prize between 3 behind and 1 in front of its line, within
its half width; one more than 3 behind is gone. Not while the bat is dead.
Catching plays the sound panned by (int)(x / board half width) * 100, adds
the score, then:

- incbat / decbat: the bat's half width target +-0.5, within [2, 4.5]; the
  width moves to it at 0.1 * dt.
- topowerfull / tonormal: every ball's power flag 0x10 on or off. A
  powerball is drawn full size (not 0.8), its shadow half size 2, and every
  25 ms it leaves a 250 ms spot of its colour (0x401990), scattered by
  random(-0.2, 0.2) on each axis when also explosive. A spot is a camera
  facing triangle of particle.png, (0, 1.25), (1.25, -0.625), (-1.25,
  -0.625), with alpha = ms left.
- toexplosive / tosimple: explosive flag 0x20 on (clearing hypnotic 0x40,
  red, s_explosive.x) or both off (blue, s_ball.x).
- togipnoz: hypnotic, yellow, s_gipnoz.x, for 15 s.
- speedup / speeddown: every ball's speed +-0.25 within [0.85, 2] x scalex.
- trippleball: each ball adds two more at 120 degrees either side, same
  speed and powers.
- magnit, gun, bazuka, cannon, rubber: the bat's mode 2, 4 (18 shots), 8
  (8), 0x10 (5), 0x20 (30 s). With the magnet a ball that hits the bat
  sticks where it hit (magnited.ogg) and still gets its bounce, so a click
  sends it off at that angle. The magnet never runs out; another mode lets
  its balls go.
- killbat: the bat blows up as when the last ball is lost; the balls fall
  through.
- life: a life, up to 9.
- nextlevel: the level is finished.
