// socom_texture_tool.cpp
// SOCOM: U.S. Navy SEALs (PS2) GameZ texture extractor.
//
// Decodes the paired *_txr.zed / *_pal.zed archives used by GameZ.
// Confirmed for SOCOM 1 weapon archives:
//   - ZAR/CZAR envelope version 0x00020002
//   - texture pixels: PSMT8 (8-bit indexed)
//   - texels are stored LINEAR in the archive payload
//   - TEX0.CBP selects palette object texpal_<CBP>
//   - CSM1 CLUT permutation
//   - PSMCT16 and PSMCT32 palettes
//
// Output: ordinary RGBA PNG files.
// No external libraries are required.
//
// macOS / Apple Silicon:
//   clang++ -std=c++17 -O2 -Wall -Wextra socom_texture_tool.cpp -o socom_texture_tool
//
// Usage:
//   ./socom_texture_tool weap_txr.zed weap_pal.zed --list
//   ./socom_texture_tool weap_txr.zed weap_pal.zed --extract glock18.tif out
//   ./socom_texture_tool weap_txr.zed weap_pal.zed --extract-all out

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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

static_assert(sizeof(ZarHeader) == 0x64);
static_assert(sizeof(ZarNodeDisk) == 0x10);

template <typename T>
static T ReadLE(const std::uint8_t* p)
{
    T v{};
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
    if (alignment == 0)
        throw std::runtime_error("zero ZAR alignment");

    const std::size_t r = value % alignment;
    return r ? value + (alignment - r) : value;
}

struct Node
{
    std::size_t index{};
    std::string name;
    std::uint32_t dataOffset{};
    std::uint32_t dataSize{};
    std::vector<Node> children;
};

class ZarArchive
{
public:
    explicit ZarArchive(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw std::runtime_error("cannot open " + path.string());

        in.seekg(0, std::ios::end);
        const auto length = in.tellg();
        in.seekg(0, std::ios::beg);

        if (length < static_cast<std::streamoff>(sizeof(ZarHeader)))
            throw std::runtime_error("file is too small");

        bytes_.resize(static_cast<std::size_t>(length));

        in.read(
            reinterpret_cast<char*>(bytes_.data()),
            static_cast<std::streamsize>(bytes_.size()));

        if (!in)
            throw std::runtime_error("failed reading " + path.string());

        std::memcpy(&header_, bytes_.data(), sizeof(header_));

        if (header_.version != 0x00020002)
            throw std::runtime_error("unsupported ZAR version");

        stringsOffset_ = sizeof(ZarHeader);
        nodesOffset_ = stringsOffset_ + header_.stringBlockSize;

        const std::uint64_t nodeBytes =
            static_cast<std::uint64_t>(header_.nodeCount) *
            sizeof(ZarNodeDisk);

        if (nodesOffset_ + nodeBytes > bytes_.size())
            throw std::runtime_error("ZAR node table outside file");

        payloadOffset_ = AlignUp(
            static_cast<std::size_t>(nodesOffset_ + nodeBytes),
            header_.alignment);

        if (payloadOffset_ + header_.payloadSize != bytes_.size())
            throw std::runtime_error("ZAR payload does not end at EOF");

        diskNodes_.resize(header_.nodeCount);

        std::memcpy(
            diskNodes_.data(),
            bytes_.data() + nodesOffset_,
            static_cast<std::size_t>(nodeBytes));

        std::size_t cursor = 0;
        root_ = ParseNode(cursor);

        if (cursor != diskNodes_.size())
            throw std::runtime_error("ZAR tree did not consume all nodes");
    }

    const Node& Root() const
    {
        return root_;
    }

