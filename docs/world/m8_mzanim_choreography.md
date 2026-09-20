# SOCOM 1 M8 `mzanim.zar` choreography analysis

## Archive summary

- ZAR/CZAR version: `0x00020002`
- Total archive nodes: 3,069
- Animation set: `mission`
- Mission name-table entries: 809
- SoftImage script groups: 5
- Mission animation/sequence definitions: 285
- Exact archive-name overlap with `m8_mdl.zed`: 175 names

The archive is mission choreography/state data, not skeletal motion. Its symbol tables directly reference many nodes present in the reconstructed M8 world model.

## High-value world systems

### Alarm system

- `alarm_switch_down` → alarmswitch | alarm_action_plate | lighton | alarmlight | lightoff | lightoff_green | lighton_green | alarmlight_green
- `alarm_switch_up` → alarmswitch | alarm_action_plate | lighton_green | lightoff_green | alarmlight_green
- `alarm_triggers_on` → tripwire01
- `alarm_triggers_off` → tripwire01
- `alarm_on_01` → tripwire01 | detector_node
- `alarm_on_02` → tripwire02 | detector_node
- `alarm_on_03` → tripwire03 | detector_node
- `manual_alarm` → (no direct world-node name in local symbol table)
- `thealarm_sound` → alarmhorn | hornlight | hornlightbox_on | hornlightbox_off | flare1
- `alarm_light` → alarmswitch | lighton | alarmlight | lightoff | alarmlight_green

### Generator

- `generator_1` → generator_1 | generator_dis | alarm_action_plate | kill_switch | gen_fiddle_switch | gen_good | gen_parts | stack | destroyed_di
- `alarm_off` → alarm_action_plate
- `turn_off_gen` → gen_fiddle_switch

### Doors / destructibles

- `blowDoor` → doorhit | doordi | doorbits | doorbit1 | doorbit2 | doorbit3 | doorbit4 | doorbit5 | doorbit6 | doorbit7 | doorbit8
- `doorbreak` → doorbits
- `singleDoor` → (no direct world-node name in local symbol table)
- `kick_singleDoor` → (no direct world-node name in local symbol table)
- `outhousedoor1` → door_outhouse1
- `outhousedoor2` → door_outhouse2

### Russian radios

- `russian_radio1` → radio1
- `russian_radio2` → radio2
- `russian_radio3` → radio3

### Extraction / helicopter

- `m8extract` → (no direct world-node name in local symbol table)
- `deactivate_copter` → (no direct world-node name in local symbol table)
- `do_fadetoblack` → (no direct world-node name in local symbol table)

### Objectives

- `m8objsetup` → arrow1
- `m8obj1` → drop_arrow | arrow1
- `m8obj2` → drop_arrow | arrow1
- `m8obj3` → (no direct world-node name in local symbol table)
- `m8obj4` → (no direct world-node name in local symbol table)
- `m8obj5` → (no direct world-node name in local symbol table)
- `m8obj6` → drop_arrow | arrow1
- `m8obj7a` → (no direct world-node name in local symbol table)
- `m8obj8` → (no direct world-node name in local symbol table)
- `m8obj9` → (no direct world-node name in local symbol table)
- `m8obj10` → (no direct world-node name in local symbol table)
- `m8obj11` → (no direct world-node name in local symbol table)
- `m8obj13` → drop_arrow | arrow1
- `m8obj14` → (no direct world-node name in local symbol table)

## Confirmed examples

### Generator / alarm dependency

`generator_1` references the generator assembly and alarm-related scene nodes including `generator_dis`, `generator_off`, `alarm_action_plate`, `gen_fiddle_switch`, `gen_good`, `destroyed`, and `gen_parts`. This strongly indicates the sequence controls the generator's visual/destructible state and its relationship to the alarm system.

### Alarm tripwires

The mission contains separate sequences for `tripwire01`, `tripwire02`, and `tripwire03`. The alarm sequences reference `alarmswitch`, `alarm_action_plate`, `alarmlight`, `alarmlight_green`, `detector_node`, and the alarm horn/light nodes found in the M8 scene graph.

### Doors

Door choreography references concrete world nodes such as `door_outhouse1`, `door_outhouse2`, `doorbits`, `doordi`, `doorhit`, and individual `doorbit1`…`doorbit8` nodes. This gives us a path to reconstruct interactive/destructible doors rather than treating them as static meshes.

### Radios

`russian_radio1`, `russian_radio2`, and `russian_radio3` directly reference the recovered `radio1`, `radio2`, and `radio3` world nodes and associated music/effect symbols.

### Extraction helicopter

`m8extract` references `copter`, `mainrotor`, `tailrotor`, camera symbols, particle textures, and helicopter audio. This sequence is a strong candidate for the extraction cutscene/world-animation controller.

## Next reverse-engineering step

Decode `Seq_Data` into opcode/instruction records by tracing the sequence interpreter in `SCUS_971.34`. The name-index table is already decoded, so once each opcode's operand format is known we can convert the mission choreography into an explicit event graph such as:

```text
trigger -> condition -> target world node -> action -> sound/effect -> next sequence
```

This will be the basis for native-PC mission scripting.