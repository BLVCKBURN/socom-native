#include "ui_native_helpers.hpp"
#include "mission_runtime_layout.hpp"
#include "ui_runtime_layout.hpp"
#include <cassert>
#include <cstdint>

int main() {
    using namespace socom;
    ee::GuestMemory mem;
    ee::CpuState cpu{};
    ee::NativeDispatch dispatch;
    retail::ui::native::BindRecoveredUiHelpers(dispatch);

    constexpr std::uint32_t header = ee::GuestMemory::kBase + 0x100u;
    constexpr std::uint32_t data   = ee::GuestMemory::kBase + 0x200u;
    mem.write<std::uint32_t>(header + 0x04u, 0u);
    mem.write<std::uint32_t>(header + 0x08u, data);
    cpu.a0().set_u32(header);
    dispatch.call(retail::kUiVectorEmpty, cpu, mem);
    assert(cpu.v0().u32() == 1u);

    mem.write<std::uint32_t>(header + 0x04u, 3u);
    dispatch.call(retail::kUiVectorBack, cpu, mem);
    assert(cpu.v0().u32() == data + 8u);
    dispatch.call(retail::kUiVectorPopCount, cpu, mem);
    assert(mem.read<std::uint32_t>(header + 0x04u) == 2u);

    static_assert(static_cast<std::uint8_t>(retail::mission::ResultState::Complete) == 2u);
    static_assert(sizeof(retail::ui::MenuCommandRecord) == 0x34u);
    return 0;
}
