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
- Hits within 1 of either end play the bat's end animations.

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
0). While 5 or fewer are left, the game also drops a random prize every 9 to
14 seconds.

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

The grid changes at once; the drawn brick follows, falling 0.003 units a
ms. Bricks at y > 0.5 are out of the ball's reach, so a level can only be
finished because upper bricks come down. Glass (invisible) bricks hold
others up like any brick; 028_Taxi stands its car on them.

## Lives and score

A new game starts with 3 lives and score 0. Losing the last ball loses a
life; at below 0 the game ends. Prizes can give lives, up to 9. Finishing a
level does not give a life: in a recording of the original, Lives shows 2
on level 48 and still 2 on level 49 after finishing it. (The same recording
shows Lives 2 with no lives lost, so the HUD may count spare lives; not yet
checked.)
