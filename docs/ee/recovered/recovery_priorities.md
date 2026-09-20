# Native conversion recovery priorities

## Phase A — original application state path
1. Recover the object/type behind `0x004B31D8` used to resolve UI command arguments.
2. Recover the menu-state storage around `0x004B3450` used by `SwitchMenu`.
3. Decompile helpers called by `SetMission`, especially `0x001CB780`, `0x001CEAE0`, and `0x003BC630`.
4. Recover the state transition helpers called by `SetMenuState`.

## Phase B — original mission runtime
1. Split `0x001FA610` into subsystem initialization responsibilities.
2. Name the RDR helper family around `0x00240E50`–`0x00242040` using the already recovered compiled-RDR representation.
3. Recover `0x001F8580` (mission state transition), `0x001F8900`, `0x001FEF40`, and `0x001F4490` from the real frame loop.
4. Type the object at mission offset `+0x594`.

## Phase C — gameplay
Work outward from the frame routine into zSeal, zAI/zGraph, zEntity, zWeapon and zAnim. Reuse the retail tick order and object layouts instead of the temporary OpenGL runtime's gameplay controller.

## Phase D — platform replacement
Only PS2-specific services should be replaced wholesale: GS/VIF/VU rendering backend, PAD input, SPU2 sound, MPEG, CD/DVD filesystem calls, IOP modules and memory-card persistence.
