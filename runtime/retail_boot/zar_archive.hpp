#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace socom::menu_boot {

#pragma pack(push,1)
struct ZarHeader {
    std::uint32_t unknown00;
    std::uint32_t nodeCount;
    std::uint32_t stringBlockSize;
    std::uint32_t preferredBase;
    std::uint32_t alignment;
    std::uint8_t unknown14[0x40];
    std::uint32_t payloadSize;
    std::uint32_t unknown58;
    std::uint32_t unknown5C;
    std::uint32_t version;
};
struct ZarNodeDisk {
    std::uint32_t nameAddress;
    std::uint32_t dataOffset;
    std::uint32_t dataSize;
    std::uint32_t childCount;
};
#pragma pack(pop)

static_assert(sizeof(ZarHeader) == 0x64);
static_assert(sizeof(ZarNodeDisk) == 0x10);

inline std::size_t AlignUp(std::size_t v, std::size_t a) {
    if (!a) throw std::runtime_error("zero ZAR alignment");
    const auto r = v % a;
    return r ? v + (a-r) : v;
}

class ZarArchive {
public:
    explicit ZarArchive(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open archive: " + path);
        f.seekg(0, std::ios::end);
        const auto end = f.tellg();
        f.seekg(0, std::ios::beg);
        if (end < static_cast<std::streamoff>(sizeof(ZarHeader)))
            throw std::runtime_error("archive too small");
        bytes_.resize(static_cast<std::size_t>(end));
        f.read(reinterpret_cast<char*>(bytes_.data()),
               static_cast<std::streamsize>(bytes_.size()));
        if (!f) throw std::runtime_error("failed reading archive");

        std::memcpy(&header_, bytes_.data(), sizeof(header_));
        if (header_.version != 0x00020002)
            throw std::runtime_error("unexpected ZAR version");

        stringOffset_ = sizeof(ZarHeader);
        nodeOffset_ = stringOffset_ + header_.stringBlockSize;
        const std::size_t nodeBytes =
            static_cast<std::size_t>(header_.nodeCount) * sizeof(ZarNodeDisk);
        if (nodeOffset_ + nodeBytes > bytes_.size())
            throw std::runtime_error("ZAR node table outside file");

        payloadOffset_ = AlignUp(nodeOffset_ + nodeBytes, header_.alignment);
        if (payloadOffset_ + header_.payloadSize != bytes_.size())
            throw std::runtime_error("ZAR payload size mismatch");

        nodes_.resize(header_.nodeCount);
        std::memcpy(nodes_.data(), bytes_.data() + nodeOffset_, nodeBytes);
    }

    std::vector<std::uint8_t> ReadLeaf(const std::string& wanted) const {
        for (const auto& n : nodes_) {
            if (ResolveName(n.nameAddress) != wanted)
                continue;
            const std::uint64_t end =
                static_cast<std::uint64_t>(n.dataOffset) + n.dataSize;
            if (end > header_.payloadSize)
                throw std::runtime_error("ZAR leaf outside payload: " + wanted);
            return std::vector<std::uint8_t>(
                bytes_.begin() + payloadOffset_ + n.dataOffset,
                bytes_.begin() + payloadOffset_ + n.dataOffset + n.dataSize);
        }
        throw std::runtime_error("leaf not found: " + wanted);
    }

private:
    std::string ResolveName(std::uint32_t addr) const {
        if (!addr) return "<root>";
        if (addr < header_.preferredBase) return "<bad>";
        const std::uint64_t rel =
            static_cast<std::uint64_t>(addr) - header_.preferredBase;
        if (rel >= header_.stringBlockSize) return "<bad>";
        const std::size_t start = stringOffset_ + static_cast<std::size_t>(rel);
        const std::size_t limit = stringOffset_ + header_.stringBlockSize;
        std::size_t end = start;
        while (end < limit && bytes_[end]) ++end;
        return std::string(
            reinterpret_cast<const char*>(bytes_.data()+start), end-start);
    }

    std::vector<std::uint8_t> bytes_;
    ZarHeader header_{};
    std::vector<ZarNodeDisk> nodes_;
    std::size_t stringOffset_{};
    std::size_t nodeOffset_{};
    std::size_t payloadOffset_{};
};

} // namespace socom::menu_boot