    const std::uint8_t* PayloadAt(
        std::uint32_t offset,
        std::size_t size = 1) const
    {
        if (static_cast<std::uint64_t>(offset) + size >
            header_.payloadSize)
        {
            throw std::runtime_error("payload access outside archive");
        }

        return bytes_.data() + payloadOffset_ + offset;
    }

private:
    std::string ResolveName(std::uint32_t address) const
    {
        if (address == 0)
            return "<root>";

        if (address < header_.preferredBase)
            throw std::runtime_error("bad serialized name address");

        const std::uint64_t relative =
            static_cast<std::uint64_t>(address) -
            header_.preferredBase;

        if (relative >= header_.stringBlockSize)
            throw std::runtime_error("name address outside string block");

        const std::size_t start =
            stringsOffset_ + static_cast<std::size_t>(relative);

        const std::size_t limit =
            stringsOffset_ + header_.stringBlockSize;

        std::size_t end = start;

        while (end < limit && bytes_[end] != 0)
            ++end;

        if (end == limit)
            throw std::runtime_error("unterminated node name");

        return std::string(
            reinterpret_cast<const char*>(bytes_.data() + start),
            end - start);
    }

    Node ParseNode(std::size_t& cursor)
    {
        if (cursor >= diskNodes_.size())
            throw std::runtime_error("ZAR tree overrun");

        const std::size_t index = cursor;
        const ZarNodeDisk d = diskNodes_[cursor++];

        Node node;
        node.index = index;
        node.name = ResolveName(d.nameAddress);
        node.dataOffset = d.dataOffset;
        node.dataSize = d.dataSize;
        node.children.reserve(d.childCount);

        for (std::uint32_t i = 0; i < d.childCount; ++i)
            node.children.push_back(ParseNode(cursor));

        return node;
    }

    std::vector<std::uint8_t> bytes_;
    ZarHeader header_{};
    std::vector<ZarNodeDisk> diskNodes_;
    Node root_;
    std::size_t stringsOffset_{};
    std::size_t nodesOffset_{};
    std::size_t payloadOffset_{};
};

struct Tex0
{
    std::uint32_t tbp0{};
    std::uint32_t tbw{};
    std::uint32_t psm{};
    std::uint32_t tw{};
    std::uint32_t th{};
    std::uint32_t tcc{};
    std::uint32_t tfx{};
    std::uint32_t cbp{};
    std::uint32_t cpsm{};
    std::uint32_t csm{};
    std::uint32_t csa{};
    std::uint32_t cld{};
};

static Tex0 DecodeTex0(std::uint64_t v)
{
    Tex0 t;
    t.tbp0 = static_cast<std::uint32_t>(v & 0x3FFF);
    t.tbw  = static_cast<std::uint32_t>((v >> 14) & 0x3F);
    t.psm  = static_cast<std::uint32_t>((v >> 20) & 0x3F);
    t.tw   = static_cast<std::uint32_t>((v >> 26) & 0x0F);
    t.th   = static_cast<std::uint32_t>((v >> 30) & 0x0F);
    t.tcc  = static_cast<std::uint32_t>((v >> 34) & 0x01);
    t.tfx  = static_cast<std::uint32_t>((v >> 35) & 0x03);
    t.cbp  = static_cast<std::uint32_t>((v >> 37) & 0x3FFF);
    t.cpsm = static_cast<std::uint32_t>((v >> 51) & 0x0F);
    t.csm  = static_cast<std::uint32_t>((v >> 55) & 0x01);
    t.csa  = static_cast<std::uint32_t>((v >> 56) & 0x1F);
    t.cld  = static_cast<std::uint32_t>((v >> 61) & 0x07);
    return t;
}

struct Rgba
{
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{};
};

struct Palette
{
    std::uint32_t id{};
    std::uint32_t parameter{};
    std::vector<Rgba> colors;
};

static std::uint8_t Expand5(std::uint32_t v)
{
    // Exact 5-bit -> 8-bit expansion.
    return static_cast<std::uint8_t>((v << 3) | (v >> 2));
}

