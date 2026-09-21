#pragma once
#include <cstdint>
#include <cstring>

namespace socom::recomp::scus97134::cseal_weapon {

inline constexpr std::uint32_t kEquipmentOffset = 0x530;

// Retail CSeal equipment subobject layout.
struct EquipmentLayout {
    static constexpr std::uint32_t WeaponSlots = 0x0DC;       // 30 x guest pointers
    static constexpr std::uint32_t SlotCompanion = 0x154;     // per-slot companion pointers/data
    static constexpr std::uint32_t CurrentSlot = 0x814;
    static constexpr std::uint32_t SlotCount = 0x81C;
    static constexpr std::uint32_t OwnerSeal = 0x820;
    static constexpr std::uint32_t SelectableCount = 0x828;
    static constexpr std::uint32_t SelectableIndices = 0x82C;
    static constexpr std::uint32_t MaxWeaponSlots = 30;
};

inline constexpr std::uint32_t kGetWeaponWrapped = 0x002BB5E0;
inline constexpr std::uint32_t kGetCurrentWeapon = 0x002BB680;
inline constexpr std::uint32_t kPopulateWeaponSlot = 0x002B6DF0;
inline constexpr std::uint32_t kResolveWeaponZoomData = 0x002BDBB0;

// CSeal attachment/name handles recovered from the constructor.
struct SealAttachmentLayout {
    static constexpr std::uint32_t GenericWeapon = 0x2D0;
    static constexpr std::uint32_t Rifle = 0x2D4;
    static constexpr std::uint32_t Pistol = 0x2D8;
    static constexpr std::uint32_t Grenade = 0x2DC;
    static constexpr std::uint32_t AttachmentRecordArray = 0xFB4;
};

// Zoom-control state embedded directly in CSeal.
struct ZoomLayout {
    static constexpr std::uint32_t State = 0x160;
    static constexpr std::uint32_t PreviousState = 0x161;
    static constexpr std::uint32_t Dirty = 0x162;
    static constexpr std::uint32_t Scale = 0x164;
};

} // namespace socom::recomp::scus97134::cseal_weapon
