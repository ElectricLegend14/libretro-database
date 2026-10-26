# FE3H Co-op

A shared-screen co-op mod for *Fire Emblem: Three Houses*. Each player commands
their own units during the player phase, with their own color and controller.

## Player settings

Settings live in `sdmc:/config/fe3h-coop/coop.ini`. Start from
[`config/coop.ini`](config/coop.ini).

```ini
[coop]
players = 2                 ; 2-4
unclaimed_units = shared    ; shared | host | locked
in_game_claiming = true     ; ZR on the battle prep screen claims/releases a unit
tint_unit_outlines = true

[player1]
name = Alex
color = lion                ; #RRGGBB or a preset
controller = 1
characters = Byleth, Dimitri, Dedue, Mercedes

[player2]
name = Sam
color = #F97316
controller = 2              ; same number as player1 = pass-the-controller hot-seat
characters = Felix, Sylvain, Ingrid, Ashe
```

**Characters:** use roster names (case-insensitive), or `#<id>` for a raw game
character id. A character can belong to only one player. If two players list
the same character, the first player keeps it and you get a warning.

**Colors:** `blue red green yellow purple orange pink cyan white`, the house
colors `eagle lion deer`, or any `#RRGGBB`. The active player's color tints the
cursor and the turn banner. Each unit's outline is drawn in its owner's color.
If two players pick colors that are hard to tell apart, you get a warning.

**In-game claiming:** on the battle prep screen, the player holding a
controller presses ZR on a unit to claim it. Pressing ZR again releases it.
You can't take a unit another player already owns. Claims are written back
to `coop.ini`.

Mistakes in the file never crash the game. Unknown names or colors are skipped
with a warning. An invalid player count falls back to the defaults.

## Layout

| Path | What |
| --- | --- |
| `include/coop/`, `src/*.cpp` | Platform-independent core: config parsing, roster, ownership and turn rules |
| `src/switch/hooks.cpp` | exlaunch glue for the game (hook offsets still **TODO**) |
| `tests/` | Desktop tests for the core |

## Building and testing the core

```sh
cmake -S . -B build && cmake --build build && ./build/coop_tests
```

## Still to do in the game binary

The hooks in `src/switch/hooks.cpp` need offsets found by reverse engineering
(Ghidra or IDA on the game's `main` NSO):

1. Unit struct → character id, then fill in `Character::gameId` in `src/roster.cpp`.
2. The "can select unit" check used during the player phase.
3. Battle pad input reading, to filter out controllers that aren't the active player's.
4. Battle prep unit list: ZR → `toggleClaim`.
5. Player-phase end: cycle through the players before handing over to the enemy phase.
6. Cursor, outline and banner draw calls, to apply the colors.

Only use this with a copy of the game you own.
