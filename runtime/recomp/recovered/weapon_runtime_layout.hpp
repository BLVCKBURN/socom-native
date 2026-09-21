#pragma once
#include <cstdint>

namespace socom::recomp::scus97134::weapon_runtime {

inline constexpr std::uint32_t kProjectileTypeInfo = 0x00470A50;
inline constexpr std::uint32_t kProjectileVtable = 0x0048A9B0;
inline constexpr std::uint32_t kProjectileVirtualPreTick = 0x00333540;
inline constexpr std::uint32_t kProjectileVirtualPostTick = 0x00333070;

inline constexpr std::uint32_t kWeaponTypeInfo = 0x00470EF0;
inline constexpr std::uint32_t kWeaponVtable = 0x0048A9C0;
inline constexpr std::uint32_t kWeaponVirtualDestructor = 0x0033A1C0;
inline constexpr std::uint32_t kWeaponVirtualAction = 0x00339E30;

inline constexpr std::uint32_t kWeaponPostTick = 0x003390A0;
inline constexpr std::uint32_t kWeaponPreTick = 0x003390E0;
inline constexpr std::uint32_t kWeaponManagerPreTick = 0x00335110;
inline constexpr std::uint32_t kWeaponManagerPostTick = 0x00334F90;

inline constexpr std::uint32_t kWeaponManagerGlobal = 0x0048E568;
inline constexpr std::uint32_t kWeaponTickDisableFlag = 0x0048E574;

struct DefinitionLayout {
    static constexpr std::uint32_t Field04 = 0x04;
    static constexpr std::uint32_t Field08 = 0x08;
    static constexpr std::uint32_t Field24 = 0x24;
    static constexpr std::uint32_t Field28 = 0x28;

    static constexpr std::uint32_t Value38 = 0x38;
    static constexpr std::uint32_t Value3C = 0x3C;
    static constexpr std::uint32_t Value40 = 0x40;
    static constexpr std::uint32_t Value44 = 0x44;

    // Confirmed parser mapping in SCUS_971.34.
    static constexpr std::uint32_t FireWait = 0x48;

    // Used throughout the SEAL/equipment path to select rifle/pistol/grenade/etc.
    static constexpr std::uint32_t Type = 0x1D4;
};

inline constexpr std::uint32_t kGetFloat40 = 0x003397D0;
inline constexpr std::uint32_t kGetFloat44 = 0x003397E0;
inline constexpr std::uint32_t kGetFloat3C = 0x003397F0;
inline constexpr std::uint32_t kGetFloat38 = 0x00339800;
inline constexpr std::uint32_t kGetByte34 = 0x00339810;
inline constexpr std::uint32_t kGetField28 = 0x00339820;
inline constexpr std::uint32_t kGetField24 = 0x00339830;
inline constexpr std::uint32_t kGetField08 = 0x00339840;
inline constexpr std::uint32_t kGetField04 = 0x00339850;

inline constexpr std::uint32_t kTypeCanonicalize = 0x00338D30;
inline constexpr std::uint32_t kGetCanonicalType = 0x00338F80;
inline constexpr std::uint32_t kIsSpecialType = 0x00338F90;

} // namespace socom::recomp::scus97134::weapon_runtime
