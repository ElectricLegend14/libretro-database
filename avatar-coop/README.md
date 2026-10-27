# Avatar: The Last Airbender (DS) co-op

A two-player co-op mod for *Avatar: The Last Airbender* (Nintendo DS, 2006,
USA, serial `AVAE`). It's a Lua script for the BizHawk emulator.

In this game all four heroes (Aang, Sokka, Katara and Haru) fight on screen
together. You control one, and the computer controls the other three. This
mod lets Player 2 take over one of the computer-controlled heroes with a
second gamepad or their own keyboard keys. The game itself is unchanged: each
frame, the script moves Player 2's hero by writing to the game's memory.

> **Status: ready for a first real test.** Player 2 plays **Sokka**. His
> addresses were found in one level and may move in others, until his pointer
> is known (see "If the addresses stop working" below). Player 1's position
> is a fixed address that works everywhere.

## What you need

- **BizHawk 2.9 or newer** (Windows or Linux): <https://tasvideos.org/BizHawk>.
  It runs DS games with the melonDS core.
- **Your own dump of the game.** The USA version this targets is
  `Avatar - The Last Airbender (USA).nds`, with SHA-1
  `B5BDC88489AD74399E2ED968D816669E594A5F74`.
  `coop.lua` warns you if a different version is loaded.
- **A DS BIOS and firmware** set up in BizHawk, if your version asks for them.

## Files

| File | What it does |
| --- | --- |
| `coop.lua` | The mod. Run this to play. |
| `config.lua` | Memory addresses, Player 2's buttons, speed and leash. Most of your editing happens here. |
| `finder.lua` | Finds the position addresses automatically. |
| `probe.lua` | Shows live values on screen, so you can check the addresses. |
| `memory.lua` | Helpers the other scripts share. |

To run any script, open **Tools > Lua Console** in BizHawk, then
**Script > Open Script**. Stop one script before starting another.

## Setup (once)

### 1. Check that memory reading works

Load the game, get into normal play, and run `probe.lua`. The top lines show
each hero's health, which comes from this repository's known cheat codes.

- If the numbers look right (for example `Aang HP: 120 / 120`) and drop when
  you take damage, everything is working.
- If they're `0 / 0` or nonsense, you probably have a different version of the
  game. Tell Claude what you see.

### 2. Find the positions automatically

1. Get into a level and stand somewhere **open**, away from walls and enemies.
   The script needs to walk a few steps in every direction.
2. Run `finder.lua` and press **T**. Don't touch the controls. It walks your
   hero left, right, up and down by itself, three times each, which takes
   about 20 seconds.
3. It lists the candidates it found and saves them to `finder-results.txt`.
   Expect a handful: your hero, the heroes following you, and some copies
   (camera, graphics).
4. Press **N** to test each candidate. The game rewinds to where the search
   started and moves that value a few steps:
   - If a **hero jumps** to a new spot, it's a real position. Press **M** to
     mark it, and write down **which hero** jumped.
   - If **nothing visible happens**, or only the camera shifts, it's a copy.
     Skip it.
   - **B** goes back to the previous candidate.
5. Send Claude the contents of `finder-results.txt` and which hero jumped for
   each marked line. Claude will fill in `config.lua`.

If you'd like to fill it in yourself: an `x` match goes in `x`, a `depth` match
goes in `y`, and Player 1's hero goes under `p1`. Pick one hero for Player 2
and use theirs under `p2`.

### 3. If the addresses stop working after a level change

The game may keep the heroes in memory that moves between levels. Run the
finder again in the new level and send Claude both results files. Claude can
work out the pattern (a "pointer") so the addresses work everywhere.

### Finding addresses by hand (fallback)

If the finder can't find anything, use **Tools > RAM Search** with the domain
set to **Main RAM** and 4-byte signed values. Walk right, then search
**greater than** the previous value. Walk left, then search **less than**.
Stand still, then search **equal to**. Repeat until only a few are left, and
poke-test them in **RAM Watch**.

## Playing

1. Load the game, get into a level, and run `coop.lua`.
2. **Player 1: walk up, down, left and right once.** The camera angle changes
   between levels, so the script watches which way your hero goes for each
   button and steers Player 2 the same way. Until then, the screen says
   which directions it still needs.
3. Player 2 uses **I J K L** to move by default, or the D-pad on a second
   Xbox-style controller.
4. Player 1 plays as anyone except Sokka. **Don't switch to Sokka** on the
   touch screen, or you'll both be steering him.

When Player 2 lets go, the computer takes over Sokka again, like an autopilot.
The script also watches how the game turns Sokka while the computer walks
him, and uses that to make him face where Player 2 is walking. Until it has
seen enough, the screen says "learning facing" and Sokka may walk sideways.

BizHawk gives the DS only one controller port, so Player 2's buttons are read
straight from your PC's keyboard and gamepads. To use a different controller,
set `show_host_input = true` in `config.lua`. The screen will show the names
of the buttons you press; copy them into `p2_keys`.

**Playing online:** run BizHawk on one PC and have your friend join through
[Parsec](https://parsec.app). Their controller then works as a second local
gamepad.

If Player 2 moves too fast or too slow, change `p2.speed` in `config.lua`.

## Extras once the basics work

- **Attacks and bending:** find a value that changes when that hero attacks
  (often an "action state"). Add it to `actions` with a button. While that
  button is held, its value is written every frame.
- **Leash:** Player 2 is kept within `leash` of Player 1, so the game's camera
  doesn't lose them. Set it to `nil` to turn this off.
- **Three or four players:** since all four heroes are on screen, the same
  approach could work for up to four players.

## Known limits

- Movement has been tested against a simulated game at many camera angles,
  but not in the real game yet.
- Player 2 can move but can't attack until an action address is found.
- Sokka's addresses come from one level. If Player 2 stops moving in another
  level, run the finder there as Sokka and send the results.
- It only works in BizHawk, not on a real DS. Real hardware would need a ROM
  hack, which is much bigger.
