#pragma once
#include <cstdint>

namespace socom::recomp::scus97134::retail_chain {

inline constexpr std::uint32_t kUiArgResolver       = 0x001CB780;
inline constexpr std::uint32_t kUiArgPoolLookup     = 0x001C5270;
inline constexpr std::uint32_t kUiArgTableResolve   = 0x001C3AF0;
inline constexpr std::uint32_t kUiSetMission        = 0x001D61A0;

inline constexpr std::uint32_t kMissionFrame        = 0x001F8EB0;
inline constexpr std::uint32_t kMissionTickWrapper  = 0x001F94A0;

inline constexpr std::uint32_t kCurrentUiContext    = 0x004B31D8;
inline constexpr std::uint32_t kUiArgPool           = 0x004B31A0;
inline constexpr std::uint32_t kMissionSingleton    = 0x004D4880;

struct UiArgPoolLayout {
    static constexpr std::uint32_t Records = 0x1C;
    static constexpr std::uint32_t Count = 0x20;
    static constexpr std::uint32_t RecordSize = 0x30;
};

struct UiCommandContextLayout {
    static constexpr std::uint32_t ArgumentTable = 0x24;
    static constexpr std::uint32_t PoolSelector = 0x34;
};

} // namespace socom::recomp::scus97134::retail_chain
