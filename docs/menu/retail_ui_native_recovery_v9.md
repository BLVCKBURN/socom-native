# Retail UI Native Recovery v9 — Major Breakthrough

v9 recovers the command-consumption and navigation model used by the retail executable.

## The command "queue" is actually a stack

The UI command storage begins at:

```text
0x004B3450
```

Layout recovered from `SCUS_971.34`:

```text
+0x000  RetailMenuCommandRecord records[40]
+0x820  pending command count
+0x824  current/top record index
+0x828  active screen-history vector
```

Each record is:

```cpp
struct RetailMenuCommandRecord {
    uint32_t type;
    char text[48];
}; // 0x34
```

The command handlers increment `+0x824`, write the new record, then increment `+0x820`.

The retail UI update routine at:

```text
0x0039C470
```

reads `records[topIndex]`, dispatches it, performs cleanup, decrements the counters, and loops while `pendingCount != 0`.

Therefore the retail command system is **LIFO**, not FIFO.

v8 modeled this as FIFO. v9 fixes that architectural error.

## Recovered internal command types

The consumer at `0x0039C470` switches on:

```text
2 = forward screen switch
5 = back/previous screen switch
6 = activate button
7 = enable button
8 = disable button
```

Types 2 and 5 both call the retail screen-loader routine at:

```text
0x0039C8A0
```

but type 2 has extra navigation-history work before/after the load.

## Recovered SwitchMenu semantics

`SwitchMenu` handler:

```text
0x001D5DB0
```

contains the literal:

```text
PREVIOUS_SCREEN
```

Normal transition:

```text
SwitchMenu("dlgOptions_Menu.rdr")
    -> enqueue type 2
    -> consumer pushes current screen onto history
    -> load dlgOptions_Menu.rdr
```

Back transition:

```text
SwitchMenu("PREVIOUS_SCREEN")
    -> resolve history.back()
    -> remove that history entry
    -> enqueue type 5 with resolved screen name
    -> load previous screen without pushing current screen
```

There is an additional retail rule:

```text
if explicit target == history.back()
    treat it as a back transition (type 5)
```

This avoids building duplicate A/B/A history chains.

## Retail history storage

The vector at `+0x828` stores active previous-screen names.

The retail implementation uses pooled char buffers. A second vector around `+0xE34` acts as a free-list. `FlushMenuHistory` moves active buffers back into that free-list.

The native port does not need to mimic allocator details, so v9 uses owned `std::string` values while preserving the externally visible stack behavior.

## PC runtime change

The separate Windows-side `g_history` approximation has been removed.

Escape/back now queues:

```text
PREVIOUS_SCREEN
```

through the recovered retail navigation state.

RDR-generated `SWITCHMENU`, button activation, enable and disable operations all flow through the same LIFO command stack consumed by the native UI update loop.

This is a substantial move from "a PC UI inspired by the retail data" toward a native recompilation of the original front-end architecture.
