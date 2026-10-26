# Finding the game functions

The controller relay works without any of this. Unit ownership and the
per-player End Turn need a few of the game's own functions, which you find
once and write into `offsets.ini`. You don't need to rebuild the mod for this.
Restart the game after editing the file.

Use your own copy of the game, dumped from your own Switch.

## What you need

- **Ghidra** (free, from the NSA's GitHub) plus the **Ghidra Switch Loader**
  extension (Adubbz/Ghidra-Switch-Loader), so Ghidra can open Switch executables.
- The game's **exefs**. In Ryujinx: right-click the game → *Extract Data* →
  *ExeFS*. Do this with the update you actually play installed. You get a
  folder with `main` (the game code) and `main.npdm`.
- For finding things while the game runs: a memory scanner such as Cheat
  Engine attached to the emulator, or the emulator's GDB stub if your build
  has one.
- `coop_check`, built from this repo (`cmake -S . -B build && cmake --build build`).

## Step 1: try the controller relay

Do this first. It tells you the mod loads at all.

1. Build and install the mod (README, "Building the Switch module").
2. In Ryujinx, set up two controllers as Player 1 and Player 2.
3. Turn on guest logging in Ryujinx's logging settings and open the log window.
4. Start the game. You should see:

   ```
   [coop] 2 players ready. Hold ZL + ZR and press Plus to pass the turn.
   ```

5. Only controller 1 moves the cursor. Hold ZL + ZR and press Plus:
   `[coop] Player 2's turn` appears, and now only controller 2 works.

**If the game won't boot** with a missing-symbol error, the `nn::hid` names in
`switch/main.cpp` don't match this game's. Do step 2 and send me what you find.

**If it boots but controller 2 does nothing after passing**, the game reads
input some other way. Step 2 shows which functions it really imports.

## Step 2: check which controller functions the game imports

1. Open `main` in Ghidra (File → Import File) and let it analyse. Defaults are fine.
2. In the *Symbol Tree*, open *Imports* and search for `GetNpadState`.
3. Write down every name you see, for example
   `nn::hid::GetNpadStates(nn::hid::NpadFullKeyState*, int, unsigned int const&)`.

`switch/main.cpp` hooks the FullKey, Handheld and JoyDual variants. If the game
uses others, or the last parameter says `NpadIdType const&` instead of
`unsigned int const&`, send me the list and I'll update the hooks.

## Step 3: the address you write down

The Switch loader places `main` at `0x7100000000`. offsets.ini wants
addresses **relative to the start of main**, so subtract that base:

```
Ghidra shows   7100A1B2C0
offsets.ini    0xA1B2C0
```

Even better, write a **pattern** instead: a pattern keeps working after a game
update, but a raw address usually doesn't.

## Step 4: find each function

Find them in this order. Each one makes the next easier.

### unit_character_id

Returns which character a battle unit is (an `int` id).

1. Start a battle. In the memory scanner, search for a unit's current HP.
   Let it take damage, then search again, until one or two addresses are left.
   That's inside the unit's data.
2. Look at the memory around it. A small number that stays the same for this
   unit but differs for every other unit is the character id. It's usually
   within a few hundred bytes of the HP.
3. In Ghidra, find a short function that reads that field and returns it:
   one `ldr w0, [x0, #offset]` and a `ret`. Use *Search → For Scalars* with the
   field offset to find candidates.

If the game never calls such a function (small ones like this often get
folded into their callers), skip `unit_character_id` and tell the mod where
the id sits inside the unit instead:

```ini
[units]
character_id_field = 0x24   ; bytes from the start of the unit
character_id_size = 2       ; 1, 2 or 4 bytes
```

The start of the unit is the address `can_select_unit` receives as its first
argument (`x0` in Ghidra/the debugger).

### can_select_unit

Decides whether pressing A on a unit picks it up.

1. Let a unit act (Wait). It greys out, and pressing A on it no longer picks it up.
2. Find the byte in that unit's data that changed when it greyed out.
3. Put a read breakpoint/watchpoint on that byte, then press A on the unit.
   Execution stops inside the check. Walk up the call stack to the function
   that returns `true`/`false` for "can be selected".

### begin_player_phase / end_player_phase

1. Find the value that says whose phase it is: search memory for a number
   that changes every time the "Player Phase" or "Enemy Phase" banner shows.
2. Put a write watchpoint on it.
   - The write that switches to the enemy phase leads back to
     **end_player_phase**, the function the map menu's *End* command calls.
   - The write that switches back to the player phase leads to
     **begin_player_phase**.

### The rest

`prep_unit_list_input`, `draw_map_cursor` and `draw_unit_outline` are for
in-game claiming and the colored cursors and rings. Finding them is optional for
now. If you do, the mod logs that it found them, and I'll write those hooks next.

## Step 5: turn it into a pattern and test it

1. In Ghidra's listing, select the first 5–8 instructions of the function,
   then right-click → *Copy Special* → *Byte String*.
2. Replace the bytes of any instruction that contains an address with `??`.
   These are `adrp`, `add` right after an `adrp`, `bl`, `b`, and `ldr` from a
   literal. Those bytes change whenever the game is rebuilt.
3. Write it into offsets.ini:

   ```ini
   [hooks]
   can_select_unit = pattern: FF 43 01 D1 F4 4F 03 A9 FD 7B 04 A9 ?? ?? ?? 94
   ```

   To hook a function through a place that calls it, start the pattern at that
   `bl` and use `call:`. The first four bytes always match any `bl`.

4. Check it against the real game code on your PC:

   ```sh
   ./build/coop_check config/coop.ini config/offsets.ini path/to/exefs/main
   ```

   Each hook shows `-> main+0x...` when the pattern matches exactly once. If it
   says "matches more than once", add more bytes. If it says "not found",
   remove the bytes that change between builds.

5. Copy `offsets.ini` next to `coop.ini` on the SD card and start the game.
   The log shows `[coop] hook can_select_unit at main+0x...` for each one.

## Step 6: map character ids to names

With `unit_character_id` and `can_select_unit` set, press A on each unit once.
The log shows:

```
[coop] unit with character id 12 selected (not in the roster yet)
```

Note which unit was which and send me the list. I'll fill in `src/roster.cpp`
so `coop.ini` can use names. Until then, configs can use ids directly:
`characters = #12, #15`.

## What to send me

- The build id that `coop_check` prints for your `main`, so the patterns are
  tied to your game version.
- Your offsets.ini, plus anything the log printed that you didn't expect.
- The `GetNpadState` import names from step 2, if the relay didn't work.
