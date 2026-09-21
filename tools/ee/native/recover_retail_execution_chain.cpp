#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::uint16_t read_u16(const std::vector<std::uint8_t>& data, std::size_t off) {
    if (off + 2 > data.size()) throw std::runtime_error("read_u16 outside file");
    return static_cast<std::uint16_t>(data[off]) |
           (static_cast<std::uint16_t>(data[off + 1]) << 8);
}

static std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t off) {
    if (off + 4 > data.size()) throw std::runtime_error("read_u32 outside file");
    return static_cast<std::uint32_t>(data[off]) |
           (static_cast<std::uint32_t>(data[off + 1]) << 8) |
           (static_cast<std::uint32_t>(data[off + 2]) << 16) |
           (static_cast<std::uint32_t>(data[off + 3]) << 24);
}

struct LoadSegment {
    std::uint32_t offset{};
    std::uint32_t vaddr{};
    std::uint32_t filesz{};
    std::uint32_t memsz{};
    std::uint32_t flags{};
    std::uint32_t align{};
};

struct ElfImage {
    std::vector<std::uint8_t> bytes;
    std::vector<LoadSegment> loads;
};

static ElfImage load_elf(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open ELF: " + path.string());
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) throw std::runtime_error("empty ELF");
    f.seekg(0, std::ios::beg);

    ElfImage image;
    image.bytes.resize(static_cast<std::size_t>(sz));
    f.read(reinterpret_cast<char*>(image.bytes.data()), static_cast<std::streamsize>(image.bytes.size()));
    if (!f) throw std::runtime_error("failed reading ELF");

    const auto& d = image.bytes;
    if (d.size() < 0x34 || d[0] != 0x7F || d[1] != 'E' || d[2] != 'L' || d[3] != 'F')
        throw std::runtime_error("not an ELF file");
    if (d[4] != 1 || d[5] != 1)
        throw std::runtime_error("expected ELF32 little-endian");

    const std::uint32_t phoff = read_u32(d, 0x1C);
    const std::uint16_t phentsize = read_u16(d, 0x2A);
    const std::uint16_t phnum = read_u16(d, 0x2C);

    for (std::uint16_t i = 0; i < phnum; ++i) {
        const std::size_t o = static_cast<std::size_t>(phoff) + static_cast<std::size_t>(i) * phentsize;
        if (o + 32 > d.size()) throw std::runtime_error("program header outside ELF");
        const std::uint32_t type = read_u32(d, o + 0x00);
        if (type != 1) continue; // PT_LOAD
        LoadSegment s;
        s.offset = read_u32(d, o + 0x04);
        s.vaddr  = read_u32(d, o + 0x08);
        s.filesz = read_u32(d, o + 0x10);
        s.memsz  = read_u32(d, o + 0x14);
        s.flags  = read_u32(d, o + 0x18);
        s.align  = read_u32(d, o + 0x1C);
        if (static_cast<std::uint64_t>(s.offset) + s.filesz > d.size())
            throw std::runtime_error("PT_LOAD file range outside ELF");
        image.loads.push_back(s);
    }
    if (image.loads.empty()) throw std::runtime_error("no PT_LOAD segment found");
    return image;
}

static std::size_t va_to_off(std::uint32_t va, const std::vector<LoadSegment>& loads) {
    for (const auto& s : loads) {
        if (va >= s.vaddr && static_cast<std::uint64_t>(va) < static_cast<std::uint64_t>(s.vaddr) + s.filesz)
            return static_cast<std::size_t>(s.offset) + (va - s.vaddr);
    }
    std::ostringstream oss;
    oss << "VA 0x" << std::hex << std::uppercase << va << " is not file-backed";
    throw std::runtime_error(oss.str());
}

static std::uint32_t word_at(const ElfImage& elf, std::uint32_t va) {
    return read_u32(elf.bytes, va_to_off(va, elf.loads));
}

static bool is_jal(std::uint32_t w) { return (w >> 26) == 3u; }
static std::uint32_t jal_target(std::uint32_t va, std::uint32_t w) {
    return ((va + 4u) & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2u);
}

static std::string hex8(std::uint32_t v) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << v;
    return oss.str();
}
static std::string hexn(std::uint32_t v) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << v;
    return oss.str();
}

