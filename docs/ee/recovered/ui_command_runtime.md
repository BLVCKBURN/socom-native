# SCUS_971.34 UI Command Runtime Recovery

## Command argument context

`SetMission` (`0x001D61A0`) and `SwitchMenu` (`0x001D5DB0`) both load the global command-context pointer at `0x004B31D8` and pass it to `0x001CB780`.

Observed fields of the pointed-to context:

- `+0x18`: dynamic input source used when a command argument is absent or resolves to `INPUT_STRING`.
- `+0x24`: argument-storage object passed into the lower-level lookup path.
- `+0x34`: 16-bit context/registry handle used by `0x001CB780`.

The exact original C++ type name is not yet proven, so the recomp headers deliberately keep descriptive names.

## `SetMission`

Recovered flow:

1. Read the command's argument index from command record byte `+7`.
2. Resolve that argument through `0x001CB780` and global context `0x004B31D8`.
3. If no literal argument is available, or if the value is `INPUT_STRING`, resolve a dynamic string through the context's `+0x18` object and `0x001CEAE0`.
4. Perform a typed global/UI value lookup through the registry at `0x004D4F10`; a type-3 result supplies a string pointer.
5. Call `0x001FAE10` on the `CMission` singleton at `0x004D4880` with the resolved mission name.
6. Return success (`1`).

This establishes a direct original-game path from RDR UI command execution to mission descriptor loading.

## `SwitchMenu` storage

The global storage base is `0x004B3450`.

The first `0x820` bytes are exactly 40 records of `0x34` bytes each. `SwitchMenu` computes `index * 52`, writes a 32-bit command type at record `+0`, and copies a menu/screen string at record `+4`.

```cpp
struct MenuCommandRecord {
    uint32_t type;
    char text[48];
}; // 0x34 bytes
```

Additional recovered offsets:

- `+0x820`: counter incremented when a menu command record is queued.
- `+0x824`: record index/count used when selecting a 0x34-byte slot.
- `+0x828`: vector/stack-like object (`0x004B3C78`).
- `+0xE34`: second vector/stack-like object (`0x004B4284`).

The semantic names of the two counters/stacks remain provisional; their layout is not.

## Exact retail helper translations

The following tiny functions have been translated directly into native C++ in `runtime/recomp/recovered/ui_native_helpers.hpp`:

- `0x001D4930`: `count == 0`
- `0x001D4940`: decrement count
- `0x001D4950`: return pointer to last 32-bit entry
- `0x001D4990`: const-equivalent last-entry helper
- `0x001D6150`: second `count == 0` template instantiation

They can already be registered in `NativeDispatch` at their original EE addresses. This is intentionally the first small set of function-for-function native recompilation in the project.
