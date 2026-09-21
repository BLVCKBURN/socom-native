#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "cseal_weapon_accessors.hpp"

static void WriteU32(std::uint8_t* p, std::uint32_t v) {
    std::memcpy(p, &v, sizeof(v));
}

int main() {
    using L = socom::recomp::scus97134::cseal_weapon::EquipmentLayout;
    std::array<std::uint8_t, 0x900> e{};

    WriteU32(e.data() + L::SlotCount, 3);
    WriteU32(e.data() + L::WeaponSlots + 0*4, 0x11111111);
    WriteU32(e.data() + L::WeaponSlots + 1*4, 0x22222222);
    WriteU32(e.data() + L::WeaponSlots + 2*4, 0x33333333);
    WriteU32(e.data() + L::CurrentSlot, 1);

    using namespace socom::recomp::native_cseal_weapon;

    assert(GetCurrentWeaponGuestPtr(e.data()) == 0x22222222);
    assert(GetWeaponWrappedGuestPtr(e.data(), 0) == 0x11111111);
    assert(GetWeaponWrappedGuestPtr(e.data(), 1) == 0x22222222);
    assert(GetWeaponWrappedGuestPtr(e.data(), 2) == 0x33333333);
    assert(GetWeaponWrappedGuestPtr(e.data(), 3) == 0); // retail sentinel
    assert(GetWeaponWrappedGuestPtr(e.data(), 4) == 0x22222222);
    assert(GetWeaponWrappedGuestPtr(e.data(), -1) == 0x33333333);

    WriteU32(e.data() + L::SlotCount, 0);
    assert(GetWeaponWrappedGuestPtr(e.data(), 0) == 0);

    std::cout << "CSeal weapon native accessor smoke test passed.\n";
    return 0;
}
