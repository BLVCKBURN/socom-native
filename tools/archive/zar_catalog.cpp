// zar_catalog_mac.cpp
// Recursively catalogs every models.zar under a directory.
// macOS / Apple Silicon / clang++ / C++17 compatible.
//
// Build:
//   clang++ -std=c++17 -O2 -Wall -Wextra zar_catalog_mac.cpp -o zar_catalog
//
// Run:
//   ./zar_catalog "/path/to/SOCOM_EXTRACTED" > models_catalog.csv

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct ZarHeader
{
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

struct ZarNodeDisk
{
    std::uint32_t nameAddress;
    std::uint32_t dataOffset;
    std::uint32_t dataSize;
    std::uint32_t childCount;
};
#pragma pack(pop)

static_assert(sizeof(ZarHeader) == 0x64, "Unexpected ZarHeader size");
static_assert(sizeof(ZarNodeDisk) == 0x10, "Unexpected ZarNodeDisk size");

static std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
    if (alignment == 0)
        throw std::runtime_error("zero alignment");

    const auto remainder = value % alignment;
    return remainder ? value + (alignment - remainder) : value;
}

static std::string CsvQuote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

static std::uint64_t Fnv1a64(const std::vector<std::uint8_t>& data)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (std::uint8_t byte : data)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::string Join(const std::vector<std::string>& values,
                        const char* separator = " | ")
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i) out << separator;
        out << values[i];
    }
    return out.str();
}

static std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string GuessCategory(const std::vector<std::string>& names)
{
    std::string all;
    for (const auto& n : names)
    {
        all += Lower(n);
        all += ' ';
    }

    auto contains = [&](const char* s) {
        return all.find(s) != std::string::npos;
    };

    if (contains("worldmodel") || contains("sky") || contains("tree") ||
        contains("bush") || contains("building") || contains("bunker"))
        return "world/level";

    if (contains("mesh_seal") || contains("mesh_con_") ||
        contains("mesh_terror") || contains("mesh_merc"))
        return "character";

    if (contains("muzzle_flash") || contains("bullet_shell") ||
        contains("tracer") || contains("fragment"))
        return "effects/projectiles";

    if (contains("gear_") || contains("seal_hat") || contains("holster"))
        return "gear/attachments";

    if (contains("ui") || contains("glock") || contains("ak47") ||
        contains("m4a") || contains("mp5") || contains("m16"))
        return "weapon/UI";

    return "unknown";
}

struct ParsedZar
{
    ZarHeader header{};
    std::vector<ZarNodeDisk> nodes;
    std::vector<std::string> names;
    std::vector<std::uint8_t> bytes;
    std::size_t stringsOffset = 0;
    std::size_t nodesOffset = 0;
    std::size_t payloadOffset = 0;
};

static std::string ResolveName(const ParsedZar& zar, std::uint32_t address)
{
    if (address == 0) return "<root>";
    if (address < zar.header.preferredBase) return "<bad-name>";

    const auto relative =
        static_cast<std::uint64_t>(address) - zar.header.preferredBase;

    if (relative >= zar.header.stringBlockSize)
        return "<bad-name>";

    const std::size_t start =
        zar.stringsOffset + static_cast<std::size_t>(relative);
    const std::size_t limit =
        zar.stringsOffset + zar.header.stringBlockSize;

    std::size_t end = start;
    while (end < limit && zar.bytes[end] != 0) ++end;

    if (end == limit) return "<unterminated>";

    return std::string(
        reinterpret_cast<const char*>(zar.bytes.data() + start),
        end - start);
}

static ParsedZar LoadZar(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("open failed");

    in.seekg(0, std::ios::end);
    const std::streamoff length = in.tellg();
    if (length < static_cast<std::streamoff>(sizeof(ZarHeader)))
        throw std::runtime_error("file too small");

    in.seekg(0, std::ios::beg);

    ParsedZar zar;
    zar.bytes.resize(static_cast<std::size_t>(length));
    in.read(reinterpret_cast<char*>(zar.bytes.data()),
            static_cast<std::streamsize>(zar.bytes.size()));
    if (!in) throw std::runtime_error("read failed");

    std::memcpy(&zar.header, zar.bytes.data(), sizeof(zar.header));

    if (zar.header.version != 0x00020002)
        throw std::runtime_error("unexpected ZAR version");

    zar.stringsOffset = sizeof(ZarHeader);
    zar.nodesOffset = zar.stringsOffset + zar.header.stringBlockSize;

    const std::uint64_t nodeBytes =
        static_cast<std::uint64_t>(zar.header.nodeCount) * sizeof(ZarNodeDisk);

    if (zar.nodesOffset + nodeBytes > zar.bytes.size())
        throw std::runtime_error("node table outside file");

    zar.payloadOffset = AlignUp(
        static_cast<std::size_t>(zar.nodesOffset + nodeBytes),
        zar.header.alignment);

    if (zar.payloadOffset + zar.header.payloadSize != zar.bytes.size())
        throw std::runtime_error("layout does not reach EOF");

    zar.nodes.resize(zar.header.nodeCount);
    std::memcpy(zar.nodes.data(), zar.bytes.data() + zar.nodesOffset,
                static_cast<std::size_t>(nodeBytes));

    zar.names.reserve(zar.nodes.size());
    for (const auto& node : zar.nodes)
        zar.names.push_back(ResolveName(zar, node.nameAddress));

    return zar;
}