static std::uint8_t Ps2Alpha8ToPng(std::uint8_t a)
{
    // GS alpha convention is normally 0..0x80.
    // Preserve values above that while mapping 0x80 to full PNG opacity.
    const unsigned x = static_cast<unsigned>(a) * 2u;
    return static_cast<std::uint8_t>(std::min(x, 255u));
}

static std::size_t Csm1Index(std::size_t p)
{
    // CSM1 swaps CLUT index bits 3 and 4 in groups of 32.
    return
        (p & 0xE7u) +
        ((p & 0x08u) << 1) +
        ((p & 0x10u) >> 1);
}

static std::map<std::uint32_t, Palette>
LoadPalettes(const ZarArchive& palArchive)
{
    const Node* palettesRoot = nullptr;

    for (const auto& n : palArchive.Root().children)
    {
        if (n.name == "palettes")
        {
            palettesRoot = &n;
            break;
        }
    }

    if (!palettesRoot)
        throw std::runtime_error("palette archive has no 'palettes' node");

    std::map<std::uint32_t, Palette> result;

    for (const auto& paletteNode : palettesRoot->children)
    {
        const Node* parNode = nullptr;
        const Node* bufNode = nullptr;

        for (const auto& child : paletteNode.children)
        {
            if (child.name == "par")
                parNode = &child;
            else if (child.name == "buf")
                bufNode = &child;
        }

        if (!parNode || !bufNode)
            throw std::runtime_error(
                "palette node missing par/buf: " + paletteNode.name);

        if (parNode->dataSize != 8)
            throw std::runtime_error(
                "unexpected palette parameter size");

        const auto* par =
            palArchive.PayloadAt(parNode->dataOffset, parNode->dataSize);

        const std::uint32_t id = ReadLE<std::uint32_t>(par + 0);
        const std::uint32_t parameter = ReadLE<std::uint32_t>(par + 4);

        const auto* raw =
            palArchive.PayloadAt(bufNode->dataOffset, bufNode->dataSize);

        Palette palette;
        palette.id = id;
        palette.parameter = parameter;
        palette.colors.resize(256);

        std::array<Rgba,256> physical{};

        if (bufNode->dataSize == 0x200)
        {
            // PSMCT16 / A1B5G5R5.
            for (std::size_t i = 0; i < 256; ++i)
            {
                const std::uint16_t c =
                    ReadLE<std::uint16_t>(raw + i * 2);

                physical[i] = {
                    Expand5((c >> 0) & 0x1F),
                    Expand5((c >> 5) & 0x1F),
                    Expand5((c >> 10) & 0x1F),
                    static_cast<std::uint8_t>(
                        (c & 0x8000) ? 255 : 0)
                };
            }
        }
        else if (bufNode->dataSize == 0x400)
        {
            // PSMCT32 / R,G,B,A byte order.
            for (std::size_t i = 0; i < 256; ++i)
            {
                physical[i] = {
                    raw[i * 4 + 0],
                    raw[i * 4 + 1],
                    raw[i * 4 + 2],
                    Ps2Alpha8ToPng(raw[i * 4 + 3])
                };
            }
        }
        else
        {
            throw std::runtime_error(
                "unsupported palette buffer size 0x" +
                [&] {
                    std::ostringstream s;
                    s << std::hex << bufNode->dataSize;
                    return s.str();
                }());
        }

        // Game archives use CSM1. Convert physical CLUT order into
        // ordinary logical palette-index order.
        for (std::size_t p = 0; p < 256; ++p)
            palette.colors[Csm1Index(p)] = physical[p];

        result[id] = palette;
    }

    return result;
}

// ---------------- Minimal dependency-free RGBA PNG writer ----------------

