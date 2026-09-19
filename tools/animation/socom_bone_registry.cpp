// socom_bone_registry.cpp
// Reconstructs SOCOM 1's runtime bone-name registry from motion.zar.
//
// The PS2 executable's bone ID routine keeps up to 64 names, each in a
// 32-byte slot. Names receive sequential IDs on first registration.
// motion.zar parsing invokes that registration path for animation bone names,
// so iterating clips in archive order and recording first-seen names
// reproduces the registry order.
//
// Build:
//   clang++ -std=c++17 -O2 -Wall -Wextra socom_bone_registry.cpp -o socom_bone_registry
//
// Usage:
//   ./socom_bone_registry motion.zar
//
// Expected for the supplied SOCOM 1 build:
//   0  skel_root
//   1  hips
//   2  aimnodes
//   ...
//   31 rifle_out

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#pragma pack(push, 1)
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

template<typename T>
static T ReadLE(const std::uint8_t* p) {
    T v{};
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static std::size_t AlignUp(std::size_t v, std::size_t a) {
    if (!a) throw std::runtime_error("zero alignment");
    const auto r = v % a;
    return r ? v + (a - r) : v;
}

struct Node {
    std::string name;
    std::uint32_t dataOffset{};
    std::uint32_t dataSize{};
    std::vector<Node> children;
};

class ZarArchive {
public:
    explicit ZarArchive(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open " + path.string());

        in.seekg(0, std::ios::end);
        const auto length = in.tellg();
        in.seekg(0, std::ios::beg);

        if (length < static_cast<std::streamoff>(sizeof(ZarHeader)))
            throw std::runtime_error("file too small");

        bytes_.resize(static_cast<std::size_t>(length));
        in.read(reinterpret_cast<char*>(bytes_.data()),
                static_cast<std::streamsize>(bytes_.size()));
        if (!in) throw std::runtime_error("read failed");

        std::memcpy(&header_, bytes_.data(), sizeof(header_));
        if (header_.version != 0x00020002)
            throw std::runtime_error("unexpected ZAR version");

        stringsOffset_ = sizeof(ZarHeader);
        nodesOffset_ = stringsOffset_ + header_.stringBlockSize;

        const std::uint64_t nodeBytes =
            static_cast<std::uint64_t>(header_.nodeCount) * sizeof(ZarNodeDisk);

        if (nodesOffset_ + nodeBytes > bytes_.size())
            throw std::runtime_error("node table outside file");

        payloadOffset_ = AlignUp(
            static_cast<std::size_t>(nodesOffset_ + nodeBytes),
            header_.alignment);

        if (payloadOffset_ + header_.payloadSize != bytes_.size())
            throw std::runtime_error("payload does not end at EOF");

        disk_.resize(header_.nodeCount);
        std::memcpy(disk_.data(), bytes_.data() + nodesOffset_,
                    static_cast<std::size_t>(nodeBytes));

        std::size_t cursor = 0;
        root_ = Parse(cursor);

        if (cursor != disk_.size())
            throw std::runtime_error("node tree parse mismatch");
    }

    const Node& Root() const { return root_; }

    const std::uint8_t* PayloadAt(std::uint32_t off, std::size_t size) const {
        if (static_cast<std::uint64_t>(off) + size > header_.payloadSize)
            throw std::runtime_error("payload access out of bounds");
        return bytes_.data() + payloadOffset_ + off;
    }

private:
    std::string ResolveName(std::uint32_t address) const {
        if (!address) return "<root>";
        if (address < header_.preferredBase)
            throw std::runtime_error("invalid name address");

        const auto rel =
            static_cast<std::uint64_t>(address) - header_.preferredBase;

        if (rel >= header_.stringBlockSize)
            throw std::runtime_error("name outside string block");

        const std::size_t start =
            stringsOffset_ + static_cast<std::size_t>(rel);

        const std::size_t limit =
            stringsOffset_ + header_.stringBlockSize;

        std::size_t end = start;
        while (end < limit && bytes_[end] != 0) ++end;

        if (end == limit)
            throw std::runtime_error("unterminated node name");

        return std::string(
            reinterpret_cast<const char*>(bytes_.data() + start),
            end - start);
    }

    Node Parse(std::size_t& cursor) {
        if (cursor >= disk_.size())
            throw std::runtime_error("tree overrun");

        const auto d = disk_[cursor++];

        Node n;
        n.name = ResolveName(d.nameAddress);
        n.dataOffset = d.dataOffset;
        n.dataSize = d.dataSize;
        n.children.reserve(d.childCount);

        for (std::uint32_t i = 0; i < d.childCount; ++i)
            n.children.push_back(Parse(cursor));

        return n;
    }

    std::vector<std::uint8_t> bytes_;
    ZarHeader header_{};
    std::vector<ZarNodeDisk> disk_;
    Node root_;
    std::size_t stringsOffset_{};
    std::size_t nodesOffset_{};
    std::size_t payloadOffset_{};
};

static std::vector<std::string>
ReadClipBoneNames(const std::uint8_t* blob, std::size_t size) {
    if (size < 0x20)
        throw std::runtime_error("motion clip too small");

    if (ReadLE<std::uint32_t>(blob + 0x00) != 5)
        throw std::runtime_error("unexpected motion clip type");

    const std::uint8_t boneCount = blob[0x0C];
    const std::uint32_t namesOffset =
        ReadLE<std::uint32_t>(blob + 0x18);
    const std::uint32_t tracksOffset =
        ReadLE<std::uint32_t>(blob + 0x1C);

    if (namesOffset > tracksOffset || tracksOffset > size)
        throw std::runtime_error("bad motion name block");

    std::vector<std::string> names;
    names.reserve(boneCount);

    std::size_t cursor = namesOffset;

    for (std::uint32_t i = 0; i < boneCount; ++i) {
        if (cursor >= tracksOffset)
            throw std::runtime_error("bone name block truncated");

        std::size_t end = cursor;
        while (end < tracksOffset && blob[end] != 0) ++end;

        if (end == tracksOffset)
            throw std::runtime_error("unterminated bone name");

        names.emplace_back(
            reinterpret_cast<const char*>(blob + cursor),
            end - cursor);

        cursor = (end + 1 + 3) & ~std::size_t(3);
    }

    if (cursor != tracksOffset)
        throw std::runtime_error(
            "bone name block does not end at track block");

    return names;
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: socom_bone_registry motion.zar\n";
            return 1;
        }

        ZarArchive archive(argv[1]);

        std::set<std::string> seen;
        std::vector<std::string> registry;

        for (const auto& clip : archive.Root().children) {
            if (clip.dataSize < 0x20)
                continue;

            const auto names = ReadClipBoneNames(
                archive.PayloadAt(clip.dataOffset, clip.dataSize),
                clip.dataSize);

            for (const auto& name : names) {
                if (seen.insert(name).second) {
                    if (name.size() >= 32)
                        throw std::runtime_error(
                            "bone name exceeds runtime 31-character limit");

                    if (registry.size() >= 64)
                        throw std::runtime_error(
                            "registry exceeds runtime 64-bone limit");

                    registry.push_back(name);
                }
            }
        }

        std::cout << "runtime_bone_id,bone_name\n";

        for (std::size_t i = 0; i < registry.size(); ++i)
            std::cout << i << ',' << registry[i] << '\n';

        std::cerr
            << "Recovered " << registry.size()
            << " first-seen motion bone names.\n";

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}
