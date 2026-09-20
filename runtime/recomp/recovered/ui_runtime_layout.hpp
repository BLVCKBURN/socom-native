#pragma once
#include <cstddef>
#include <cstdint>

namespace socom::retail::ui {

// SwitchMenu indexes these records as index * 0x34, writes a 32-bit type at
// +0x00, then copies the screen/menu string at +0x04.
struct MenuCommandRecord {
    std::uint32_t type{};
    char text[48]{};
};
static_assert(sizeof(MenuCommandRecord) == 0x34);

// Template-style small vector layout proven by retail helpers 0x001D4930,
// 0x001D4940, 0x001D4950 and 0x001D4990. The meaning of +0x00 has not yet
// been established; +0x04 is count and +0x08 is a pointer to 32-bit entries.
struct GuestVector32Header {
    std::uint32_t unknown00{};
    std::uint32_t count{};
    std::uint32_t data{};
};
static_assert(offsetof(GuestVector32Header, count) == 0x04);
static_assert(offsetof(GuestVector32Header, data) == 0x08);

inline constexpr std::uint32_t kCommandContextPointer = 0x004B31D8u;
inline constexpr std::uint32_t kMenuStorageBase       = 0x004B3450u;
inline constexpr std::uint32_t kRecordArrayOffset     = 0x0000u;
inline constexpr std::uint32_t kRecordCapacity        = 40u;
inline constexpr std::uint32_t kPendingCountOffset    = 0x0820u; // medium-confidence semantic name
inline constexpr std::uint32_t kRecordIndexOffset     = 0x0824u; // medium-confidence semantic name
inline constexpr std::uint32_t kStackAOffset          = 0x0828u;
inline constexpr std::uint32_t kStackBOffset          = 0x0E34u;

// Fields of the object pointed to by 0x004B31D8 used by command argument
// resolution. Names remain descriptive until the original class type is known.
inline constexpr std::uint32_t kContextDynamicInputSource = 0x0018u;
inline constexpr std::uint32_t kContextArgumentStorage     = 0x0024u;
inline constexpr std::uint32_t kContextHandle              = 0x0034u; // uint16_t

} // namespace socom::retail::ui