static std::uint32_t Crc32(
    const std::uint8_t* data,
    std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;

    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];

        for (int bit = 0; bit < 8; ++bit)
        {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(
                    -static_cast<std::int32_t>(crc & 1u));

            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

static std::uint32_t Adler32(
    const std::uint8_t* data,
    std::size_t size)
{
    constexpr std::uint32_t mod = 65521;
    std::uint32_t a = 1;
    std::uint32_t b = 0;

    for (std::size_t i = 0; i < size; ++i)
    {
        a = (a + data[i]) % mod;
        b = (b + a) % mod;
    }

    return (b << 16) | a;
}

static void AppendBE32(
    std::vector<std::uint8_t>& out,
    std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

static void AppendChunk(
    std::vector<std::uint8_t>& png,
    const std::array<char,4>& type,
    const std::vector<std::uint8_t>& data)
{
    AppendBE32(png, static_cast<std::uint32_t>(data.size()));

    const std::size_t crcStart = png.size();

    for (char c : type)
        png.push_back(static_cast<std::uint8_t>(c));

    png.insert(png.end(), data.begin(), data.end());

    const std::uint32_t crc =
        Crc32(png.data() + crcStart, 4 + data.size());

    AppendBE32(png, crc);
}

static void WritePngRgba(
    const fs::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<Rgba>& pixels)
{
    if (pixels.size() !=
        static_cast<std::size_t>(width) * height)
    {
        throw std::runtime_error("PNG pixel count mismatch");
    }

    // PNG scanlines: one filter byte followed by RGBA.
    std::vector<std::uint8_t> scan;
    scan.reserve(
        static_cast<std::size_t>(height) *
        (1 + static_cast<std::size_t>(width) * 4));

    for (std::uint32_t y = 0; y < height; ++y)
    {
        scan.push_back(0); // filter type: None

        for (std::uint32_t x = 0; x < width; ++x)
        {
            const Rgba& c =
                pixels[static_cast<std::size_t>(y) * width + x];

            scan.push_back(c.r);
            scan.push_back(c.g);
            scan.push_back(c.b);
            scan.push_back(c.a);
        }
    }

    // Build a zlib stream using uncompressed DEFLATE blocks.
    std::vector<std::uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);

    std::size_t pos = 0;

    while (pos < scan.size())
    {
        const std::size_t remaining = scan.size() - pos;
        const std::uint16_t len =
            static_cast<std::uint16_t>(
                std::min<std::size_t>(remaining, 65535));

        const bool finalBlock =
            (pos + len == scan.size());

        z.push_back(finalBlock ? 0x01 : 0x00);

        z.push_back(static_cast<std::uint8_t>(len & 0xFF));
        z.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));

        const std::uint16_t nlen =
            static_cast<std::uint16_t>(~len);

        z.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
        z.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFF));

        z.insert(
            z.end(),
            scan.begin() + static_cast<std::ptrdiff_t>(pos),
            scan.begin() + static_cast<std::ptrdiff_t>(pos + len));

        pos += len;
    }

    AppendBE32(z, Adler32(scan.data(), scan.size()));

    std::vector<std::uint8_t> png = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };

    std::vector<std::uint8_t> ihdr;
    AppendBE32(ihdr, width);
    AppendBE32(ihdr, height);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // RGBA
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace

    AppendChunk(png, {'I','H','D','R'}, ihdr);
    AppendChunk(png, {'I','D','A','T'}, z);
    AppendChunk(png, {'I','E','N','D'}, {});

    std::ofstream out(path, std::ios::binary);

    if (!out)
        throw std::runtime_error("cannot create " + path.string());

    out.write(
        reinterpret_cast<const char*>(png.data()),
        static_cast<std::streamsize>(png.size()));

    if (!out)
        throw std::runtime_error("failed writing " + path.string());
}

// -----------------------------------------------------------------------

struct TextureInfo
{
    const Node* textureNode{};
    const Node* dataNode{};
    std::uint16_t width{};
    std::uint16_t height{};
    std::uint32_t texelBytes{};
    std::uint32_t textureId{};
    Tex0 tex0{};
};

static const Node* FindChild(
    const Node& parent,
    const std::string& name)
{
    for (const auto& n : parent.children)
        if (n.name == name)
            return &n;

    return nullptr;
}

