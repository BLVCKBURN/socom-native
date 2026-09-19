# SOCOM 1 body / bone reverse-engineering notes

Confirmed against SCUS_971.34 and the supplied retail assets.

## Runtime bone registry

Function `0x0028C720` is the global name -> numeric bone-ID registry.

Observed behavior:

- Maximum 64 registered names.
- Names must be shorter than 32 bytes.
- Backing name table uses 32-byte slots.
- Existing names return their previous numeric ID.
- New names receive the next sequential ID.
- The animation loader calls this registry while processing motion bone names.

Iterating `motion.zar` clips in archive order and recording each first-seen
bone name reproduces 32 names for the supplied game build.

## CBody / CZBodyPart

`0x001E4540` searches body parts by node name.

`0x001E45E0` searches body parts by numeric ID.

`0x001E4A70` constructs a body tree from a GameZ CNode graph.

Confirmed CZBodyPart fields:

- `+0x0C`: backing CNode pointer
- `+0x20`: parent CZBodyPart pointer
- `+0x24`: signed 16-bit runtime bone ID

The body maintains an ID-indexed part table beginning at body `+0x30`.

The CNode child collection used during recursive body construction is at
approximately node `+0x68`, and the node name pointer used by body lookup is at
node `+0x90`.

## SEAL body initialization

The SEAL initialization code first requests the asset named `seal_body`.
If that is absent it falls back to `skel_root`.

It then passes that CNode graph to the body constructor and caches pointers to
named parts such as:

- lfoot / rfoot
- lhand / rhand
- hips
- spinelo / spinehi
- head / neck
- thighs / calves / feet / toes
- biceps / forearms
- scapulae / shoulder-weight nodes
- aimnodes
- weapon

## Asset library path

The GameZ asset-library loader builds sibling archive names with:

`run/%s/%s%s.zed`

and the suffixes:

- `_txr`
- `_pal`
- `_mdl`

After `_mdl.zed` loads, the executable searches the archive for the `models`
collection and instantiates its entries.

For the MP6 character library the relevant file is therefore:

`run/mp/mp6/clib/clib_mdl.zed`

That archive is the remaining source needed to recover the exact `seal_body`
CNode parent/child hierarchy.
