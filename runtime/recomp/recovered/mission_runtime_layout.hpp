#pragma once
#include <cstdint>

namespace socom::retail::mission {

// Terminal state values observed directly in 0x001F8EB0 -> 0x001F8580 calls.
enum class ResultState : std::uint8_t {
    Complete = 2,
    Failure  = 3,
    Abort    = 4,
    Timeout  = 5,
};

// Offsets in the retail CMission singleton at 0x004D4880.
inline constexpr std::uint32_t kResultState             = 0x0018u;
inline constexpr std::uint32_t kMissionNamePtr          = 0x001Cu;
inline constexpr std::uint32_t kMissionNameBuffer       = 0x0020u;
inline constexpr std::uint32_t kElapsedTime             = 0x0060u;
inline constexpr std::uint32_t kResultDeadline          = 0x0064u;
inline constexpr std::uint32_t kModeTimeAccumulator     = 0x0068u; // medium confidence
inline constexpr std::uint32_t kModeTimeScale           = 0x006Cu; // medium confidence
inline constexpr std::uint32_t kCountdownOrDelay        = 0x007Cu; // medium confidence
inline constexpr std::uint32_t kArray0Count             = 0x0084u; // semantic role not fully named
inline constexpr std::uint32_t kArray0Ptr               = 0x0088u;
inline constexpr std::uint32_t kObjectiveContainer      = 0x0098u; // medium confidence
inline constexpr std::uint32_t kObjectiveCount          = 0x009Cu;
inline constexpr std::uint32_t kObjectiveEntriesPtr     = 0x00A0u;
inline constexpr std::uint32_t kObjectiveRefreshTimer   = 0x00A8u;
inline constexpr std::uint32_t kMissionStateFlag        = 0x00ACu; // medium confidence
inline constexpr std::uint32_t kAiParamsBlock           = 0x00B4u;
inline constexpr std::uint32_t kMissionCompleteValve    = 0x00F4u;
inline constexpr std::uint32_t kMissionFailureValve     = 0x00F8u;
inline constexpr std::uint32_t kMissionAbortValve       = 0x00FCu;
inline constexpr std::uint32_t kMissionTimeoutValve     = 0x0100u;
inline constexpr std::uint32_t kMissionReplayValve      = 0x0104u;
inline constexpr std::uint32_t kRuntimeListOrHead       = 0x0590u; // medium confidence
inline constexpr std::uint32_t kActiveGameplayObject    = 0x0594u; // medium confidence
inline constexpr std::uint32_t kInitFlag                = 0x0598u; // medium confidence
inline constexpr std::uint32_t kLoadingScreenAssets     = 0x05C4u;
inline constexpr std::uint32_t kRuntimeContainerA       = 0x05C8u; // medium confidence
inline constexpr std::uint32_t kRuntimeContainerB       = 0x05D4u; // medium confidence
inline constexpr std::uint32_t kSatchelTimer            = 0x05E4u;

// ai_params block beginning at CMission + 0xB4. Mapping follows the ordered
// named lookups in 0x001F94C0; these names are data keys observed in the ELF.
namespace ai_params {
inline constexpr std::uint32_t kWeatherFactor       = 0x00u;
inline constexpr std::uint32_t kUnknown04           = 0x04u; // default 4; fireteam command path influences behavior
inline constexpr std::uint32_t kRespawnRangeCached  = 0x08u;
inline constexpr std::uint32_t kRespawnTimeCached   = 0x0Cu;
inline constexpr std::uint32_t kRespawnTime         = 0x10u;
inline constexpr std::uint32_t kRespawnFade         = 0x14u;
inline constexpr std::uint32_t kRespawnRange        = 0x18u;
inline constexpr std::uint32_t kLightScale          = 0x1Cu;
inline constexpr std::uint32_t kLightInside         = 0x20u;
inline constexpr std::uint32_t kShadowScale         = 0x24u;
inline constexpr std::uint32_t kActiveLimit         = 0x28u;
inline constexpr std::uint32_t kNoSnoozeLimit       = 0x2Cu;
inline constexpr std::uint32_t kBlockSize           = 0x30u;
}

} // namespace socom::retail::mission