static void ConsumeSubtree(const ParsedZar& zar, std::size_t& cursor)
{
    if (cursor >= zar.nodes.size())
        throw std::runtime_error("tree overrun");

    const auto childCount = zar.nodes[cursor].childCount;
    ++cursor;

    for (std::uint32_t i = 0; i < childCount; ++i)
        ConsumeSubtree(zar, cursor);
}

static std::vector<std::size_t> TopLevelIndices(const ParsedZar& zar)
{
    if (zar.nodes.empty())
        throw std::runtime_error("empty node table");

    std::vector<std::size_t> result;
    std::size_t cursor = 1;
    const auto rootChildren = zar.nodes[0].childCount;

    for (std::uint32_t i = 0; i < rootChildren; ++i)
    {
        if (cursor >= zar.nodes.size())
            throw std::runtime_error("bad root child count");

        result.push_back(cursor);
        ConsumeSubtree(zar, cursor);
    }

    if (cursor != zar.nodes.size())
        throw std::runtime_error("tree did not consume all nodes");

    return result;
}

static std::vector<std::string> TextureRefs(const ParsedZar& zar)
{
    std::set<std::string> found;
    const auto* data = zar.bytes.data() + zar.payloadOffset;
    const std::size_t size = zar.header.payloadSize;

    for (std::size_t i = 0; i < size; ++i)
    {
        if (data[i] < 0x20 || data[i] > 0x7e)
            continue;

        std::size_t j = i;
        while (j < size && data[j] >= 0x20 && data[j] <= 0x7e)
            ++j;

        if (j < size && data[j] == 0 && j > i)
        {
            std::string text(
                reinterpret_cast<const char*>(data + i),
                j - i);

            if (Lower(text).find(".tif") != std::string::npos)
                found.insert(text);
        }

        if (j > i) i = j;
    }

    return std::vector<std::string>(found.begin(), found.end());
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr
            << "Usage:\n"
            << "  ./zar_catalog <root_directory>\n\n"
            << "Example:\n"
            << "  ./zar_catalog \"/Users/yourname/Desktop/SOCOM\" > models_catalog.csv\n";
        return 1;
    }

    const fs::path root(argv[1]);

    if (!fs::exists(root) || !fs::is_directory(root))
    {
        std::cerr << "Invalid directory: " << root << '\n';
        return 2;
    }

    std::cout
        << "path,file_size,fnv1a64,node_count,string_bytes,payload_bytes,"
           "top_level_count,category,top_level_names,texture_refs\n";

    std::size_t scanned = 0;
    std::size_t failures = 0;
    std::error_code ec;

    fs::recursive_directory_iterator it(
        root,
        fs::directory_options::skip_permission_denied,
        ec);
    fs::recursive_directory_iterator end;

    if (ec)
    {
        std::cerr << "Unable to scan directory: " << ec.message() << '\n';
        return 2;
    }

    for (; it != end; it.increment(ec))
    {
        if (ec)
        {
            std::cerr << "Filesystem warning: " << ec.message() << '\n';
            ec.clear();
            continue;
        }

        const auto& entry = *it;
        if (!entry.is_regular_file(ec))
        {
            ec.clear();
            continue;
        }

        if (Lower(entry.path().filename().string()) != "models.zar")
            continue;

        try
        {
            ParsedZar zar = LoadZar(entry.path());
            const auto topIndices = TopLevelIndices(zar);

            std::vector<std::string> topNames;
            topNames.reserve(topIndices.size());
            for (auto index : topIndices)
                topNames.push_back(zar.names[index]);

            const auto textures = TextureRefs(zar);
            const std::uint64_t hash = Fnv1a64(zar.bytes);

            std::ostringstream hashText;
            hashText << std::hex << std::setw(16) << std::setfill('0') << hash;

            std::cout
                << CsvQuote(entry.path().string()) << ','
                << zar.bytes.size() << ','
                << hashText.str() << ','
                << zar.header.nodeCount << ','
                << zar.header.stringBlockSize << ','
                << zar.header.payloadSize << ','
                << topNames.size() << ','
                << CsvQuote(GuessCategory(topNames)) << ','
                << CsvQuote(Join(topNames)) << ','
                << CsvQuote(Join(textures))
                << '\n';

            ++scanned;
        }
        catch (const std::exception& e)
        {
            ++failures;
            std::cerr << "FAILED: " << entry.path() << " : " << e.what() << '\n';
        }
    }

    std::cerr << "Cataloged " << scanned
              << " models.zar files; failures=" << failures << '\n';

    return failures ? 3 : 0;
}
