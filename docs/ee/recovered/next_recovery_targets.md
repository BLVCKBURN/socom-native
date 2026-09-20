# Next Retail Recovery Targets

With boot/menu/mission lifecycle boundaries now stable, the next high-value work is:

1. **Finish `SetMission` dependencies**
   - type `0x001CB780` command argument resolver;
   - type the object behind `0x004B31D8`;
   - identify the typed-value lookup at `0x003BC630` / registry `0x004D4F10`.

2. **Recover the mission scheduler registration API**
   - identify exact register/unregister semantics around the global scheduler at `0x00527130`;
   - attach the known retail callbacks (`diTick`, `Mission`, `Update visual effects`) to their phases.

3. **Split `0x001FA610` into subsystems**
   - mission reader mount (`readerm.zar`);
   - objectives (`hudobjectives.rdr`);
   - mission cameras (`missioncams.rdr`);
   - actions (`actions.rdr`);
   - sound-bank setup;
   - `seal_tent` / `terrorist_tent` spawn-node resolution.

4. **Descend from `0x001F8EB0` into player/gameplay calls**
   - type the object returned by `0x00200010`;
   - recover player state checks through `0x0028F120`;
   - connect `zSeal`, `zAI`, `zGraph`, `zWeapon`, entity and objective logic to the native dispatch map.

5. **Translate simple leaf functions first**
   - continue replacing small deterministic EE helpers in native C++;
   - use the retail addresses as dispatch keys;
   - keep unresolved functions explicit rather than approximating their behavior.
