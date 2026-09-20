#pragma once
#include "ee_guest.hpp"
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace socom::ee {

using NativeFunction = void(*)(CpuState&, GuestMemory&);

struct FunctionBinding {
    const char* name{};
    NativeFunction fn{};
};

class NativeDispatch {
public:
    void bind(std::uint32_t guestAddress, const char* name, NativeFunction fn) {
        bindings_[guestAddress] = FunctionBinding{name, fn};
    }

    const FunctionBinding* find(std::uint32_t guestAddress) const noexcept {
        auto it = bindings_.find(guestAddress);
        return it == bindings_.end() ? nullptr : &it->second;
    }

    void call(std::uint32_t guestAddress, CpuState& cpu, GuestMemory& mem) const {
        auto* b = find(guestAddress);
        if (!b || !b->fn) {
            char buf[96]{};
            std::snprintf(buf, sizeof(buf), "unrecompiled EE function 0x%08X", guestAddress);
            throw std::runtime_error(buf);
        }
        cpu.pc = guestAddress;
        b->fn(cpu, mem);
        cpu.gpr[0] = {}; // R5900 $zero remains hard-wired.
    }

private:
    std::unordered_map<std::uint32_t, FunctionBinding> bindings_;
};

} // namespace socom::ee
