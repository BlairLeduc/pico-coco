-- ssc-trace.lua: MAME 0.289's Speech/Sound Cartridge driven with fixed
-- bytes, its TMS7040 traced instruction by instruction (design.md §12.9).
--
-- Run by tools/mame/trace-ssc.sh. Environment:
--   SSC_OUT     the directory for ssc.tr (the trace), ssc.end (the 7040's
--               registers and register file when the trace stops) and
--               ssc.bytes (each byte given, with the frame)
--   SSC_FRAMES  how many fields to run (default 600)
--
-- The script writes the 6809's side itself, as a program would: $FF7D
-- set and cleared (the manual's reset), then each byte at $FF7E once
-- BUSY reads high, one a field at most. MAME is a tool, never source
-- (D2, D8): this drives it, and nothing of it is in the core.

local out = os.getenv("SSC_OUT")
local frames = tonumber(os.getenv("SSC_FRAMES") or "600")
local cpu = manager.machine.devices[":ext:ssc:pic7040"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local dbg = manager.machine.debugger

-- "I CAN TALK" and CR; the AY's registers through command $AF (tone A
-- on, at volume 15, period 100); then the manual's allophones for
-- "COLOR COMPUTER" into buffer 0 (command $B0) and spoken ($F0).
local bytes = { 0x49,0x20,0x43,0x41,0x4E,0x20,0x54,0x41,0x4C,0x4B,0x20,0x0D,
                0xAF,7,0x3E, 0xAF,8,15, 0xAF,0,100, 0xAF,1,0,
                0xB0, 8,15,45,51,4,4,4,8,15,16,9,49,31,13,51,4,255, 0xF0 }
local start = 150
local n, i = 0, 1
local log = io.open(out .. "/ssc.bytes", "w")

dbg:command("trace " .. out .. "/ssc.tr,:ext:ssc:pic7040,noloop")

SUB = emu.add_machine_frame_notifier(function()
  n = n + 1
  if n == 120 then
    mem:write_u8(0xff01, 0x34) mem:write_u8(0xff03, 0x3f) mem:write_u8(0xff23, 0x3c)
    mem:write_u8(0xff7d, 1)
    log:write(string.format("%d reset 1\n", n))
  elseif n == 121 then
    mem:write_u8(0xff7d, 0)
    log:write(string.format("%d reset 0\n", n))
  elseif n >= start and i <= #bytes then
    -- After the text, give the speech time before the next command.
    local wait = (i == 13 and n < start + 200) or (i == 25 and n < start + 260)
    if not wait and (mem:read_u8(0xff7e) & 0x80) ~= 0 then
      mem:write_u8(0xff7e, bytes[i])
      log:write(string.format("%d byte %02x\n", n, bytes[i]))
      i = i + 1
    end
  end
  if n == frames then
    dbg:command("trace off")
    local f = io.open(out .. "/ssc.end", "w")
    local st = cpu.state
    f:write(string.format("PC %04X SP %02X ST %02X\n", st["PC"].value, st["SP"].value, st["ST"].value))
    local sp = cpu.spaces["program"]
    for a = 0, 255 do f:write(string.format("%02X%s", sp:read_u8(a), (a % 32 == 31) and "\n" or "")) end
    f:close()
    log:close()
    manager.machine:exit()
  end
end)
