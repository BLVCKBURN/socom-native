// models_probe.cpp
// SOCOM 1 models.zar structural probe.
// C++17 / Visual Studio 2022 compatible.
//
// Dumps:
//   - each top-level model record
//   - embedded child/subobject offsets
//   - printable .tif texture references inside each model blob
//   - a short hex preview at each subobject entry point
//
// This does NOT attempt to decode PS2 geometry yet.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct ZarHeader
{
    std::uint32_t unknown00;
    std::uint32_t nodeCount;
    std::uint32_t stringBlockSize;
    std::uint32_t preferredBase;
    std::uint32_t alignment;
    std::uint8_t  unknown14[0x40];
    std::uint32_t payloadSize;
    std::uint32_t unknown58;
    std::uint32_t unknown5C;
    std::uint32_t version;
};

struct ZarNodeDisk
{
    std::uint32_t nameAddress;
    std::uint32_t dataOffset;
    std::uint32_t dataSize;
    std::uint32_t childCount;
};
#pragma pack(pop)

static_assert(sizeof(ZarHeader) == 0x64);
static_assert(sizeof(ZarNodeDisk) == 0x10);

static std::size_t AlignUp(std::size_t v, std::size_t a)
{
    if (!a) throw std::runtime_error("zero alignment");
    const auto r = v % a;
    return r ? v + (a - r) : v;
}

struct Node
{
    std::size_t index = 0;
    std::string name;
    std::uint32_t dataOffset = 0;
    std::uint32_t dataSize = 0;
    std::vector<Node> children;
};

class ZarArchive
{
public:
    explicit ZarArchive(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open " + path);

        in.seekg(0, std::ios::end);
        const auto len = in.tellg();
        in.seekg(0, std::ios::beg);

        bytes_.resize(static_cast<std::size_t>(len));
        in.read(reinterpret_cast<char*>(bytes_.data()),
                static_cast<std::streamsize>(bytes_.size()));
        if (!in) throw std::runtime_error("read failed");

        if (bytes_.size() < sizeof(ZarHeader))
            throw std::runtime_error("file too small");

        std::memcpy(&h_, bytes_.data(), sizeof(h_));
        if (h_.version != 0x00020002)
            throw std::runtime_error("unsupported ZAR version");

        stringsOff_ = sizeof(ZarHeader);
        nodesOff_ = stringsOff_ + h_.stringBlockSize;

        const std::uint64_t nodeBytes =
            static_cast<std::uint64_t>(h_.nodeCount) * sizeof(ZarNodeDisk);

        if (nodesOff_ + nodeBytes > bytes_.size())
            throw std::runtime_error("node table outside file");

        payloadOff_ = AlignUp(
            static_cast<std::size_t>(nodesOff_ + nodeBytes),
            h_.alignment);

        if (payloadOff_ + h_.payloadSize != bytes_.size())
            throw std::runtime_error("layout check failed");

        disk_.resize(h_.nodeCount);
        std::memcpy(disk_.data(),
                    bytes_.data() + nodesOff_,
                    static_cast<std::size_t>(nodeBytes));

        std::size_t cursor = 0;
        root_ = ParseNode(cursor);

        if (cursor != disk_.size())
            throw std::runtime_error("tree did not consume every node");
    }

    const Node& Root() const { return root_; }

    const std::uint8_t* Data(const Node& n) const
    {
        if (static_cast<std::uint64_t>(n.dataOffset) + n.dataSize >
            h_.payloadSize)
            throw std::runtime_error("payload range invalid");

        return bytes_.data() + payloadOff_ + n.dataOffset;
    }

private:
    std::string ResolveName(std::uint32_t addr) const
    {
        if (!addr) return "<root>";
        if (addr < h_.preferredBase)
            throw std::runtime_error("bad name pointer");

        const auto rel =
            static_cast<std::uint64_t>(addr) - h_.preferredBase;

        if (rel >= h_.stringBlockSize)
            throw std::runtime_error("name outside string block");

        const auto start = stringsOff_ + static_cast<std::size_t>(rel);
        const auto limit = stringsOff_ + h_.stringBlockSize;

        std::size_t end = start;
        while (end < limit && bytes_[end] != 0)
            ++end;

        if (end == limit)
            throw std::runtime_error("unterminated name");

        return std::string(
            reinterpret_cast<const char*>(bytes_.data() + start),
            end - start);
    }

    Node ParseNode(std::size_t& cursor)
    {
        if (cursor >= disk_.size())
            throw std::runtime_error("tree overrun");

        const auto idx = cursor;
        const auto d = disk_[cursor++];

        Node n;
        n.index = idx;
        n.name = ResolveName(d.nameAddress);
        n.dataOffset = d.dataOffset;
        n.dataSize = d.dataSize;
        n.children.reserve(d.childCount);

        for (std::uint32_t i = 0; i < d.childCount; ++i)
            n.children.push_back(ParseNode(cursor));

        return n;
    }

    std::vector<std::uint8_t> bytes_;
    ZarHeader h_{};
    std::vector<ZarNodeDisk> disk_;
    Node root_;
    std::size_t stringsOff_ = 0;
    std::size_t nodesOff_ = 0;
    std::size_t payloadOff_ = 0;
};

static std::string HexPreview(const std::uint8_t* p, std::size_t n)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < n; ++i)
    {
        if (i) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(p[i]);
    }
    return out.str();
}

static void FindTextureStrings(const std::uint8_t* data, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        if (data[i] < 0x20 || data[i] > 0x7e)
            continue;

        std::size_t j = i;
        while (j < size && data[j] >= 0x20 && data[j] <= 0x7e)
            ++j;

        if (j < size && data[j] == 0 && (j - i) >= 4)
        {
            const std::string s(
                reinterpret_cast<const char*>(data + i),
                j - i);

            if (s.find(".tif") != std::string::npos)
            {
                std::cout << "    texture @ +0x"
                          << std::hex << i << std::dec
                          << ": " << s << '\n';
            }
        }

        if (j > i)
            i = j;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: models_probe <models.zar>\n";
        return 1;
    }

    try
    {
        ZarArchive z(argv[1]);

        std::cout << "Top-level models: "
                  << z.Root().children.size() << "\n\n";

        for (const auto& model : z.Root().children)
        {
            std::cout << "[" << model.name << "]"
                      << " payload_off=0x" << std::hex << model.dataOffset
                      << " size=0x" << model.dataSize
                      << std::dec
                      << " children=" << model.children.size()
                      << '\n';

            const auto* modelData = z.Data(model);

            for (const auto& child : model.children)
            {
                if (child.dataSize != 4)
                {
                    std::cout << "    " << child.name
                              << ": unexpected child size "
                              << child.dataSize << '\n';
                    continue;
                }

                std::uint32_t internalOffset = 0;
                std::memcpy(&internalOffset, z.Data(child), 4);

                std::cout << "    " << child.name
                          << " -> parent +0x"
                          << std::hex << internalOffset << std::dec;

                if (internalOffset < model.dataSize)
                {
                    const auto remain =
                        static_cast<std::size_t>(model.dataSize - internalOffset);
                    const auto preview =
                        remain < 64 ? remain : static_cast<std::size_t>(64);

                    std::cout << "\n      preview: "
                              << HexPreview(modelData + internalOffset, preview);
                }
                else
                {
                    std::cout << "  [OUT OF RANGE]";
                }

                std::cout << '\n';
            }

            FindTextureStrings(modelData, model.dataSize);
            std::cout << '\n';
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }

    return 0;
}
