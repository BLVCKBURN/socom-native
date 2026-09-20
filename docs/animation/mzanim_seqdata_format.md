# SOCOM 1 `mzanim.zar` Seq_Data format

Reference build: `SCUS_971.34`

The M8 mission archive validates the following structure across every
`Seq_Data` payload examined.

## Overall result

- M8 animation/sequence definitions: 285
- Parsed Seq_Data blocks: 838
- Parsed command records: 5767
- Structural parse errors: 0

## Sequence blocks

`Seq_Data` is a concatenation of variable-length blocks.

Each block begins with a 28-byte header:

```cpp
struct SeqBlockHeader {
    uint32_t field00;
    uint32_t field04;      // commonly 0x00000102 / related state bits
    uint32_t field08;
    uint32_t blockSize;    // +0x0C, size relative to this block start
    uint32_t field10;
    uint32_t field14;
    uint32_t field18;
};
```

Commands begin at block offset `+0x1C`.

`blockSize` is relative, not an absolute Seq_Data offset. Advancing by
`blockSize` walks every block exactly to EOF.

## Command record header

Each serialized command begins with:

```cpp
struct SeqCommandHeader {
    uint16_t rawOpcode;
    uint16_t meta;
};
```

The command ID is:

```cpp
commandId = rawOpcode & 0x00FF;
```

The high opcode bits are separate flags. M8 currently shows `0x0100` and
`0x0200` variants for some commands; their exact semantics remain under study.

The serialized record size is:

```cpp
recordSize = (meta & 0xFFFC) >> 2;
```

This is independently validated against allocation sizes in the command
parsers in `SCUS_971.34`.

Examples:

```text
OBJECT_ACTIVE_STATE parser allocation = 8 bytes
meta 0x0022 -> (0x0022 & 0xFFFC) >> 2 = 8

CALL_ANIMATION parser allocation = 24 bytes
meta 0x0062 -> (0x0062 & 0xFFFC) >> 2 = 24
```

## Command IDs

Command IDs correspond to command registration order in the executable.

Examples confirmed from M8 data:

```text
16  OBJECT_ACTIVE_STATE
33  CALL_ANIMATION
34  STOP_ANIMATION
38  CALL_SEQUENCE
46  TIMER
59  HUD_ON
62  HUD_LETTERBOX_OFF
65  HUD_STOP_DEFUSE_TIMER
```

The complete recovered registry is in `zanim_command_registry.csv`.

## Practical consequence

M8 `Seq_Data` is no longer an opaque byte stream. We can now deterministically
split it into blocks and typed command records, then reverse each command's
arguments from its parser/tick implementation.

That is the next step toward converting SOCOM mission choreography into a
native-PC event/state graph.
