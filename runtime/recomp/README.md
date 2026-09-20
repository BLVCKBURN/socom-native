# EE native-recomp bridge

This directory is the migration layer between the original SCUS_971.34 address space and native PC implementations.

The goal is not to emulate the full PS2. The goal is to preserve original game behavior while functions are translated/reimplemented one by one. `GuestMemory` maps the retail ELF load image plus its BSS at the original EE virtual addresses, `CpuState` preserves the R5900 calling/register shape, and `NativeDispatch` binds original function addresses to native C++ implementations.

This bridge is deliberately separate from the current OpenGL proof runtime. Platform-specific PS2 services (GS/VIF/VU rendering, PAD input, SPU2 audio, MPEG, memory card, CD/DVD paths and later networking) will receive PC backends. Mission state, AI, weapons, UI commands, valves, animation scripting and other gameplay logic should come from the recovered original code/data path.

Do not commit the retail SCUS executable or game assets. The analyzer and generated address metadata are sufficient for source control.
