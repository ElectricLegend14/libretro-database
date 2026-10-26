# FE3H Co-op

A shared-screen co-op mod for *Fire Emblem: Three Houses*. Each player commands
their own units during the player phase, with their own color and controller.

Only use this with a copy of the game you own.

## What works today

| Feature | Status |
| --- | --- |
| Settings file, picker page, `coop_check` | Done and tested |
| **Controller relay**: the active player's controller drives the game, ZL + ZR + Plus passes the turn | Builds in CI, **not yet tried in the game** |
| Players can only pick up their own units | Needs `can_select_unit` + `unit_character_id` (or `[units] character_id_field`) in offsets.ini |
| End Turn hands over to the next player | Needs `begin_player_phase` + `end_player_phase` |
| Character names instead of `#id`s | Needs the ids mapped ([docs/REVERSING.md](docs/REVERSING.md), step 6) |
| Claiming during battle prep, colored cursor and rings | Hooks not written yet |

## Quick start

1. **Check your settings on your PC**

   ```sh
   cmake -S . -B build && cmake --build build
   ./build/coop_tests
   ./build/coop_check config/coop.ini config/offsets.ini
   ```

   Edit `config/coop.ini` by hand, or use the picker page (`tools/picker.html`).

2. **Get the Switch module.** The easiest way: open the latest *FE3H co-op mod*
   run under the repo's **Actions** tab and download the `fe3h-coop` artifact.
   It's already laid out for Ryujinx and for the SD card (see `INSTALL.txt` inside).

   To build it yourself instead, install [devkitPro](https://devkitpro.org/wiki/Getting_Started)
   (devkitA64 + libnx), then:

   ```sh
   ./switch/build.sh
   ```

   This downloads exlaunch, adds the mod's code, and builds
   `switch/exlaunch/deploy/subsdk9` and `main.npdm`. To build and copy straight
   into Ryujinx in one step:
   `RYU_PATH="$HOME/.config/Ryujinx" ./switch/build.sh deploy-ryu`.

3. **Install**

   | | Mod files | Settings |
   | --- | --- | --- |
   | Ryujinx | `<Ryujinx>/mods/contents/010055D009F78000/fe3h-coop/exefs/` | `<Ryujinx>/sdcard/config/fe3h-coop/` |
   | Switch (Atmosphère) | `atmosphere/contents/010055D009F78000/exefs/` | `config/fe3h-coop/` on the SD card |

   The settings folder holds `coop.ini` and `offsets.ini`.

4. **Play.** Only the active player's controller works. Hold **ZL + ZR** and
   press **Plus** to pass the turn. The emulator log shows whose turn it is.
   [docs/REVERSING.md](docs/REVERSING.md) step 1 has what to expect and what
   to do if it doesn't work.

If the game won't start with the mod's `main.npdm`, try copying only `subsdk9`.
The controller relay still works that way, but on a real Switch the mod may not
be able to read the settings files, so it uses the defaults: two players, on
controllers 1 and 2.

## Player settings

Settings live in `coop.ini`. Start from [`config/coop.ini`](config/coop.ini).

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
colors `eagle lion deer`, or any `#RRGGBB`. The active player's color will tint the
cursor and the turn banner, and each unit's outline will be drawn in its owner's color.
If two players pick colors that are hard to tell apart, you get a warning.

**Turns:** each player phase is split into one turn per player who has a unit
to move, in player order. Players who share a controller pass it around.

**In-game claiming:** on the battle prep screen, the player holding a
controller will press ZR on a unit to claim it, and press it again to release it.
You can't take a unit another player already owns. Claims are written back
to `coop.ini`.

**Picker page:** open `tools/picker.html` in a browser to choose colors,
controllers and units by clicking, then copy the finished `coop.ini`. It can
also load an existing `coop.ini` so you can edit it.

Mistakes in the file never crash the game. Unknown names or colors are skipped
with a warning. An invalid player count falls back to the defaults.

## Game functions (offsets.ini)

Anything beyond the controller relay hooks the game's own functions. Their
locations go in `offsets.ini` as offsets or byte patterns, so you can try what
you find in Ghidra without rebuilding. `coop_check` can test each pattern against
the game's real code before you boot:

```sh
./build/coop_check config/coop.ini config/offsets.ini path/to/exefs/main
```

[docs/REVERSING.md](docs/REVERSING.md) walks through finding each one.

## Layout

| Path | What |
| --- | --- |
| `include/coop/`, `src/` | Platform-independent core: settings, roster, turn rules, input routing, pattern scanning, NSO reading |
| `switch/main.cpp` | The exlaunch module that runs inside the game |
| `switch/build.sh` | Fetches exlaunch, adds the mod's code and builds it |
| `config/` | Example `coop.ini` and `offsets.ini` |
| `tools/coop_check.cpp` | Checks the settings files, and offsets against the game's `main` |
| `tools/picker.html` | Click-to-pick page that writes `coop.ini` |
| `tests/` | Desktop tests (`tests/data/make_sample_nso.py` rebuilds the test executable) |
| `docs/REVERSING.md` | How to find the game functions |
