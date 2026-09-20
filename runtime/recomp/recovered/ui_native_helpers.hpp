#pragma once
#include "../recomp_dispatch.hpp"
#include "../scus_971_34_addresses.hpp"
#include <cstdint>

namespace socom::retail::ui::native {

// Exact translations of tiny retail template helpers used by SwitchMenu.
// These are intentionally small first wins for address-compatible native
// recompilation: behavior below follows the SCUS_971.34 instructions directly.
inline void VectorEmpty(socom::ee::CpuState& cpu, socom::ee::GuestMemory& mem) {
    const auto object = cpu.a0().u32();
    const auto count = mem.read<std::uint32_t>(object + 0x04u);
    cpu.v0().set_u32(count == 0u ? 1u : 0u);
}

inline void VectorPopCount(socom::ee::CpuState& cpu, socom::ee::GuestMemory& mem) {
    const auto object = cpu.a0().u32();
    auto count = mem.read<std::uint32_t>(object + 0x04u);
    --count; // Retail helper performs no empty check either.
    mem.write<std::uint32_t>(object + 0x04u, count);
}

inline void VectorBack(socom::ee::CpuState& cpu, socom::ee::GuestMemory& mem) {
    const auto object = cpu.a0().u32();
    const auto count = mem.read<std::uint32_t>(object + 0x04u);
    const auto data = mem.read<std::uint32_t>(object + 0x08u);
    cpu.v0().set_u32(data + count * 4u - 4u);
}

inline void BindRecoveredUiHelpers(socom::ee::NativeDispatch& dispatch) {
    dispatch.bind(kUiVectorEmpty,     "ui_vector_empty_001D4930", &VectorEmpty);
    dispatch.bind(kUiVectorPopCount,  "ui_vector_pop_count_001D4940", &VectorPopCount);
    dispatch.bind(kUiVectorBack,      "ui_vector_back_001D4950", &VectorBack);
    dispatch.bind(kUiVectorBackConst, "ui_vector_back_const_001D4990", &VectorBack);
    dispatch.bind(kUiVectorEmptyAlt,  "ui_vector_empty_alt_001D6150", &VectorEmpty);
}

} // namespace socom::retail::ui::native
