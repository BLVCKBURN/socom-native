#pragma once
#include <cstdint>
#include <cstring>
#include "../recovered/weapon_runtime_layout.hpp"

namespace socom::recomp::native_weapon {

inline std::uint32_t ReadU32(const std::uint8_t* p) {
    std::uint32_t v{};
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline float ReadF32(const std::uint8_t* p) {
    float v{};
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline std::int8_t ReadS8(const std::uint8_t* p) {
    return static_cast<std::int8_t>(*p);
}

inline float GetFloat40(const std::uint8_t* weapon) {
    return ReadF32(weapon + scus97134::weapon_runtime::DefinitionLayout::Value40);
}
inline float GetFloat44(const std::uint8_t* weapon) {
    return ReadF32(weapon + scus97134::weapon_runtime::DefinitionLayout::Value44);
}
inline float GetFloat3C(const std::uint8_t* weapon) {
    return ReadF32(weapon + scus97134::weapon_runtime::DefinitionLayout::Value3C);
}
inline float GetFloat38(const std::uint8_t* weapon) {
    return ReadF32(weapon + scus97134::weapon_runtime::DefinitionLayout::Value38);
}
inline std::int8_t GetByte34(const std::uint8_t* weapon) {
    return ReadS8(weapon + 0x34);
}
inline std::uint32_t GetField28(const std::uint8_t* weapon) {
    return ReadU32(weapon + scus97134::weapon_runtime::DefinitionLayout::Field28);
}
inline std::uint32_t GetField24(const std::uint8_t* weapon) {
    return ReadU32(weapon + scus97134::weapon_runtime::DefinitionLayout::Field24);
}
inline std::uint32_t GetField08(const std::uint8_t* weapon) {
    return ReadU32(weapon + scus97134::weapon_runtime::DefinitionLayout::Field08);
}
inline std::uint32_t GetField04(const std::uint8_t* weapon) {
    return ReadU32(weapon + scus97134::weapon_runtime::DefinitionLayout::Field04);
}
inline float GetFireWait(const std::uint8_t* weapon) {
    return ReadF32(weapon + scus97134::weapon_runtime::DefinitionLayout::FireWait);
}
inline std::uint8_t GetType(const std::uint8_t* weapon) {
    return *(weapon + scus97134::weapon_runtime::DefinitionLayout::Type);
}

} // namespace socom::recomp::native_weapon
