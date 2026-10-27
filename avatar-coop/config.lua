-- Settings for the Avatar: The Last Airbender (DS) co-op script.
--
-- Addresses were found with finder.lua (see README.md). Each hero's data has
-- the same layout: x at +0x00, depth at +0x08, facing at +0x1C.
--
-- An address can be written two ways:
--   { addr = 0x0212A4C0 }                  -- fixed location in Main RAM
--   { ptr = 0x020F1000, offset = 0x30 }    -- read a pointer, then add offset
-- Both full DS addresses (0x02xxxxxx) and Main RAM offsets (0x0xxxxx) work.
-- Add `size = 2` or `size = 1` if a value is not 4 bytes wide.

return {
  -- The hero Player 2 controls: Sokka. These are heap addresses from one
  -- level, so they may move in other levels until the pointer is known.
  p2 = {
    x = { addr = 0x02242A74 }, -- required: world x
    y = { addr = 0x02242A7C }, -- required: world depth
    facing = { addr = 0x02242A90 }, -- optional: direction the hero faces
    -- How far Player 2 moves per frame, in the game's own position units.
    -- Heroes walk about 10,000 per frame (finder: ~203,000 in 20 frames).
    speed = 10000,
    -- Facing units in one full turn (a quarter turn measured as 16384).
    facing_turn = 0x10000,
  },

  -- Player 1's position: a fixed copy of whichever hero they're controlling,
  -- the same in every level. Required: coop.lua watches it to learn the
  -- camera angle, so Player 2's controls match the screen.
  p1 = {
    x = { addr = 0x021C7E64 },
    y = { addr = 0x021C7E6C },
  },

  -- Keep Player 2 within this distance of Player 1 so they can't wander off
  -- screen or get left behind. Needs the p1 addresses. Set to nil to turn off.
  leash = 4096 * 96,

  -- Extra moves for Player 2. While the button is held, `value` is written to
  -- the address every frame (for example an "attacking" state flag).
  -- Example once found: attack = { button = "U", addr = 0x0212A4F0, size = 1, value = 3 },
  actions = {},

  -- Player 2's buttons. These are host input names from BizHawk's input.get().
  -- Keyboard keys work as-is. For a second gamepad, use names like
  -- "X2 DpadUp" (XInput) or "J2 POV1U" (DirectInput); turn on
  -- show_host_input below to see the exact names your controller sends.
  p2_keys = {
    up = { "I", "X2 DpadUp" },
    down = { "K", "X2 DpadDown" },
    left = { "J", "X2 DpadLeft" },
    right = { "L", "X2 DpadRight" },
  },

  -- Show which host buttons are being pressed, to help set up p2_keys.
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
