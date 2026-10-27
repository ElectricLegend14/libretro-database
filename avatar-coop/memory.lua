-- Shared helpers for reading and writing the DS's Main RAM from BizHawk.

local M = {}

local DOMAIN = "Main RAM"
local MAIN_RAM_BASE = 0x02000000
local MAIN_RAM_SIZE = 0x400000

-- Accept either a full DS address or an offset into Main RAM.
local function to_offset(a)
  if a >= MAIN_RAM_BASE then
    a = a - MAIN_RAM_BASE
  end
  if a < 0 or a >= MAIN_RAM_SIZE then
    return nil
  end
  return a
end

-- Turn a config entry into a Main RAM offset, following its pointer if it
-- has one. Returns nil when the entry is unset or the pointer is null.
function M.resolve(entry)
  if entry == nil then
    return nil
  end
  if entry.base then
    local b = M.resolve(entry.base)
    return b and to_offset(b + entry.offset)
  end
  if entry.ptr then
    local p = to_offset(entry.ptr)
    if not p then
      return nil
    end
    local target = memory.read_u32_le(p, DOMAIN)
    if target == 0 then
      return nil
    end
    local t = to_offset(target)
    return t and to_offset(t + (entry.offset or 0))
  end
  return entry.addr and to_offset(entry.addr)
end

-- An entry for the value `offset` bytes after another entry's address.
function M.field(base, offset, size)
  return { base = base, offset = offset, size = size }
end

local readers = {
  [1] = function(a) return memory.read_s8(a, DOMAIN) end,
  [2] = function(a) return memory.read_s16_le(a, DOMAIN) end,
  [4] = function(a) return memory.read_s32_le(a, DOMAIN) end,
}
local writers = {
  [1] = function(a, v) memory.write_s8(a, v, DOMAIN) end,
  [2] = function(a, v) memory.write_s16_le(a, v, DOMAIN) end,
  [4] = function(a, v) memory.write_s32_le(a, v, DOMAIN) end,
}

function M.read(entry)
  local a = M.resolve(entry)
  if not a then
    return nil
  end
  return readers[entry.size or 4](a)
end

function M.write(entry, value)
  local a = M.resolve(entry)
  if not a then
    return false
  end
  -- Wrap into the signed range, so unsigned values like 0xFFFF fit too.
  local size = entry.size or 4
  local range = 2 ^ (size * 8)
  value = math.floor(value) % range
  if value >= range / 2 then
    value = value - range
  end
  writers[size](a, math.floor(value))
  return true
end

return M
