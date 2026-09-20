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

## Typed `TIMER` command

`TIMER` is command ID 46. Its source parser is at `0x1AB8D0`; it allocates a
12-byte runtime object, stores the operation byte at `+0x04`, and stores the
converted float operand at `+0x08`. The tick handler at `0x1C0D00` reads the
single global timer float at `0x4D48E8`.

The tick jump table at `0x462190` establishes the operation mapping:

```text
0  ==
1  !=
2  >=
3  <=
4  >
5  <
6  SET
```

The serialized M8 form is 16 bytes including the standard command header:

```cpp
struct TimerRecordM8 {
    SeqCommandHeader header;
    uint32_t valuePayload;       // +0x04
    uint32_t modePayload;        // +0x08
    uint8_t  valueOperandTag;    // +0x0C; normally 0x07
    uint8_t  modeOperandTag;     // +0x0D; normally 0x02
    uint16_t reserved;           // +0x0E
};
```

The generic loader evaluates the tagged serialized payloads and converts them
to the smaller runtime form. The decompiler now emits the known comparison
semantics (for example, `TIMER test != 8`) while retaining non-canonical tags
for dynamic/symbolic operands so no information is lost.

## Typed `SOUND` command

`SOUND` is command ID 27. Its parser at `0x1B56A0` allocates a 32-byte base
runtime command. The tick handler at `0x1BF060` consumes this layout:

```cpp
struct SoundCommandBase {
    SeqCommandHeader header; // +0x00
    uint16_t flags;          // +0x04
    uint8_t soundId;         // +0x06
    uint8_t alternateId;     // +0x07
    float volume;            // +0x08, when flags & 0x0010
    uint16_t pan;            // +0x0C, when flags & 0x0020
    int16_t pitchRaw;        // +0x0E, when flags & 0x0040
    uint8_t nodeId;          // +0x10, when flags & 0x0002
    uint8_t reserved11;
    int16_t appendedOffset;  // +0x12, used by fade data
    float translation[3];    // +0x14, when flags & 0x0004
};
```

Confirmed flag bits from the parser and tick handler:

```text
0x001  alternate name lookup    0x080  START
0x002  AT_NODE                  0x100  STOP
0x004  TRANSLATION              0x200  STOP_ON_EXIT
0x008  INPUT_TRANSLATION        0x400  MUSIC_LOCAL
0x010  VOLUME                   0x800  MUSIC_EVENT
0x020  PAN
0x040  PITCH
```

M8 has 252 compact 24-byte `SOUND` records and two 172-byte records used by
`fireplace_flame1` and `fireplace_flame2`. The long records carry the confirmed
`PAN|MUSIC_LOCAL` flags and appended parameter data. The decompiler prints all
confirmed fields and keeps both unresolved compact words and appended bytes
visible, rather than assigning speculative meanings.

## Typed `CAMERA` command

`CAMERA` is command ID 25. The parser begins at `0x1B5B60` and its primary
tick handler begins at `0x1BF540`. The handler proves that the 16 low control
bits are eight two-bit property modes:

```text
bits  0-1   H_FOV       bits  8-9   FAR_CLIP
bits  2-3   V_FOV       bits 10-11  FOG_NEAR
bits  4-5   NEAR_CLIP   bits 12-13  FOG_FAR
bits  6-7   MID_CLIP    bits 14-15  FOG_COLOR
```

The handler uses paired absolute/interpolated values for the scalar camera
properties and three paired components for fog color. A duration/progress
value controls interpolation.

M8 contains two serialized families:

- 69 compact records with a zero control word and action IDs `0` through `4`.
  The single action-4 record also carries the name `Snow3`.
- 13 extended 80-byte records whose control word is `0x000003F9` and whose
  remaining payload is a packed float stream.

The decompiler prints compact action IDs and names directly. Extended records
print the confirmed property-mode map and every packed float. Action labels and
the final packed-to-runtime expansion remain deliberately unnamed until the
loader path proves them.

## Nested `IF` / `ELSEIF` expressions

Commands 2 and 3 begin with a 32-bit expression-mode field at record offset
`+4`. Starting at `+8`, the expression contains a stream of nested records with
the same `rawOpcode`, `meta`, and derived record-size rules as top-level ZANIM
commands. Parsing continues until the outer record ends or a diagnostic-string
sentinel is reached. This permits recursive, lossless condition decompilation.

Some zero-term/compiler-diagnostic forms append `0xFFFFFFFF` and an aligned
NUL-terminated message such as `INRANGE test passed`. The decompiler now emits
those messages and retains any remaining trailing bytes.

## Typed `DESTRUCTION_SOURCE`

Command 26's parser at `0x1B8610` constructs an 80-byte particle/destruction
source with node, texture, RGBA, maximum size, velocity ranges, lifespan range,
friction, world acceleration, strip/shard mode, and opacity. Its tick handler
at `0x1C0E20` passes those fields to the particle-source constructor.

M8 stores a compact variable-length form. Two consecutive strings beginning at
record offset `+0x10` identify the optional node and required texture/resource
name. Most records target the current node and reference resources such as
`SND_VM08_35`; named variants include nodes such as `DiMone` and `Hutchins`.
The preceding 12-byte compact prefix remains visible pending recovery of its
loader-side expansion.

## Typed child attachment

The compact eight-byte command-41 form matches the runtime structure used by
parser `0x1AC8C0` and tick handler `0x1BA7C0`:

```cpp
struct ObjectAddChild {
    SeqCommandHeader header;
    uint16_t flags;       // bit 0: retain world location
    uint8_t parentId;
    uint8_t childId;
};
```

The M8 decompiler now names both IDs and the retain-world-location option.
Larger command-42/43 variants remain raw because their serialized loader
transformation is not equivalent to this compact runtime form.

## Typed `LOOP` and `WAIT`

Compact `LOOP` records contain a mode and signed count. Variable-length forms
add an argument, enable field, and aligned sequence name; M8 uses names such as
`Patrol_guns`, `Spot_shout`, and `Close_call`.

The common `WAIT` mode `0x09` carries a floating-point duration in seconds.
Mode `0x10` carries an integer frame count. Less common extended modes retain
their additional bytes until their range/randomization semantics are proven.

## Typed `RANGE_TEST`

Command 8's tick handler at `0x1B97E0` resolves two optional nodes or
translations, computes their displacement, squares its length, and compares it
with the configured squared-radius field. Six comparison flag bits implement
`<`, `<=`, `>`, `>=`, `==`, and `!=` tests.

The common compact M8 record exposes a flags word, source radius, reference
type, and reference payload. The loader expands these into the runtime node,
translation, comparison, and squared-radius fields. Extended compact bytes are
retained until that expansion is fully mapped.
