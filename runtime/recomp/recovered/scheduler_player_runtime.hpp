#pragma once
#include <cstdint>

namespace socom::recomp::scus97134::scheduler_player {

inline constexpr std::uint32_t kSchedulerObject = 0x00527130;
inline constexpr std::uint32_t kSchedulerRegister = 0x0030B4F0;

struct SchedulerNodeLayout {
    static constexpr std::uint32_t Enabled = 0x04;
    static constexpr std::uint32_t Priority = 0x08;
    static constexpr std::uint32_t Callback = 0x0C;
    static constexpr std::uint32_t Context = 0x10;
    static constexpr std::uint32_t Size = 0x1C;
};

inline constexpr std::uint32_t kEeGp = 0x00494670;
inline constexpr std::uint32_t kCurrentPlayerGlobal = 0x0048D548;
inline constexpr std::uint32_t kCurrentPlayerGetter = 0x00200010;
inline constexpr std::uint32_t kCurrentPlayerSetter = 0x00200020;

inline constexpr std::uint32_t kMissionTickWrapper = 0x001F94A0;
inline constexpr std::uint32_t kMissionTickRegistration = 0x001FA79C;
inline constexpr std::uint32_t kMissionFrame = 0x001F8EB0;

// Corrected retail scheduler callbacks.
// Previous checkpoint accidentally used 0x003490xx; those land inside an unrelated function.
inline constexpr std::uint32_t kWeaponPostTick = 0x003390A0;
inline constexpr std::uint32_t kWeaponPreTick = 0x003390E0;

inline constexpr std::uint32_t kSealNeighborhoodTick = 0x00250C90;
inline constexpr std::uint32_t kSealNeighborhoodCleanup = 0x00250E30;

} // namespace socom::recomp::scus97134::scheduler_player
