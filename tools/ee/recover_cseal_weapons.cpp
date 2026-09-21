#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: recover_cseal_weapons OUT_DIR\n";
        return 2;
    }

    fs::path out = argv[1];
    fs::create_directories(out);

    {
        std::ofstream f(out / "cseal_weapon_layout.csv");
        f << "object,offset,meaning,confidence,evidence\n";
        f << "CSeal,+0x160,zoom-control state,high,\"jump-table state machine; nearby literal zoom_control\"\n";
        f << "CSeal,+0x161,previous zoom-control state,high,\"updated by 0x002522A0\"\n";
        f << "CSeal,+0x162,zoom state dirty/change flag,high,\"set on transition\"\n";
        f << "CSeal,+0x164,zoom scale/parameter,high,\"state cases write 1.0/1.5/2.0/9.0 or derived value\"\n";
        f << "CSeal,+0x2D0,generic weapon attachment/name handle,high,\"constructor resolves literal weapon\"\n";
        f << "CSeal,+0x2D4,rifle attachment/name handle,high,\"constructor resolves literal rifle\"\n";
        f << "CSeal,+0x2D8,pistol attachment/name handle,high,\"constructor resolves literal pistol\"\n";
        f << "CSeal,+0x2DC,grenade attachment/name handle,high,\"constructor resolves literal grenade\"\n";
        f << "CSeal,+0x530,equipment/weapon runtime subobject,high,\"slot population + weapon subsystem calls + zoom lookup\"\n";
        f << "CSeal,+0xFB4,attachment-record pointer array,high,\"0x0025CC00 stores per-type attachment records\"\n";
        f << "Equipment,+0x0DC,weapon pointer array [30],high,\"indexed by slot across 0x002B6D60/0x002BB5E0/0x002BB680\"\n";
        f << "Equipment,+0x154,per-slot companion pointer/data,high,\"written by 0x002B6DF0\"\n";
        f << "Equipment,+0x814,current selected weapon slot,high,\"0x002BB680 indexes with this field\"\n";
        f << "Equipment,+0x81C,weapon slot count,high,\"wrapping/bounds logic in 0x002BB5E0\"\n";
        f << "Equipment,+0x820,owner CSeal pointer,high,\"helpers dereference +0x820 then use CSeal fields\"\n";
        f << "Equipment,+0x828,selectable/ordered count,medium,\"used by next-selection helper 0x002B3A50\"\n";
        f << "Equipment,+0x82C,selectable/ordered index array,medium,\"used by next-selection helper 0x002B3A50\"\n";
    }

    {
        std::ofstream f(out / "cseal_weapon_functions.csv");
        f << "address,name,role,confidence\n";
        f << "0x0025CC00,CSeal_CreateAttachmentRecord,\"builds per-type weapon/rifle/pistol/grenade attachment record\",high\n";
        f << "0x002522A0,CSeal_SetZoomState,\"transitions zoom state and scale\",high\n";
        f << "0x002BB5E0,Equipment_GetWeaponWrapped,\"returns weapon pointer by wrapped slot index\",high\n";
        f << "0x002BB680,Equipment_GetCurrentWeapon,\"returns weapon pointer from current slot\",high\n";
        f << "0x002B6D60,Equipment_UpdateWeaponSlot,\"updates an existing weapon slot\",high\n";
        f << "0x002B6DF0,Equipment_PopulateWeaponSlot,\"stores weapon pointer/companion data for a slot\",high\n";
        f << "0x002BDBB0,Equipment_ResolveZoomWeaponData,\"resolves current weapon and zoom/slot companion data\",high\n";
        f << "0x00339760,ZWeapon_GetField18,\"retail weapon accessor used during SEAL equipment creation\",high\n";
        f << "0x0033A610,ZWeapon_GlobalLookup,\"weapon/ammo lookup wrapper used from character equipment descriptors\",medium\n";
    }

    {
        std::ofstream f(out / "weapon_creation_flow.txt");
        f <<
R"(SCUS_971.34 weapon/equipment creation flow

CSeal constructor
  0x0025DE8C -> 0x0025CC00(type=1)   rifle attachment
  0x0025DE98 -> 0x0025CC00(type=2)   pistol attachment
  0x0025DEA4 -> 0x0025CC00(type=3)   grenade attachment

  +0x2D4 = resolve("rifle")
  +0x2D8 = resolve("pistol")
  +0x2DC = resolve("grenade")
  +0x2D0 = resolve("weapon")

seal_create equipment setup
  0x00295B44: equipment = CSeal + 0x530
  reads character descriptor list at creationDescriptor + 0x258/+0x260
  loops 12-byte equipment descriptors
  0x002BB5E0: obtains weapon instance for slot/index
  0x00339760: weapon accessor
  weapon category byte at weapon + 0x1D4 selects:
      0 => null/default
      1 => rifle
      2 => pistol
      other observed path => grenade
  0x002B6D60 / 0x002B6DF0 populate equipment slot data

Equipment runtime
  weapon pointers: equipment + 0x0DC + slot*4
  current slot:    equipment + 0x814
  count:           equipment + 0x81C
  owner CSeal*:    equipment + 0x820

Native translations supplied:
  retail 0x002BB680 -> GetCurrentWeaponGuestPtr
  retail 0x002BB5E0 -> GetWeaponWrappedGuestPtr
)";
    }

    std::cout << "CSeal weapon-runtime recovery reports written to " << out.string() << "\n";
    return 0;
}
