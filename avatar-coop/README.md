# Avatar: The Last Airbender (DS) co-op

A two-player co-op mod for *Avatar: The Last Airbender* (Nintendo DS, 2006,
USA, serial `AVAE`). It's a Lua script for the BizHawk emulator. Player 1
plays normally, and Player 2 takes over a party member with a second gamepad
or their own keyboard keys.

The game itself is unchanged. The script reads Player 2's input every frame
and moves their character by writing to the game's memory.

> **Status: framework ready, addresses not found yet.** The script is written
> and tested, but it still needs the memory locations of the characters in this
> game. Until those are filled in, it shows "Co-op: setup needed" and leaves
> the game alone. Finding them takes about an hour with BizHawk's RAM Search
> (see below).

## What you need

- **BizHawk 2.9 or newer** (Windows or Linux): <https://tasvideos.org/BizHawk>.
  It runs DS games with the melonDS core.
- **Your own dump of the game.** The USA version this targets is:
  `Avatar - The Last Airbender (USA).nds`, SHA-1
  `B5BDC88489AD74399E2ED968D816669E594A5F74`.
  The script warns you if a different version is loaded.
- **A DS BIOS and firmware** set up in BizHawk, if your version asks for them.

## Files

| File | What it does |
| --- | --- |
| `coop.lua` | The mod. Run this to play. |
| `config.lua` | Memory addresses, Player 2's buttons, speed and leash. Most of your editing happens here. |
| `probe.lua` | Shows each configured address's live value on screen, so you can check them. |
| `memory.lua` | Helpers the other scripts share. |

## Playing

1. Load the game in BizHawk.
2. Open **Tools > Lua Console**, then **Script > Open Script**, and choose `coop.lua`.
3. Player 1 uses the normal DS controls. Player 2 uses **I J K L** to move by
   default, or the D-pad on a second Xbox-style controller.

BizHawk gives the DS only one controller port, so Player 2's buttons are read
straight from your PC's keyboard and gamepads. To use a different controller,
set `show_host_input = true` in `config.lua`. The screen will show the names
of the buttons you press; copy them into `p2_keys`.

**Playing online:** run BizHawk on one PC and have your friend join through
[Parsec](https://parsec.app). Their controller then works as a second local
gamepad.

## Finding the addresses

You only need to do this once per game version. Use **Tools > RAM Search**
with the memory domain set to **Main RAM**. Characters' positions in DS games
are usually **4-byte signed** values.

### 1. Player 1's position (do this first, it's the easiest)

1. Stand still, then click **Reset** in RAM Search.
2. Walk right a little. Search for values **greater than** the previous value.
3. Walk left. Search for values **less than** the previous value.
4. Stand still. Search for values **equal to** the previous value.
5. Repeat until only a few addresses are left. Add them to **RAM Watch** and
   check that the value changes when you walk left or right.
6. Use the address as `p1.x` in `config.lua`. The vertical axis (`p1.y`) is
   usually 4 or 8 bytes after it; test it by walking up and down.

### 2. Player 2's character

Do the same search, but watch a party member that follows you. They move when
you move, so search in short steps while they are walking.

To confirm you have the right address, **poke** a new value into it in RAM
Watch. The character should jump to a new spot.

### 3. Addresses that move around

If an address stops working after a level change or reload, the game is
keeping that character in memory that moves. Find the new address after
reloading, then search RAM for a 4-byte value close to it (the start of the
character's data, usually a little before the position). The address that
holds that value is the pointer. Write it in `config.lua` like this, where
`offset` is the position's distance from the start of the data:

```lua
x = { ptr = 0x020F1000, offset = 0x30 },
```

### 4. Check, then play

Run `probe.lua`. Each value should change only when it should. Then run
`coop.lua`. If Player 2 moves too fast or too slow, change `p2.speed`.

## Extras once the basics work

- **Facing direction:** find the byte that changes when the companion turns,
  and set `p2.facing` and `p2.facing_values`.
- **Attacks and bending:** find a value that changes when the companion
  attacks (often an "action state"). Add it to `actions` with a button.
  While that button is held, its value is written every frame.
- **Leash:** Player 2 is kept within `leash` of Player 1, so the game's
  camera doesn't lose them. Set it to `nil` to turn this off.

## Known limits

- This is still untested in the real game. It doesn't yet know whether the
  game keeps party members on screen alongside the active character. If it
  only shows one character at a time, Player 2 needs a different approach,
  such as taking turns on the active character. That gets decided once the
  addresses are found.
- The game's own AI also moves the companion. The script overrides it on any
  frame where Player 2 presses a direction. When Player 2 lets go, the AI
  takes over again.
- It only works in BizHawk, not on a real DS. Real hardware would need a
  ROM hack, which is much bigger.
