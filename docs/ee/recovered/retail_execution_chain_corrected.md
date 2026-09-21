# Corrected Retail Execution Chain — SCUS_971.34

## Critical analyzer correction

The first reverse-engineering analyzer treated virtual address `0x00100000` as raw file offset zero.

That was wrong.

The primary ELF `PT_LOAD` segment is:

- file offset: `0x80`
- virtual address: `0x00100000`
- file size: `0x0038D200`
- memory size: `0x004D6D00`

Every instruction-level recovery must therefore translate addresses using:

```text
file_offset = 0x80 + (virtual_address - 0x00100000)
```

The earlier high-level string/source discoveries remain useful, but any instruction boundary or callsite produced from the old raw mapping must be regenerated.

## Confirmed UI argument resolver

`0x001CB780` is a genuine function entry.

Observed behavior:

1. Saves `a0` as the command context.
2. Reads a 16-bit selector from command-context `+0x34`.
3. Uses global pool object `0x004B31A0`.
4. Calls `0x001C5270` to select an entry from that pool.
5. Resolves an argument through the command-context table at `+0x24` by calling `0x001C3AF0`.

### Global argument pool

`0x001C5270` exposes the pool layout:

```cpp
struct ArgPool {
    // unknown fields
    void* records;      // +0x1C
    int32_t count;      // +0x20
};
```

Record stride is `0x30` bytes.

### Command argument table

`0x001C3AF0` bounds-checks the supplied argument index against a count at table `+0x04`, then indexes a base pointer at `+0x00` using an 8-byte record stride.

This gives us two concrete original GameZ/UI runtime containers instead of opaque pointers.

## Confirmed SetMission path

`SetMission` really begins at `0x001D61A0`.

At `0x001D61B8` it calls:

```text
0x001CB780
```

with:

- `a0 = *(0x004B31D8)` — current UI command context
- `a1 = command byte at +0x07`

The return is then validated and ultimately passed into the mission-selection path.

## Confirmed mission frame

`0x001F8EB0` is a real function prologue and is the current high-confidence `CMission` frame routine.

It performs, among other work:

- mission time/countdown updates,
- checks for result states `2`, `3`, `4`, and `5`,
- processes the active gameplay object at `CMission + 0x594`,
- calls objective maintenance,
- triggers result/cleanup transitions,
- continues into downstream gameplay/mission helpers.

The first major call at `0x00200010` sits immediately adjacent to the recovered `fts_player.cpp` neighborhood (`0x002000D0`, `0x00200230`, `0x00200330`), making the player subsystem the next high-value recovery target.

## Scheduler callback wrapper

`0x001F94A0` is a real thin callback:

```text
prologue
call 0x001F8EB0
return 0
epilogue
```

The unresolved scheduler task is now specifically to identify where this callback address is inserted into the engine callback/tick registry.

## Next targets

1. Type the object at `0x004B31D8`.
2. Recover the return type/value semantics of `0x001CB780`.
3. Identify the owner/registration site for callback `0x001F94A0`.
4. Recover `0x00200010` and nearby `fts_player.cpp` functions.
5. Trace from there into `seal.cpp`:
   - `0x002517A0`
   - `0x002526C0`
   - `0x002527D0`
   - `0x0025CD50`
6. Continue into graph/navigation and weapon neighborhoods:
   - `graph_graph.cpp` around `0x002128C0`
   - `zwep_weapon.cpp` around `0x003378A0`

This is the path from original menu command execution into original mission/player/gameplay execution.
