#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace socom::ee {

struct alignas(16) Gpr128 {
    std::uint64_t lo{};
    std::uint64_t hi{};
    std::uint32_t u32() const noexcept { return static_cast<std::uint32_t>(lo); }
    std::int32_t s32() const noexcept { return static_cast<std::int32_t>(u32()); }
    void set_u32(std::uint32_t v) noexcept { lo = v; hi = 0; }
    void set_u64(std::uint64_t v) noexcept { lo = v; hi = 0; }
};

struct CpuState {
    std::array<Gpr128, 32> gpr{};
    Gpr128 hi{};
    Gpr128 lo{};
    std::array<float, 32> fpr{};
    std::uint32_t fcr31{};
    std::uint32_t pc{};
    std::uint32_t sa{};

    Gpr128& v0() noexcept { return gpr[2]; }
    Gpr128& a0() noexcept { return gpr[4]; }
    Gpr128& a1() noexcept { return gpr[5]; }
    Gpr128& a2() noexcept { return gpr[6]; }
    Gpr128& a3() noexcept { return gpr[7]; }
    Gpr128& sp() noexcept { return gpr[29]; }
    Gpr128& ra() noexcept { return gpr[31]; }
};

class GuestMemory {
public:
    static constexpr std::uint32_t kBase = 0x00100000u;
    static constexpr std::uint32_t kRetailFileSize = 0x0038D200u;
    static constexpr std::uint32_t kRetailMemorySize = 0x004D6D00u;

    GuestMemory() : bytes_(kRetailMemorySize, 0) {}

    void loadRetailSegment(const std::vector<std::uint8_t>& elf, std::uint32_t fileOffset = 0x80u) {
        if (static_cast<std::uint64_t>(fileOffset) + kRetailFileSize > elf.size())
            throw std::runtime_error("SCUS_971.34 load segment is truncated");
        std::memcpy(bytes_.data(), elf.data() + fileOffset, kRetailFileSize);
    }

    bool mapped(std::uint32_t va, std::size_t size = 1) const noexcept {
        return va >= kBase && static_cast<std::uint64_t>(va - kBase) + size <= bytes_.size();
    }

    std::uint8_t* ptr(std::uint32_t va, std::size_t size = 1) {
        if (!mapped(va, size)) throw std::out_of_range("EE guest pointer outside mapped retail image/BSS");
        return bytes_.data() + (va - kBase);
    }
    const std::uint8_t* ptr(std::uint32_t va, std::size_t size = 1) const {
        if (!mapped(va, size)) throw std::out_of_range("EE guest pointer outside mapped retail image/BSS");
        return bytes_.data() + (va - kBase);
    }

    template<class T> T read(std::uint32_t va) const {
        T v{}; std::memcpy(&v, ptr(va, sizeof(T)), sizeof(T)); return v;
    }
    template<class T> void write(std::uint32_t va, const T& v) {
        std::memcpy(ptr(va, sizeof(T)), &v, sizeof(T));
    }

    std::size_t size() const noexcept { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

} // namespace socom::ee
