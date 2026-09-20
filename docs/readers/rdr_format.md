# Compiled GameZ RDR format (SCUS_971.34)

This note documents the compiled `.rdr` representation observed in SOCOM: U.S. Navy SEALs reference build **SCUS_971.34**. The conclusions below were validated against all RDR leaves in the eight available `reader*.zar` archives.

## Corpus validation

- Archives tested: 8
- RDR leaves tested: 224
- Structurally valid recursive trees: 224/224
- Maximum observed pointer depth: 13
- Observed cell tags: only 1, 2, 3, and 4
- Invalid string cells: 10, all in `zweapon.rdr`; the enclosing lists and pointers remain structurally valid

Reachable-cell census from the 224-file corpus:

| Tag | Observed count | Meaning |
| --- | ---: | --- |
| 1 | 195,002 | list header / signed int32 atom |
| 2 | 33,278 | IEEE-754 float32 atom |
| 3 | 223,874 | string/symbol reference into the string pool |
| 4 | 163,469 | relative pointer to another list |

## Header

All tested compiled RDR files begin with two little-endian 32-bit words:

```text
+0x00  u32 string_pool_size
+0x04  u32 data_offset
+0x08  char string_pool[string_pool_size]
...    optional padding
+data_offset  encoded list data
```

A tag-3 string value is an offset from the beginning of the pool at file offset `0x08`. Strings are NUL-terminated.

A tag-4 pointer is an offset relative to `data_offset`, not an absolute file offset.

## Eight-byte cells

The data region consists of 8-byte cells:

```cpp
struct RdrCell {
    uint32_t tag;
    uint32_t value;
};
```

Observed atom interpretation:

```text
tag 1 -> signed int32
 tag 2 -> float32, stored as raw IEEE-754 bits
 tag 3 -> string/symbol pool offset
 tag 4 -> relative pointer to another list
```

## List representation

Every reachable list begins with a tag-1 cell:

```text
[tag=1, value=cell_count]
```

`cell_count` includes the header cell itself. Therefore a list with `value=3` occupies exactly 24 bytes: one header and two payload cells.

This is deterministic and removes the need for sentinel-based or "scan until the next object" parsing.

Example root of `materials.rdr`:

```text
000000: [1, 3]
000008: [3, "SOILS"]
000010: [4, 0x18]
```

At `0x18`, the SOILS array begins with `[1, 0x25]`: 37 cells total, consisting of one header plus 36 pointers to material records.

## Common semantic shapes

The binary grammar is generic; higher-level meaning comes from recurring list shapes.

### Scalar wrapper

```text
[1, 2]
[1, <int>]
```

or tag 2 for a float and tag 3 for a string.

### Pointer array

```text
[1, N+1]
[4, child0]
[4, child1]
...
```

### Record / property bag

Many records use alternating symbol and pointer cells:

```text
[1, count]
[3, "NAME"]
[4, value_list]
[3, "HEALTH"]
[4, value_list]
...
```

A tag-3 symbol can also occur without a following pointer. In contexts such as materials this is used as a presence/switch flag, for example `LIQUID`, `UNDERWATER`, or `PICKUP`.

A one-cell list (`[1,1]`) is an empty payload. Some reader schemas use that empty/presence form as a logical switch, so the generic parser should preserve the empty list and let schema-specific code assign boolean meaning.

## `zweapon.rdr` anomaly

`zweapon.rdr` is structurally valid. Ten `Description` scalar wrappers inside the `ZAMMO` array contain the same invalid tag-3 value `0xFB95FFC0`, which is outside the string pool.

Affected ammo records:

| ID | Name |
| ---: | --- |
| 11 | M67 Ammo |
| 26 | HE Grenade Ammo |
| 12 | AN-M8 Ammo |
| 14 | Mark141 Ammo |
| 16 | Satchel Charge Ammo |
| 17 | Claymore Ammo |
| 18 | C4 Ammo |
| 19 | MPBOMB Ammo |
| 24 | ag_missile |
| 25 | explosive_stuff |

All other fields in those records decode normally. A tolerant reader should preserve the raw value and mark the string invalid rather than rejecting the file.

## ZAR envelope used by the reader archives

The eight tested reader archives use ZAR version `0x00020002` and 16-byte payload alignment. The RDR tool also validates the archive node table and payload bounds before extracting leaves.

## Tool

`tools/readers/socom_rdr_dump.cpp` is a standalone C++17 validator/dumper for this grammar. It can list RDR leaves in a ZAR, dump one leaf to a lossless typed JSON tree, or validate/dump every RDR leaf in an archive.

Example:

```powershell
g++ -std=c++17 -O2 tools/readers/socom_rdr_dump.cpp -o socom_rdr_dump.exe
.\socom_rdr_dump.exe readerc.zar --list
.\socom_rdr_dump.exe "readerc(1).zar" zweapon.rdr zweapon.json
.\socom_rdr_dump.exe "readerc(1).zar" --all out\readerc1
```

The JSON output deliberately preserves cell tags, relative offsets, raw string offsets, invalid references, and pointer structure. Schema-specific conversion should be layered on top of this representation rather than weakening the generic parser with file-specific assumptions.
