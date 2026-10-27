-- Settings for the Avatar: The Last Airbender (DS) co-op script.
--
-- Addresses were found with finder.lua (see README.md).
--
-- An address can be written two ways:
--   { addr = 0x0212A4C0 }                  -- fixed location in Main RAM
--   { ptr = 0x020F1000, offset = 0x30 }    -- read a pointer, then add offset
-- Both full DS addresses (0x02xxxxxx) and Main RAM offsets (0x0xxxxx) work.
-- Add `size = 2` or `size = 1` if a value is not 4 bytes wide.

return {
  -- Where each hero's data is: the address of their x position. These are
  -- heap addresses from one level, so they may move in other levels until
  -- the pointers are known. Run the finder as a hero to fill in their line.
  heroes = {
    { name = "Aang", at = { addr = 0x0222F774 } },
    { name = "Sokka", at = { addr = 0x02242A74 } },
    { name = "Katara", at = nil },
    { name = "Haru", at = nil },
  },

  -- Every hero's data has the same layout, as offsets from their x position.
  layout = {
    x = 0x00, -- world x
    y = 0x08, -- world depth
    facing = 0x1C, -- direction the hero faces
    -- The hero's own "controller" value, if the game has one: found with
    -- the finder's button search (P). Until it's filled in, Player 2's
    -- A/B/X/Y do nothing and "AI off" can only hold heroes in place.
    pad = nil,
  },

  p2 = {
    -- The hero Player 2 starts with.
    hero = "Sokka",
    -- How far Player 2 moves per frame, in the game's own position units,
    -- until Player 1 has walked a bit: after that it matches Player 1's speed.
    speed = 10000,
    -- Facing units in one full turn (a quarter turn measured as 16384).
    facing_turn = 0x10000,
    -- What the controller value reads with nothing or each button held
    -- (from the finder's button search).
    pad_values = { none = nil, A = nil, B = nil, X = nil, Y = nil },
  },

  -- Player 1's position: a fixed copy of whichever hero they're controlling,
  -- the same in every level. Required: coop.lua watches it to learn the
  -- camera angle, so Player 2's controls match the screen.
  p1 = {
    x = { addr = 0x021C7E64 },
    y = { addr = 0x021C7E6C },
  },

  -- Keep Player 2 within this distance of Player 1 so they can't wander off
  -- screen or get left behind. Set to nil to turn off.
  leash = 4096 * 96,

  -- Extra moves for Player 2. While the button is held, `value` is written to
  -- the address every frame (for example an "attacking" state flag).
  -- Example once found: attack = { button = "U", addr = 0x0212A4F0, size = 1, value = 3 },
  actions = {},

  -- Keys are host input names from BizHawk's input.get(). Keyboard keys work
  -- as-is. For a second gamepad, use names like "X2 DpadUp" (XInput) or
  -- "J2 POV1U" (DirectInput); turn on show_host_input below to see the exact
  -- names your controller sends.
  p2_keys = {
    up = { "I", "X2 DpadUp" },
    down = { "K", "X2 DpadDown" },
    left = { "J", "X2 DpadLeft" },
    right = { "L", "X2 DpadRight" },
  },
  p2_buttons = {
    A = { "O", "X2 A" },
    B = { "U", "X2 B" },
    X = { "P", "X2 X" },
    Y = { "Y", "X2 Y" },
  },
  -- Player 2 switches to the next hero.
  p2_switch = { "H", "X2 RightShoulder" },
  -- Turn each hero's computer control on or off, in the order of `heroes`.
  ai_toggle = {
    { "D1", "NumberPad1" },
    { "D2", "NumberPad2" },
    { "D3", "NumberPad3" },
    { "D4", "NumberPad4" },
  },

  -- Show which host buttons are being pressed, to help set up the keys.
  show_host_input = false,

  -- Party stats, already known from this repository's cheat codes
  -- (cht/Nintendo - Nintendo DS/Avatar The Last Airbender (U).cht).
  -- Co-op doesn't use them yet; probe.lua shows them so you can check that
  -- the scripts are reading the game's memory correctly.
  party = {
    { name = "Aang", hp = { addr = 0x021C7798, size = 2 }, max_hp = { addr = 0x021C7794, size = 2 } },
    { name = "Sokka", hp = { addr = 0x021C77B4, size = 2 }, max_hp = { addr = 0x021C77B0, size = 2 } },
    { name = "Katara", hp = { addr = 0x021C77D0, size = 2 }, max_hp = { addr = 0x021C77CC, size = 2 } },
    { name = "Haru", hp = { addr = 0x021C77EC, size = 2 }, max_hp = { addr = 0x021C77E8, size = 2 } },
  },
}
