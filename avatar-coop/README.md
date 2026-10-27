# Avatar: The Last Airbender (DS) co-op

A two-player co-op mod for *Avatar: The Last Airbender* (Nintendo DS, 2006,
USA, serial `AVAE`). It's a Lua script for the BizHawk emulator.

In this game all four heroes (Aang, Sokka, Katara and Haru) fight on screen
together. You control one, and the computer controls the other three. This
mod lets Player 2 take over any of the other heroes with a second gamepad or
their own keyboard keys, switch between them, and turn each hero's computer
control (AI) on or off. The game itself is unchanged: each frame, the script
moves Player 2's hero by writing to the game's memory.

> **Status:** Player 2 can move Aang or Sokka (tested in the real game with
> Sokka). Katara and Haru need a finder run each once you have them.
> Player 2's A/B/X/Y need the button search (setup step 4). Hero addresses
> were found in one level and may move in others, until their pointers are
> known. Player 1's position is a fixed address that works everywhere.

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
| `config.lua` | Hero addresses, Player 2's keys, speed and leash. Most of your editing happens here. |
| `finder.lua` | Finds hero positions and button values automatically. |
| `probe.lua` | Shows live values on screen, so you can check the addresses. |
| `memory.lua` | Helpers the other scripts share. |

To run any script, open **Tools > Lua Console** in BizHawk, then
**Script > Open Script**. Stop one script before starting another.

## Playing

1. Load the game, get into a level, and run `coop.lua`.
2. **Player 1: walk up, down, left and right once.** The camera angle changes
   between levels, so the script watches which way your hero goes for each
   button and steers Player 2 the same way. Until then, the screen says
   which directions it still needs.
3. Player 2's controls:

   | Action | Keyboard | Second Xbox-style controller |
   | --- | --- | --- |
   | Move | **I J K L** | D-pad |
   | A / B / X / Y | **O / U / P / Y** | A / B / X / Y |
   | Switch hero | **H** | Right shoulder |

4. Either player can turn a hero's **AI on or off** with **1, 2, 3, 4** (in the
   order Aang, Sokka, Katara, Haru).

The top-left of the screen shows who's who, for example
`1 Aang (P1) AI on | 2 Sokka (P2) AI off | 3 Katara AI on`.

- **Switching heroes:** Player 2 can't pick Player 1's hero. If Player 1
  switches to Player 2's hero on the touch screen, Player 2 moves to the next
  hero automatically.
- **AI on:** when Player 2 lets go, the computer takes over their hero again,
  like an autopilot.
- **AI off:** the hero stays where they are unless Player 2 is moving them.
  Turn this off for Player 2's hero so it only moves when they do.
- **Facing:** the script watches how the game turns heroes while the computer
  walks them, and uses that to make Player 2's hero face where they walk.
  Until it has seen enough, the screen says "learning facing".
- **Speed:** Player 2 moves at the same speed as Player 1's hero.

BizHawk gives the DS only one controller port, so Player 2's buttons are read
straight from your PC's keyboard and gamepads. If a key doesn't work (for
example, the number keys or your controller's buttons have other names), set
`show_host_input = true` in `config.lua`. The screen will show the names of
the buttons you press; copy them into `config.lua`.

**Playing online:** run BizHawk on one PC and have your friend join through
[Parsec](https://parsec.app). Their controller then works as a second local
gamepad.

## Setup

### 1. Check that memory reading works

Load the game, get into normal play, and run `probe.lua`. The top lines show
each hero's health, which comes from this repository's known cheat codes.

- If the numbers look right (for example `Aang HP: 130 / 130`) and drop when
  you take damage, everything is working.
- If they're `0 / 0` or nonsense, you probably have a different version of the
  game. Tell Claude what you see.

### 2. Find a hero's position automatically

Do this once for each hero, while **controlling that hero** (tap their icon
on the touch screen).

1. Stand somewhere **open**, away from walls and enemies. The script needs to
   walk a few steps in every direction.
2. Run `finder.lua` and press **T**. Don't touch the controls. It walks your
   hero left, right, up and down by itself, three times each, which takes
   about 20 seconds.
3. It lists the candidates it found and saves them to `finder-results.txt`.
   Expect a handful: your hero and some copies (camera, graphics).
4. Press **N** to test each candidate. The game rewinds to where the search
   started and moves that value a few steps:
   - If a **hero jumps** to a new spot, it's a real position. Press **M** to
     mark it. Marking also saves pointers to it, which takes a second.
   - If **nothing visible happens**, or only the camera shifts, it's a copy.
     Skip it.
   - **B** goes back to the previous candidate.
5. Rename `finder-results.txt` after the hero (the next run overwrites it),
   and send it to Claude.

To fill it in yourself, put the marked `x` address in that hero's `at` line
in `config.lua`.

### 3. If the addresses stop working after a level change

The game may keep the heroes in memory that moves between levels. Run the
finder again in the new level and send Claude both results files. Claude can
work out the pattern (a "pointer") so the addresses work everywhere.

### 4. Find the buttons (for Player 2's A/B/X/Y)

Many games steer computer heroes through a "virtual controller" stored with
each hero. If this game does, writing Player 2's buttons there gives them
real attacks and bending, and lets "AI off" switch the computer off properly.

1. Stand somewhere safe, with no enemies nearby.
2. Run `finder.lua` and press **P**. Don't touch the controls. It presses A,
   B, X and Y twice each by itself (your hero will attack the air), which
   takes about 20 seconds, then rewinds.
3. Send Claude `finder-results.txt`. Its "Buttons" section lists values that
   followed the buttons, and "Controlled hero's data" says where your hero
   is, so Claude can tell which value belongs to the hero.

### 5. Label search (which hero you're controlling)

The game must remember which hero you're controlling, and which ones the
computer controls. Finding that is the next step toward Player 2 attacks and
a real "AI off". It also finds each hero's data, and pointers to it, without
the walking search.

1. Run `finder.lua` in a level with at least two heroes.
2. Control **Aang**, stand still, and press **1**.
3. Tap **Sokka** on the touch screen, stand still, and press **2**.
4. Repeat steps 2–3 at least **three times** (more is better; also use **3**
   for Katara and **4** for Haru if you have them). Move around a bit between
   snapshots so only the important values stay the same. The status line
   shows how many values are left.
5. Send Claude `finder-results.txt`.

### Finding addresses by hand (fallback)

If the finder can't find anything, use **Tools > RAM Search** with the domain
set to **Main RAM** and 4-byte signed values. Walk right, then search
**greater than** the previous value. Walk left, then search **less than**.
Stand still, then search **equal to**. Repeat until only a few are left, and
poke-test them in **RAM Watch**.

## Settings

- **Leash:** Player 2 is kept within `leash` of Player 1, so the game's camera
  doesn't lose them. Set it to `nil` to turn this off.
- **Actions:** if you find a value that makes a hero attack (an "action
  state"), add it to `actions` with a button. While that button is held, its
  value is written every frame.

## Known limits

- Until the button search finds a hero controller, Player 2 can move but not
  attack. "AI off" also can only hold a hero in place; they may still attack
  where they stand.
- While Player 2 moves a hero whose AI is on, the AI keeps walking it too. The
  script ignores the AI's moves along Player 2's direction, but moves against
  it look the same as being pushed out of a wall, so they can slow Player 2
  down. A hero controller from the button search would fix this.
- Hero addresses come from one level. If a hero stops moving in another
  level, run the finder there as that hero and send the results.
- It only works in BizHawk, not on a real DS. Real hardware would need a ROM
  hack, which is much bigger.