static std::string csv_escape(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

struct FocusRow {
    const char* symbol;
    std::uint32_t start;
    std::uint32_t end;
    const char* note;
};

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: socom_retail_chain_analyzer SCUS_971.34 OUT_DIR\n";
            return 2;
        }
        const fs::path elfPath = fs::path(argv[1]);
        const fs::path outDir = fs::path(argv[2]);
        fs::create_directories(outDir);

        const ElfImage elf = load_elf(elfPath);
        const auto& primary = elf.loads.front();

        {
            std::ofstream f(outDir / "elf_mapping.csv", std::ios::binary);
            f << "segment,file_offset,vaddr,filesz,memsz,flags,align\n";
            for (std::size_t i = 0; i < elf.loads.size(); ++i) {
                const auto& s = elf.loads[i];
                f << i << ',' << hexn(s.offset) << ',' << hex8(s.vaddr) << ','
                  << hexn(s.filesz) << ',' << hexn(s.memsz) << ',' << hexn(s.flags) << ',' << hexn(s.align) << "\n";
            }
        }

        const std::array<FocusRow, 6> rows{{
            {"ui_arg_resolver",      0x001CB780u, 0x001CB7D4u, "real prologue; selects 0x30-byte pool entry and resolves argument through command-context +0x24"},
            {"ui_arg_pool_lookup",   0x001C5270u, 0x001C52B0u, "pool lookup: data pointer +0x1C, count +0x20, stride 0x30"},
            {"ui_arg_table_resolve", 0x001C3AF0u, 0x001C3B40u, "bounds-checks argument index against table count and resolves an 8-byte record"},
            {"ui_SetMission",        0x001D61A0u, 0x001D6290u, "real command prologue; calls 0x001CB780 at 0x001D61B8"},
            {"mission_frame",        0x001F8EB0u, 0x001F9208u, "real CMission frame prologue"},
            {"mission_tick_wrapper", 0x001F94A0u, 0x001F94C0u, "thin scheduler callback; calls 0x001F8EB0 and returns zero"},
        }};

        {
            std::ofstream f(outDir / "corrected_focus_functions.csv", std::ios::binary);
            f << "symbol,start,end,size,note\n";
            for (const auto& r : rows) {
                f << r.symbol << ',' << hex8(r.start) << ',' << hex8(r.end) << ',' << (r.end - r.start) << ',' << csv_escape(r.note) << "\n";
            }
        }

        const std::array<FocusRow, 4> callRanges{{
            {"ui_arg_resolver",      0x001CB780u, 0x001CB7D4u, ""},
            {"ui_SetMission",        0x001D61A0u, 0x001D6290u, ""},
            {"mission_frame",        0x001F8EB0u, 0x001F9208u, ""},
            {"mission_tick_wrapper", 0x001F94A0u, 0x001F94C0u, ""},
        }};

        {
            std::ofstream f(outDir / "corrected_call_edges.csv", std::ios::binary);
            f << "caller,callsite,target\n";
            for (const auto& r : callRanges) {
                for (std::uint32_t va = r.start; va < r.end; va += 4) {
                    const std::uint32_t w = word_at(elf, va);
                    if (!is_jal(w)) continue;
                    f << r.symbol << ',' << hex8(va) << ',' << hex8(jal_target(va, w)) << "\n";
                }
            }
        }

        {
            std::ofstream f(outDir / "ui_argument_layout.csv", std::ios::binary);
            f << "object,offset,meaning,confidence\n";
            f << "global_arg_pool,0x004B31A0,global argument pool object,high\n";
            f << "arg_pool,+0x1C,pointer to contiguous records,high\n";
            f << "arg_pool,+0x20,record count,high\n";
            f << "arg_pool,record_size,0x30 bytes,high\n";
            f << "command_context,+0x24,secondary argument/index table,high\n";
            f << "command_context,+0x34,pool record selector used by 0x001CB780,high\n";
            f << "current_ui_context,0x004B31D8,global pointer passed by SetMission,high\n";
        }

        std::cout << "Primary PT_LOAD: file offset " << hexn(primary.offset)
                  << ", VA " << hex8(primary.vaddr) << "\n";
        std::cout << "Corrected execution-chain reports written to " << outDir.string() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
