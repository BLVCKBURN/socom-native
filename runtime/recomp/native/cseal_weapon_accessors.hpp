#pragma once
#include <cstdint>
#include <cstring>
#include "../recovered/cseal_weapon_layout.hpp"

namespace socom::recomp::native_cseal_weapon {

inline std::uint32_t ReadU32(const std::uint8_t* p) {
    std::uint32_t v{};
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// Native translation of retail 0x002BB680.
inline std::uint32_t GetCurrentWeaponGuestPtr(const std::uint8_t* equipment) {
    using L = scus97134::cseal_weapon::EquipmentLayout;
    const std::uint32_t index = ReadU32(equipment + L::CurrentSlot);
    if (index >= L::MaxWeaponSlots)
        return 0;
    return ReadU32(equipment + L::WeaponSlots + index * 4u);
}

// Native translation of retail 0x002BB5E0.
// Retail behavior:
//   - count == 0 => null
//   - requested index == count => sentinel/no weapon => null
//   - indexes above/below range wrap modulo count
inline std::uint32_t GetWeaponWrappedGuestPtr(
    const std::uint8_t* equipment,
    std::int32_t requestedIndex)
{
    using L = scus97134::cseal_weapon::EquipmentLayout;

    const std::int32_t count =
        static_cast<std::int32_t>(ReadU32(equipment + L::SlotCount));

    if (count <= 0)
        return 0;

    if (requestedIndex == count)
        return 0;

    std::int32_t index = requestedIndex;

    while (index >= count)
        index -= count;

    while (index < 0)
        index += count;

    if (index < 0 ||
        static_cast<std::uint32_t>(index) >= L::MaxWeaponSlots)
        return 0;

    return ReadU32(
        equipment + L::WeaponSlots +
        static_cast<std::uint32_t>(index) * 4u);
}

} // namespace socom::recomp::native_cseal_weapon