static std::vector<TextureInfo>
ReadTextures(const ZarArchive& txr)
{
    const Node* root = FindChild(txr.Root(), "textures");

    if (!root)
        throw std::runtime_error("texture archive has no 'textures' node");

    std::vector<TextureInfo> result;

    for (const auto& texture : root->children)
    {
        const Node* dataNode = FindChild(texture, "texdat");

        if (!dataNode)
            throw std::runtime_error(
                "texture has no texdat node: " + texture.name);

        const auto* raw =
            txr.PayloadAt(dataNode->dataOffset, dataNode->dataSize);

        if (dataNode->dataSize < 0xA0)
            throw std::runtime_error("texdat record too small");

        TextureInfo info;
        info.textureNode = &texture;
        info.dataNode = dataNode;
        info.width = ReadLE<std::uint16_t>(raw + 0);
        info.height = ReadLE<std::uint16_t>(raw + 2);
        info.texelBytes = ReadLE<std::uint32_t>(raw + 4);
        info.textureId = ReadLE<std::uint32_t>(raw + 8);

        if (info.texelBytes !=
            static_cast<std::uint32_t>(info.width) *
            static_cast<std::uint32_t>(info.height))
        {
            // UI GameZ banks contain mixed PS2 texture formats.  The original
            // extractor was written for character PSMT8 banks and aborted on
            // the first unrelated non-indexed texture (for example blue.tif).
            // Skip unsupported entries here so a requested PSMT8 texture
            // later in the same archive can still be decoded.
            continue;
        }

        const std::uint64_t expected =
            0x20ull + info.texelBytes + 0x80ull;

        if (expected != dataNode->dataSize)
        {
            continue;
        }

        // texdat layout:
        //   0x20-byte header
        //   width*height linear PSMT8 texels
        //   0x80-byte GS upload/state trailer
        // TEX0 is 0x40 bytes into the trailer.
        const std::size_t tex0Offset =
            0x20u + info.texelBytes + 0x40u;

        const std::uint32_t lo =
            ReadLE<std::uint32_t>(raw + tex0Offset + 0);
        const std::uint32_t hi =
            ReadLE<std::uint32_t>(raw + tex0Offset + 4);

        info.tex0 =
            DecodeTex0(
                (static_cast<std::uint64_t>(hi) << 32) | lo);

        if (info.tex0.psm != 0x13)
        {
            continue;
        }

        if (info.tex0.csm != 0)
        {
            continue;
        }

        result.push_back(info);
    }

    return result;
}

static std::vector<Rgba>
DecodeTexture(
    const ZarArchive& txr,
    const TextureInfo& texture,
    const std::map<std::uint32_t, Palette>& palettes)
{
    const auto it = palettes.find(texture.tex0.cbp);

    if (it == palettes.end())
    {
        throw std::runtime_error(
            "missing palette texpal_" +
            std::to_string(texture.tex0.cbp));
    }

    const Palette& palette = it->second;

    if (texture.tex0.cpsm == 2 &&
        palette.colors.size() != 256)
    {
        throw std::runtime_error("invalid PSMCT16 palette");
    }

    if (texture.tex0.cpsm != 0 &&
        texture.tex0.cpsm != 2)
    {
        throw std::runtime_error(
            "unsupported TEX0 CPSM " +
            std::to_string(texture.tex0.cpsm));
    }

    const auto* raw =
        txr.PayloadAt(
            texture.dataNode->dataOffset,
            texture.dataNode->dataSize);

    const std::uint8_t* indices = raw + 0x20;

    std::vector<Rgba> pixels(texture.texelBytes);

    // Important: the archive stores these PSMT8 source indices linearly.
    // Do NOT apply a GS-VRAM PSMT8 unswizzle here.
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = palette.colors[indices[i]];

    return pixels;
}

static fs::path PngName(const std::string& original)
{
    fs::path p(original);
    p.replace_extension(".png");
    return p.filename();
}

