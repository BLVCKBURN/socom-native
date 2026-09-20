# SOCOM 1 M8 mission decompiler status

Reference executable: `SCUS_971.34`

## Current coverage

The current decompiler successfully parses the entire supplied M8 `mzanim.zar`:

- 285 animation objects
- 838 named sequence blocks
- 5,767 command records
- 0 structural parse failures

Every command record is preserved. Commands whose argument layout is not yet
proven are emitted with raw operand bytes instead of guessed semantics.

## Sequence block header

Each `Seq_Data` block begins with a 28-byte header.

The first dword is now confirmed to be a local `Name_Index_Table` index:

```cpp
struct SeqBlockHeader {
    uint32_t localSequenceNameIndex;  // +0x00
    uint32_t flags;                   // +0x04, commonly 0x102/0x104
    uint32_t field08;
    uint32_t blockSize;               // +0x0C
    uint32_t field10;
    uint32_t field14;
    uint32_t field18;
};
```

This resolves anonymous blocks to names such as:

```text
objectives:
  start
  Insert
  Sentry
  Ambush_cover
  Player_Ambush
  Hostagewarn
  Computer
  ...
```

## Command header

```cpp
struct SeqCommandHeader {
    uint16_t rawOpcode;
    uint16_t meta;
};

commandId  = rawOpcode & 0x00FF;
recordSize = (meta & 0xFFFC) >> 2;
```

The high byte of `rawOpcode` contains additional flags and is preserved.

## Confirmed compact command payloads

### OBJECT_ACTIVE_STATE

Executable parser: approximately `0x001B41B0`  
Runtime tick: approximately `0x001BE3F0`

The parser explicitly reads the RDR fields `NAME` and `STATE`.

Serialized M8 form:

```cpp
struct CmdObjectActiveStateDisk {
    uint16_t opcode;
    uint16_t meta;
    uint16_t state;
    uint16_t localNameIndex;
};
```

The tick resolves the target node and invokes its active-state virtual method
with either `1` or `0`.

Example decompile:

```text
OBJECT_ACTIVE_STATE target="m8extract" state=ON
```

### Animation-control commands

The following compact records store a target local-name index at `+0x04`:

```text
CALL_ANIMATION
STOP_ANIMATION
PAUSE_ANIMATION
RESUME_ANIMATION
INVALIDATE_ANIMATION
CALL_SEQUENCE
STOP_SEQUENCE
```

Examples:

```text
CALL_ANIMATION "arrow1"
STOP_ANIMATION "outdoor_clip"
INVALIDATE_ANIMATION "failure"
CALL_SEQUENCE "success"
```

`CALL_SEQUENCE` targets named sequence blocks from the same animation object's
local name table.

### CALL_ANIMATION runtime options

The executable parser also exposes optional textual fields:

```text
NAME
LOCAL_NAME
STOP_ON_EXIT
WITH_INPUT_ARGS
OPERAND_NODE
WITH_NODE
```

Those options are packed into runtime flags/fields after parsing. Their compact
serialized representation is still being mapped and is therefore not guessed
in the decompiler yet.

## Control-flow presentation

The decompiler recognizes and indents:

```text
IF
ELSEIF
ELSE
ENDIF
WHILE
END_WHILE
BREAK
```

Condition payloads are currently kept raw until the expression operand format
is fully recovered.

## Output philosophy

The decompiler is intentionally conservative:

- proven operands are named and typed
- unknown operands remain visible as `raw_args`
- no unknown field is silently discarded
- no guessed semantic is emitted as fact

This makes the current output useful for continued reverse engineering while
remaining lossless enough to revisit as additional command layouts are decoded.

## Next targets

Highest-value remaining command layouts by M8 usage frequency include:

```text
RANGE_TEST
RANDOM_WEIGHT
LIGHT
SOUND
WAIT
DESTRUCTION_SOURCE
TIMER
OBJECT_MOTION
PARTICLE_SOURCE
OBJECT_MOTION_SI_SCRIPT
```

Decoding those will turn large portions of the mission script from structural
pseudo-code into executable native-PC mission behavior.
