#pragma once
#include <cstdint>

namespace socom::recomp::scus97134::cseal {

inline constexpr std::uint32_t kObjectSize = 0x1180;
inline constexpr std::uint32_t kFactory = 0x002955A0;
inline constexpr std::uint32_t kConstructor = 0x0025D5E0;
inline constexpr std::uint32_t kDestructor = 0x0025CD50;
inline constexpr std::uint32_t kSpawnWrapper = 0x00251F10;

inline constexpr std::uint32_t kPrimaryVtable = 0x004899F0;
inline constexpr std::uint32_t kSecondaryVtable = 0x00489A4C;

struct Layout {
    static constexpr std::uint32_t PrimaryVtable = 0x000;
    static constexpr std::uint32_t EntityNode = 0x028;
    static constexpr std::uint32_t Controller = 0x0C0;
    static constexpr std::uint32_t SecondaryVtable = 0x110;
    static constexpr std::uint32_t State = 0x160;
    static constexpr std::uint32_t StateSecondary = 0x161;
    static constexpr std::uint32_t StateFlags = 0x162;
    static constexpr std::uint32_t StateParameter = 0x164;
    static constexpr std::uint32_t CreationDescriptor = 0x478;
    static constexpr std::uint32_t GameplaySubsystem = 0x530;
    static constexpr std::uint32_t RuntimeController = 0xDC8;
    static constexpr std::uint32_t PeerBacklink = 0xDCC;
    static constexpr std::uint32_t Record = 0xDE0;
};

inline constexpr std::uint32_t kStateDispatchA = 0x00252050;
inline constexpr std::uint32_t kRecordDestroy = 0x00251770;
inline constexpr std::uint32_t kRecordCreateOrUpdate = 0x002517A0;
inline constexpr std::uint32_t kRecordListRemove = 0x002526C0;
inline constexpr std::uint32_t kRecordAllocate = 0x002527D0;

} // namespace socom::recomp::scus97134::cseal