static void List(
    const std::vector<TextureInfo>& textures)
{
    std::cout
        << "name,width,height,texture_id,palette_cbp,psm,cpsm,csm\n";

    for (const auto& t : textures)
    {
        std::cout
            << t.textureNode->name << ','
            << t.width << ','
            << t.height << ','
            << t.textureId << ','
            << t.tex0.cbp << ','
            << t.tex0.psm << ','
            << t.tex0.cpsm << ','
            << t.tex0.csm << '\n';
    }
}

static void ExtractOne(
    const ZarArchive& txr,
    const TextureInfo& t,
    const std::map<std::uint32_t, Palette>& palettes,
    const fs::path& outputDirectory)
{
    fs::create_directories(outputDirectory);

    auto pixels =
        DecodeTexture(txr, t, palettes);

    // PS2 GS texture data and our Windows/GDI+ image convention use
    // opposite vertical origins for these retail UI textures.  The first
    // successful UI extraction proved this visibly:
    //   SplashLogo upside down
    //   arrow_top pointing down
    //   arrow_botm pointing up
    // Flip the decoded RGBA rows once here so every consumer receives an
    // upright retail image.
    const std::size_t rowBytes =
        static_cast<std::size_t>(t.width) * 4u;
    for (std::uint32_t y = 0; y < t.height / 2u; ++y)
    {
        const std::uint32_t other =
            t.height - 1u - y;

        auto a =
            pixels.begin() +
            static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(y) * rowBytes);
        auto b =
            pixels.begin() +
            static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(other) * rowBytes);

        for (std::size_t x = 0; x < rowBytes; ++x)
            std::swap(a[x], b[x]);
    }

    const fs::path output =
        outputDirectory / PngName(t.textureNode->name);

    WritePngRgba(output, t.width, t.height, pixels);

    std::cout
        << t.textureNode->name
        << " -> "
        << output.string()
        << "  "
        << t.width << 'x' << t.height
        << " PSMT8"
        << " CBP=" << t.tex0.cbp
        << " CPSM=" << t.tex0.cpsm
        << '\n';
}

static void Usage()
{
    std::cerr <<
        "SOCOM GameZ texture extractor\n\n"
        "Usage:\n"
        "  socom_texture_tool <*_txr.zed> <*_pal.zed> --list\n"
        "  socom_texture_tool <*_txr.zed> <*_pal.zed> "
        "--extract <texture.tif> <output_dir>\n"
        "  socom_texture_tool <*_txr.zed> <*_pal.zed> "
        "--extract-all <output_dir>\n";
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 4)
        {
            Usage();
            return 1;
        }

        const fs::path txrPath = argv[1];
        const fs::path palPath = argv[2];
        const std::string action = argv[3];

        ZarArchive txr(txrPath);
        ZarArchive pal(palPath);

        const auto palettes = LoadPalettes(pal);
        const auto textures = ReadTextures(txr);

        if (action == "--list")
        {
            if (argc != 4)
            {
                Usage();
                return 1;
            }

            List(textures);
            return 0;
        }

        if (action == "--extract")
        {
            if (argc != 6)
            {
                Usage();
                return 1;
            }

            const std::string requested = argv[4];

            const auto it = std::find_if(
                textures.begin(),
                textures.end(),
                [&](const TextureInfo& t)
                {
                    return t.textureNode->name == requested;
                });

            if (it == textures.end())
                throw std::runtime_error(
                    "texture is absent or uses a texture layout not yet supported: " +
                    requested);

            ExtractOne(
                txr,
                *it,
                palettes,
                fs::path(argv[5]));

            return 0;
        }

        if (action == "--extract-all")
        {
            if (argc != 5)
            {
                Usage();
                return 1;
            }

            const fs::path output = argv[4];

            for (const auto& t : textures)
                ExtractOne(txr, t, palettes, output);

            std::cout
                << "Extracted "
                << textures.size()
                << " textures.\n";

            return 0;
        }

        Usage();
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}
