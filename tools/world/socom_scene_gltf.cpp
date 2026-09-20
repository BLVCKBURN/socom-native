// socom_scene_gltf.cpp
// SOCOM: U.S. Navy SEALs (PS2) GameZ M8 full scene -> glTF 2.0 exporter.
//
// Confirmed against the M8 retail SOCOM 1 asset set:
//   models.zar
//   m8_mdl.zed
//   m8_txr.zed
//   m8_pal.zed
//
// Recovered world path:
//   m8_mdl.zed / models / worldmodel
//      -> CNode hierarchy + local 4x4 nparams transforms
//      -> preorder vis nodes
//
//   models.zar / worldmodel
//      -> one render-list child per preorder vis node
//      -> 0x3002 rigid triangle VIF packets
//      -> 0x3001 line-strip VIF packets
//
// World triangle position encoding:
//   game_position = packet_origin + int16_position / 16.0
//
// The exported synthetic root applies GameZ -> meter scale 0.1.
//
// Texture path:
//   m8_txr.zed + m8_pal.zed
//   - linear PSMT8 + CSM1 CLUT
//   - PSMCT16 / PSMCT32 palettes
//   - direct PSMCT32 textures
//
// Build:
//   clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
//     socom_scene_gltf.cpp -o socom_world_gltf
//
// Usage:
//   ./socom_scene_gltf models.zar m8_mdl.zed m8_txr.zed m8_pal.zed out_dir
//
// Output:
//   out_dir/m8_scene.gltf
//   out_dir/m8_scene.bin
//   out_dir/textures/*.png
//
// No external dependencies.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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

static std::size_t AlignUp(std::size_t v, std::size_t a)
{
    if (!a)
        throw std::runtime_error("zero alignment");

    const std::size_t r = v % a;
    return r ? v + (a - r) : v;
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
            throw std::runtime_error("file too small: " + path.string());

        bytes_.resize(static_cast<std::size_t>(length));

        in.read(
            reinterpret_cast<char*>(bytes_.data()),
            static_cast<std::streamsize>(bytes_.size()));

        if (!in)
            throw std::runtime_error("read failed: " + path.string());

        std::memcpy(&header_, bytes_.data(), sizeof(header_));

        if (header_.version != 0x00020002)
            throw std::runtime_error("unexpected ZAR/CZAR version");

        stringsOffset_ = sizeof(ZarHeader);
        nodesOffset_ = stringsOffset_ + header_.stringBlockSize;

        const std::uint64_t nodeBytes =
            static_cast<std::uint64_t>(header_.nodeCount) *
            sizeof(ZarNodeDisk);

        if (nodesOffset_ + nodeBytes > bytes_.size())
            throw std::runtime_error("node table outside file");

        payloadOffset_ = AlignUp(
            static_cast<std::size_t>(nodesOffset_ + nodeBytes),
            header_.alignment);

        if (payloadOffset_ + header_.payloadSize != bytes_.size())
            throw std::runtime_error("payload does not end at EOF");

        diskNodes_.resize(header_.nodeCount);

        std::memcpy(
            diskNodes_.data(),
            bytes_.data() + nodesOffset_,
            static_cast<std::size_t>(nodeBytes));

        std::size_t cursor = 0;
        root_ = ParseNode(cursor);

        if (cursor != diskNodes_.size())
            throw std::runtime_error("node tree did not consume all nodes");
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
            throw std::runtime_error("payload access out of range");
        }

        return bytes_.data() + payloadOffset_ + offset;
    }

private:
    std::string ResolveName(std::uint32_t address) const
    {
        if (!address)
            return "<root>";

        if (address < header_.preferredBase)
            throw std::runtime_error("bad serialized name pointer");

        const std::uint64_t relative =
            static_cast<std::uint64_t>(address) -
            header_.preferredBase;

        if (relative >= header_.stringBlockSize)
            throw std::runtime_error("name outside string block");

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
            throw std::runtime_error("tree overrun");

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

static const Node* FindChild(
    const Node& parent,
    const std::string& name)
{
    for (const auto& n : parent.children)
        if (n.name == name)
            return &n;

    return nullptr;
}

static const Node* FindTopLevel(
    const Node& root,
    const std::string& name)
{
    for (const auto& n : root.children)
        if (n.name == name)
            return &n;

    return nullptr;
}

static std::string ReadCString(
    const std::uint8_t* blob,
    std::size_t blobSize,
    std::uint32_t offset)
{
    if (offset >= blobSize)
        throw std::runtime_error("string offset outside blob");

    std::size_t end = offset;

    while (end < blobSize && blob[end] != 0)
        ++end;

    if (end == blobSize)
        throw std::runtime_error("unterminated model string");

    return std::string(
        reinterpret_cast<const char*>(blob + offset),
        end - offset);
}


static std::string ReadNodeString(
    const ZarArchive& archive,
    const Node& node)
{
    if (node.dataSize == 0)
        return {};

    const auto* raw =
        archive.PayloadAt(
            node.dataOffset,
            node.dataSize);

    std::size_t length = 0;

    while (length < node.dataSize &&
           raw[length] != 0)
    {
        ++length;
    }

    // Some GameZ model_name payloads occupy the exact field length
    // without a trailing NUL. Treat the full payload as the string in that
    // case; other node-string fields commonly include the terminator.
    return std::string(
        reinterpret_cast<const char*>(raw),
        length);
}

struct VifCode
{
    std::uint16_t immediate{};
    std::uint8_t num{};
    std::uint8_t cmd{};
};

static VifCode DecodeVif(std::uint32_t w)
{
    return {
        static_cast<std::uint16_t>(w & 0xFFFF),
        static_cast<std::uint8_t>((w >> 16) & 0xFF),
        static_cast<std::uint8_t>((w >> 24) & 0x7F)
    };
}

struct Vec2
{
    float x{}, y{};
};

struct Vec3
{
    float x{}, y{}, z{};
};

struct Vertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    std::array<std::uint8_t, 4> rgba{{255,255,255,255}};
};

struct PrimitiveData
{
    int mode = 4; // 4 TRIANGLES, 3 LINE_STRIP
    std::string texture;
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

struct RenderState
{
    std::string texture;
    std::uint32_t colorOffset{};
    std::uint32_t colorCount{};
};

static std::uint8_t GameZColorTo8(std::uint8_t v)
{
    // PS2/GameZ neutral/full intensity is 0x80.
    return static_cast<std::uint8_t>(
        std::min<unsigned>(255u, static_cast<unsigned>(v) * 2u));
}

static PrimitiveData ParseWorldTrianglePacket(
    const std::uint8_t* blob,
    std::size_t blobSize,
    std::uint32_t packetOffset,
    const RenderState& state)
{
    if (static_cast<std::uint64_t>(packetOffset) + 0x50 > blobSize)
        throw std::runtime_error("triangle packet truncated");

    const auto cmd0 = DecodeVif(
        ReadLE<std::uint32_t>(blob + packetOffset + 0));

    const auto cmd1 = DecodeVif(
        ReadLE<std::uint32_t>(blob + packetOffset + 4));

    if (cmd0.cmd != 0x01 ||
        cmd1.cmd != 0x6C ||
        cmd1.num != 4)
    {
        throw std::runtime_error("unexpected world triangle VIF header");
    }

    std::array<std::uint32_t,16> ctrl{};

    for (std::size_t i = 0; i < ctrl.size(); ++i)
        ctrl[i] =
            ReadLE<std::uint32_t>(
                blob + packetOffset + 8 + i * 4);

    const std::uint32_t vertexCount = ctrl[10];
    const std::uint32_t triangleCount = ctrl[11];
    const std::uint32_t triangleBase = ctrl[8];

    if (triangleBase != vertexCount * 3 + 4)
        throw std::runtime_error("world triangle-base invariant failed");

    const float originX =
        ReadLE<float>(
            reinterpret_cast<const std::uint8_t*>(&ctrl[12]));

    const float originY =
        ReadLE<float>(
            reinterpret_cast<const std::uint8_t*>(&ctrl[13]));

    const float originZ =
        ReadLE<float>(
            reinterpret_cast<const std::uint8_t*>(&ctrl[14]));

    std::size_t pos = packetOffset + 0x48;

    const auto cycleVerts =
        DecodeVif(ReadLE<std::uint32_t>(blob + pos));

    const auto unpackVerts =
        DecodeVif(ReadLE<std::uint32_t>(blob + pos + 4));

    pos += 8;

    if (cycleVerts.cmd != 0x01 ||
        cycleVerts.immediate != 0x0203 ||
        unpackVerts.cmd != 0x6D ||
        unpackVerts.num !=
            static_cast<std::uint8_t>(vertexCount * 2))
    {
        throw std::runtime_error("unexpected world vertex VIF layout");
    }

    if (pos + static_cast<std::size_t>(vertexCount) * 16 >
        blobSize)
    {
        throw std::runtime_error("world vertex stream out of range");
    }

    PrimitiveData result;
    result.mode = 4;
    result.texture = state.texture;
    result.vertices.resize(vertexCount);

    constexpr float normalScale = 1.0f / 32767.0f;
    constexpr float uvScale = 1.0f / 4096.0f;

    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        const auto* v = blob + pos + i * 16;

        const std::int16_t px = ReadLE<std::int16_t>(v + 0);
        const std::int16_t py = ReadLE<std::int16_t>(v + 2);
        const std::int16_t pz = ReadLE<std::int16_t>(v + 4);

        const std::int16_t nx = ReadLE<std::int16_t>(v + 6);
        const std::int16_t tu = ReadLE<std::int16_t>(v + 8);
        const std::int16_t tv = ReadLE<std::int16_t>(v + 10);
        const std::int16_t ny = ReadLE<std::int16_t>(v + 12);
        const std::int16_t nz = ReadLE<std::int16_t>(v + 14);

        Vertex out;

        // Confirmed against m8_mdl.zed visual/node bounds.
        out.position = {
            originX + static_cast<float>(px) / 16.0f,
            originY + static_cast<float>(py) / 16.0f,
            originZ + static_cast<float>(pz) / 16.0f
        };

        out.normal = {
            nx * normalScale,
            ny * normalScale,
            nz * normalScale
        };

        out.uv = {
            tu * uvScale,
            tv * uvScale
        };

        result.vertices[i] = out;
    }

    pos += static_cast<std::size_t>(vertexCount) * 16;

    const auto cycleIndices =
        DecodeVif(ReadLE<std::uint32_t>(blob + pos));

    const auto unpackIndices =
        DecodeVif(ReadLE<std::uint32_t>(blob + pos + 4));

    pos += 8;

    if (cycleIndices.cmd != 0x01 ||
        cycleIndices.immediate != 0x0102 ||
        unpackIndices.cmd != 0x6E ||
        unpackIndices.num != triangleCount ||
        (unpackIndices.immediate & 0x03FF) != triangleBase)
    {
        throw std::runtime_error("unexpected world triangle-index layout");
    }

    if (pos + static_cast<std::size_t>(triangleCount) * 4 >
        blobSize)
    {
        throw std::runtime_error("world index stream out of range");
    }

    result.indices.reserve(
        static_cast<std::size_t>(triangleCount) * 3);

    for (std::uint32_t i = 0; i < triangleCount; ++i)
    {
        const auto* t = blob + pos + i * 4;

        const std::uint8_t a = t[0];
        const std::uint8_t b = t[1];
        const std::uint8_t c = t[2];
        const std::uint8_t flag = t[3];

        if ((a % 3) || (b % 3) || (c % 3))
            throw std::runtime_error("world index not on VU 3-qword stride");

        // Both 1 and 3 occur in M8. Face-normal validation proves that
        // both use the authored winding, so preserve both as triangles.
        if (flag != 1 && flag != 3)
            throw std::runtime_error("unknown world triangle flag");

        const std::uint16_t ia =
            static_cast<std::uint16_t>(a / 3);

        const std::uint16_t ib =
            static_cast<std::uint16_t>(b / 3);

        const std::uint16_t ic =
            static_cast<std::uint16_t>(c / 3);

        if (ia >= vertexCount ||
            ib >= vertexCount ||
            ic >= vertexCount)
        {
            throw std::runtime_error("world index outside vertex array");
        }

        result.indices.push_back(ia);
        result.indices.push_back(ib);
        result.indices.push_back(ic);
    }

    pos += static_cast<std::size_t>(triangleCount) * 4;

    const auto cycleNormals =
        DecodeVif(ReadLE<std::uint32_t>(blob + pos));

    const auto unpackNormals =
        DecodeVif(ReadLE<std::uint32_t>(blob + pos + 4));

    pos += 8;

    if (cycleNormals.cmd != 0x01 ||
        cycleNormals.immediate != 0x0102 ||
        unpackNormals.cmd != 0x69 ||
        unpackNormals.num != triangleCount)
    {
        throw std::runtime_error("unexpected world face-normal layout");
    }

    pos += AlignUp(
        static_cast<std::size_t>(triangleCount) * 6,
        4);

    if (pos + 4 > blobSize ||
        DecodeVif(ReadLE<std::uint32_t>(blob + pos)).cmd != 0x17)
    {
        throw std::runtime_error("world triangle packet missing MSCNT");
    }

    if (state.colorCount == vertexCount &&
        static_cast<std::uint64_t>(state.colorOffset) +
            static_cast<std::uint64_t>(vertexCount) * 4 <=
            blobSize)
    {
        for (std::uint32_t i = 0; i < vertexCount; ++i)
        {
            const auto* c =
                blob + state.colorOffset + i * 4;

            result.vertices[i].rgba = {
                GameZColorTo8(c[0]),
                GameZColorTo8(c[1]),
                GameZColorTo8(c[2]),
                GameZColorTo8(c[3])
            };
        }
    }

    return result;
}

static PrimitiveData ParseWorldLinePacket(
    const std::uint8_t* blob,
    std::size_t blobSize,
    std::uint32_t packetOffset,
    std::uint32_t packetQwc,
    const RenderState& state)
{
    const std::size_t packetBytes =
        static_cast<std::size_t>(packetQwc) * 16;

    if (static_cast<std::uint64_t>(packetOffset) +
        packetBytes > blobSize ||
        packetBytes < 0x40)
    {
        throw std::runtime_error("line packet outside model blob");
    }

    const auto cmd0 =
        DecodeVif(ReadLE<std::uint32_t>(blob + packetOffset + 0));

    const auto cmd1 =
        DecodeVif(ReadLE<std::uint32_t>(blob + packetOffset + 4));

    if (cmd0.cmd != 0x01 ||
        cmd1.cmd != 0x6C ||
        cmd1.num != 2)
    {
        throw std::runtime_error("unexpected line packet header");
    }

    std::array<std::uint32_t,8> ctrl{};

    for (std::size_t i = 0; i < ctrl.size(); ++i)
        ctrl[i] =
            ReadLE<std::uint32_t>(
                blob + packetOffset + 8 + i * 4);

    const std::uint32_t vertexCount =
        ctrl[4] & 0x7FFFu;

    std::size_t pos = packetOffset + 0x28;

    const auto cycle =
        DecodeVif(ReadLE<std::uint32_t>(blob + pos));

    const auto unpack =
        DecodeVif(ReadLE<std::uint32_t>(blob + pos + 4));

    pos += 8;

    if (cycle.cmd != 0x01 ||
        cycle.immediate != 0x0203 ||
        unpack.cmd != 0x6C ||
        unpack.num !=
            static_cast<std::uint8_t>(vertexCount * 2))
    {
        throw std::runtime_error("unexpected line vertex VIF layout");
    }

    if (pos + static_cast<std::size_t>(vertexCount) * 32 >
        packetOffset + packetBytes)
    {
        throw std::runtime_error("line vertex stream outside packet");
    }

    PrimitiveData result;
    result.mode = 3; // glTF LINE_STRIP
    result.texture = state.texture;
    result.vertices.resize(vertexCount);

    for (std::uint32_t i = 0; i < vertexCount; ++i)
    {
        const auto* v =
            blob + pos + static_cast<std::size_t>(i) * 32;

        Vertex out;

        out.position = {
            ReadLE<float>(v + 0),
            ReadLE<float>(v + 4),
            ReadLE<float>(v + 8)
        };

        // The second V4-32 qword contains the authored two-component
        // line texture coordinates followed by two zero floats.
        out.uv = {
            ReadLE<float>(v + 16),
            ReadLE<float>(v + 20)
        };

        out.normal = {0.0f, 1.0f, 0.0f};

        result.vertices[i] = out;
    }

    const std::size_t mscntOffset =
        packetOffset + packetBytes - 16;

    if (DecodeVif(
            ReadLE<std::uint32_t>(blob + mscntOffset)).cmd != 0x17)
    {
        throw std::runtime_error("line packet missing MSCNT");
    }

    if (state.colorCount == vertexCount &&
        static_cast<std::uint64_t>(state.colorOffset) +
            static_cast<std::uint64_t>(vertexCount) * 4 <=
            blobSize)
    {
        for (std::uint32_t i = 0; i < vertexCount; ++i)
        {
            const auto* c =
                blob + state.colorOffset + i * 4;

            result.vertices[i].rgba = {
                GameZColorTo8(c[0]),
                GameZColorTo8(c[1]),
                GameZColorTo8(c[2]),
                GameZColorTo8(c[3])
            };
        }
    }

    return result;
}

struct RenderListResult
{
    std::vector<PrimitiveData> primitives;
    std::uint32_t triangleVertices{};
    std::uint32_t triangles{};
};

static RenderListResult ParseWorldRenderList(
    const ZarArchive& geometryArchive,
    const Node& worldModel,
    const Node& renderChild)
{
    if (renderChild.dataSize != 4)
        throw std::runtime_error("worldmodel child pointer is not 4 bytes");

    const std::uint32_t listOffset =
        ReadLE<std::uint32_t>(
            geometryArchive.PayloadAt(
                renderChild.dataOffset,
                4));

    const auto* blob =
        geometryArchive.PayloadAt(
            worldModel.dataOffset,
            worldModel.dataSize);

    const std::size_t blobSize =
        worldModel.dataSize;

    if (listOffset + 16 > blobSize)
        throw std::runtime_error("world render list outside model blob");

    const std::uint32_t recordCount =
        ReadLE<std::uint32_t>(blob + listOffset);

    if (static_cast<std::uint64_t>(listOffset) +
        16ull +
        static_cast<std::uint64_t>(recordCount) * 16ull >
        blobSize)
    {
        throw std::runtime_error("world render list truncated");
    }

    RenderState state;
    RenderListResult result;

    for (std::uint32_t i = 0; i < recordCount; ++i)
    {
        const auto* r =
            blob + listOffset + 16 +
            static_cast<std::size_t>(i) * 16;

        const std::uint32_t w0 = ReadLE<std::uint32_t>(r + 0);
        const std::uint32_t w1 = ReadLE<std::uint32_t>(r + 4);
        const std::uint32_t w2 = ReadLE<std::uint32_t>(r + 8);
        const std::uint32_t w3 = ReadLE<std::uint32_t>(r + 12);

        (void)w2;

        const std::uint32_t type =
            w0 & 0xFFFF0000u;

        if (w0 == 0x10060000u)
        {
            state.texture =
                ReadCString(blob, blobSize, w1);
        }
        else if (type == 0x30040000u)
        {
            state.colorOffset = w1;
            state.colorCount =
                static_cast<std::uint32_t>((w3 >> 16) & 0xFF);
        }
        else if (type == 0x30020000u)
        {
            PrimitiveData p =
                ParseWorldTrianglePacket(
                    blob,
                    blobSize,
                    w1,
                    state);

            result.triangleVertices +=
                static_cast<std::uint32_t>(p.vertices.size());

            result.triangles +=
                static_cast<std::uint32_t>(
                    p.indices.size() / 3);

            result.primitives.push_back(std::move(p));
        }
        else if (type == 0x30010000u)
        {
            const std::uint32_t qwc =
                w0 & 0xFFFFu;

            result.primitives.push_back(
                ParseWorldLinePacket(
                    blob,
                    blobSize,
                    w1,
                    qwc,
                    state));
        }
        else
        {
            // 0x3003 and 0x3103 are material/render-state records.
            // They do not carry geometry needed by this exporter.
        }
    }

    return result;
}

// ---------------------------------------------------------------------
// Texture decoding
// ---------------------------------------------------------------------

struct Rgba
{
    std::uint8_t r{}, g{}, b{}, a{};
};

struct Tex0
{
    std::uint32_t psm{};
    std::uint32_t cbp{};
    std::uint32_t cpsm{};
    std::uint32_t csm{};
};

static Tex0 DecodeTex0(std::uint64_t v)
{
    Tex0 t;
    t.psm = static_cast<std::uint32_t>((v >> 20) & 0x3F);
    t.cbp = static_cast<std::uint32_t>((v >> 37) & 0x3FFF);
    t.cpsm = static_cast<std::uint32_t>((v >> 51) & 0x0F);
    t.csm = static_cast<std::uint32_t>((v >> 55) & 0x01);
    return t;
}

static std::size_t Csm1Index(std::size_t p)
{
    return
        (p & 0xE7u) +
        ((p & 0x08u) << 1) +
        ((p & 0x10u) >> 1);
}

static std::uint8_t Expand5(std::uint32_t v)
{
    return static_cast<std::uint8_t>((v << 3) | (v >> 2));
}

static std::uint8_t Ps2AlphaTo8(std::uint8_t a)
{
    return static_cast<std::uint8_t>(
        std::min<unsigned>(
            255u,
            static_cast<unsigned>(a) * 2u));
}

struct Palette
{
    std::array<Rgba,256> colors{};
};

static std::map<std::uint32_t, Palette>
LoadPalettes(const ZarArchive& archive)
{
    const Node* root =
        FindTopLevel(archive.Root(), "palettes");

    if (!root)
        throw std::runtime_error("palette archive missing palettes root");

    std::map<std::uint32_t, Palette> result;

    for (const auto& n : root->children)
    {
        const Node* par = FindChild(n, "par");
        const Node* buf = FindChild(n, "buf");

        if (!par || !buf || par->dataSize != 8)
            throw std::runtime_error("bad palette node " + n.name);

        const auto* p =
            archive.PayloadAt(par->dataOffset, par->dataSize);

        const std::uint32_t id =
            ReadLE<std::uint32_t>(p + 0);

        const auto* raw =
            archive.PayloadAt(buf->dataOffset, buf->dataSize);

        std::array<Rgba,256> physical{};

        if (buf->dataSize == 0x200)
        {
            for (std::size_t i = 0; i < 256; ++i)
            {
                const std::uint16_t c =
                    ReadLE<std::uint16_t>(raw + i * 2);

                physical[i] = {
                    Expand5((c >> 0) & 0x1F),
                    Expand5((c >> 5) & 0x1F),
                    Expand5((c >> 10) & 0x1F),
                    static_cast<std::uint8_t>(
                        (c & 0x8000u) ? 255 : 0)
                };
            }
        }
        else if (buf->dataSize == 0x400)
        {
            for (std::size_t i = 0; i < 256; ++i)
            {
                physical[i] = {
                    raw[i * 4 + 0],
                    raw[i * 4 + 1],
                    raw[i * 4 + 2],
                    Ps2AlphaTo8(raw[i * 4 + 3])
                };
            }
        }
        else
        {
            throw std::runtime_error(
                "unsupported palette size in " + n.name);
        }

        Palette palette;

        for (std::size_t i = 0; i < 256; ++i)
            palette.colors[Csm1Index(i)] = physical[i];

        result[id] = palette;
    }

    return result;
}

struct TextureInfo
{
    std::string name;
    std::uint16_t width{};
    std::uint16_t height{};
    std::uint32_t texelBytes{};
    Tex0 tex0{};
    const Node* dataNode{};
};

static std::map<std::string, TextureInfo>
LoadTextureMetadata(const ZarArchive& archive)
{
    const Node* root =
        FindTopLevel(archive.Root(), "textures");

    if (!root)
        throw std::runtime_error("texture archive missing textures root");

    std::map<std::string, TextureInfo> result;

    for (const auto& n : root->children)
    {
        const Node* dataNode =
            FindChild(n, "texdat");

        if (!dataNode || dataNode->dataSize < 0xA0)
            throw std::runtime_error("bad texdat node " + n.name);

        const auto* raw =
            archive.PayloadAt(
                dataNode->dataOffset,
                dataNode->dataSize);

        TextureInfo info;
        info.name = n.name;
        info.width = ReadLE<std::uint16_t>(raw + 0);
        info.height = ReadLE<std::uint16_t>(raw + 2);
        info.texelBytes = ReadLE<std::uint32_t>(raw + 4);
        info.dataNode = dataNode;

        const std::size_t tex0Offset =
            0x20u +
            static_cast<std::size_t>(info.texelBytes) +
            0x40u;

        if (tex0Offset + 8 > dataNode->dataSize)
            throw std::runtime_error("texdat trailer truncated");

        info.tex0 =
            DecodeTex0(
                ReadLE<std::uint64_t>(raw + tex0Offset));

        result[info.name] = info;
    }

    return result;
}

// ---------------------- Minimal PNG writer ----------------------------

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

            crc =
                (crc >> 1) ^
                (0xEDB88320u & mask);
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
    std::uint32_t v)
{
    out.push_back(
        static_cast<std::uint8_t>((v >> 24) & 0xFF));
    out.push_back(
        static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(
        static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(
        static_cast<std::uint8_t>(v & 0xFF));
}

static void AppendChunk(
    std::vector<std::uint8_t>& png,
    const std::array<char,4>& type,
    const std::vector<std::uint8_t>& data)
{
    AppendBE32(
        png,
        static_cast<std::uint32_t>(data.size()));

    const std::size_t crcStart = png.size();

    for (char c : type)
        png.push_back(
            static_cast<std::uint8_t>(c));

    png.insert(
        png.end(),
        data.begin(),
        data.end());

    AppendBE32(
        png,
        Crc32(
            png.data() + crcStart,
            4 + data.size()));
}

static void WritePng(
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

    std::vector<std::uint8_t> scan;

    scan.reserve(
        static_cast<std::size_t>(height) *
        (1u + static_cast<std::size_t>(width) * 4u));

    for (std::uint32_t y = 0; y < height; ++y)
    {
        scan.push_back(0);

        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto& c =
                pixels[
                    static_cast<std::size_t>(y) * width + x];

            scan.push_back(c.r);
            scan.push_back(c.g);
            scan.push_back(c.b);
            scan.push_back(c.a);
        }
    }

    std::vector<std::uint8_t> zlib = {
        0x78, 0x01
    };

    std::size_t pos = 0;

    while (pos < scan.size())
    {
        const std::size_t remaining =
            scan.size() - pos;

        const std::uint16_t len =
            static_cast<std::uint16_t>(
                std::min<std::size_t>(
                    remaining,
                    65535));

        const bool final =
            pos + len == scan.size();

        zlib.push_back(final ? 0x01 : 0x00);

        zlib.push_back(
            static_cast<std::uint8_t>(len & 0xFF));

        zlib.push_back(
            static_cast<std::uint8_t>((len >> 8) & 0xFF));

        const std::uint16_t nlen =
            static_cast<std::uint16_t>(~len);

        zlib.push_back(
            static_cast<std::uint8_t>(nlen & 0xFF));

        zlib.push_back(
            static_cast<std::uint8_t>((nlen >> 8) & 0xFF));

        zlib.insert(
            zlib.end(),
            scan.begin() +
                static_cast<std::ptrdiff_t>(pos),
            scan.begin() +
                static_cast<std::ptrdiff_t>(pos + len));

        pos += len;
    }

    AppendBE32(
        zlib,
        Adler32(scan.data(), scan.size()));

    std::vector<std::uint8_t> png = {
        0x89, 'P', 'N', 'G',
        0x0D, 0x0A, 0x1A, 0x0A
    };

    std::vector<std::uint8_t> ihdr;

    AppendBE32(ihdr, width);
    AppendBE32(ihdr, height);

    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);

    AppendChunk(
        png,
        {'I','H','D','R'},
        ihdr);

    AppendChunk(
        png,
        {'I','D','A','T'},
        zlib);

    AppendChunk(
        png,
        {'I','E','N','D'},
        {});

    std::ofstream out(path, std::ios::binary);

    if (!out)
        throw std::runtime_error(
            "cannot create " + path.string());

    out.write(
        reinterpret_cast<const char*>(png.data()),
        static_cast<std::streamsize>(png.size()));
}

struct DecodedTextureResult
{
    bool hasTransparency{};
};

static DecodedTextureResult DecodeAndWriteTexture(
    const ZarArchive& txr,
    const std::map<std::uint32_t, Palette>& palettes,
    const TextureInfo& info,
    const fs::path& output)
{
    const auto* raw =
        txr.PayloadAt(
            info.dataNode->dataOffset,
            info.dataNode->dataSize);

    const auto* pixelsRaw =
        raw + 0x20;

    std::vector<Rgba> pixels;

    const std::size_t pixelCount =
        static_cast<std::size_t>(info.width) *
        info.height;

    pixels.resize(pixelCount);

    if (info.tex0.psm == 0x13)
    {
        if (info.texelBytes != pixelCount)
            throw std::runtime_error(
                "PSMT8 byte count mismatch: " + info.name);

        if (info.tex0.csm != 0)
            throw std::runtime_error(
                "CSM2 not supported: " + info.name);

        const auto it =
            palettes.find(info.tex0.cbp);

        if (it == palettes.end())
            throw std::runtime_error(
                "palette missing for " + info.name);

        for (std::size_t i = 0; i < pixelCount; ++i)
            pixels[i] =
                it->second.colors[pixelsRaw[i]];
    }
    else if (info.tex0.psm == 0x00)
    {
        if (info.texelBytes != pixelCount * 4)
            throw std::runtime_error(
                "PSMCT32 byte count mismatch: " + info.name);

        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            pixels[i] = {
                pixelsRaw[i * 4 + 0],
                pixelsRaw[i * 4 + 1],
                pixelsRaw[i * 4 + 2],
                Ps2AlphaTo8(pixelsRaw[i * 4 + 3])
            };
        }
    }
    else
    {
        throw std::runtime_error(
            "unsupported texture PSM in " + info.name);
    }

    bool hasTransparency = false;

    for (const auto& p : pixels)
    {
        if (p.a != 255)
        {
            hasTransparency = true;
            break;
        }
    }

    WritePng(
        output,
        info.width,
        info.height,
        pixels);

    return {hasTransparency};
}

// ---------------------------------------------------------------------
// Scene hierarchy
// ---------------------------------------------------------------------

struct VisualRef
{
    const Node* vis{};
    const Node* vparams{};
    int sceneNode = -1;
};

struct SceneNode
{
    const Node* source{};
    int parent = -1;
    std::vector<int> children;
    std::vector<std::size_t> visualIndices;
    std::vector<PrimitiveData> primitives;
};

static void CollectScene(
    const Node& node,
    int currentParent,
    std::vector<SceneNode>& sceneNodes,
    std::vector<VisualRef>& visuals)
{
    int current = currentParent;

    if (FindChild(node, "nparams"))
    {
        SceneNode s;
        s.source = &node;
        s.parent = currentParent;

        current =
            static_cast<int>(sceneNodes.size());

        sceneNodes.push_back(std::move(s));

        if (currentParent >= 0)
            sceneNodes[
                static_cast<std::size_t>(currentParent)]
                .children.push_back(current);
    }

    // GameZ stores one logical visual component in a "visuals" node.
    // Some components contain multiple sibling "vis" records. Those are
    // alternate render/lighting variants, not geometry that should all be
    // drawn simultaneously. The first vis is the base authored variant.
    if (node.name == "visuals")
    {
        if (current < 0)
            throw std::runtime_error(
                "visuals node has no containing nparams node");

        const Node* selected = nullptr;

        for (const auto& child : node.children)
        {
            if (child.name == "vis")
            {
                selected = &child;
                break;
            }
        }

        if (selected)
        {
            const Node* vparams =
                FindChild(*selected, "vparams");

            if (!vparams || vparams->dataSize != 0x18)
                throw std::runtime_error(
                    "selected vis missing 0x18 vparams");

            const std::size_t visualIndex =
                visuals.size();

            visuals.push_back({
                selected,
                vparams,
                current
            });

            sceneNodes[
                static_cast<std::size_t>(current)]
                .visualIndices.push_back(visualIndex);
        }

        // Do not recursively collect sibling vis variants.
        for (const auto& child : node.children)
        {
            if (child.name != "vis")
                CollectScene(
                    child,
                    current,
                    sceneNodes,
                    visuals);
        }

        return;
    }

    for (const auto& child : node.children)
        CollectScene(
            child,
            current,
            sceneNodes,
            visuals);
}


struct GeometryCounters
{
    std::size_t logicalVisuals{};
    std::size_t skippedRenderVariants{};
    std::size_t trianglePackets{};
    std::size_t linePackets{};
    std::size_t vertices{};
    std::size_t triangles{};
    std::size_t lineSegments{};
};

static bool ContainsLogicalVisual(
    const Node& node)
{
    if (node.name == "visuals")
    {
        for (const auto& child : node.children)
            if (child.name == "vis")
                return true;
    }

    for (const auto& child : node.children)
        if (ContainsLogicalVisual(child))
            return true;

    return false;
}

static void AttachModelGeometry(
    const ZarArchive& geometry,
    const ZarArchive& mdl,
    const Node& geometryModel,
    const Node& modelDefinition,
    int parentSceneNode,
    std::vector<SceneNode>& sceneNodes,
    std::set<std::string>& usedTextures,
    GeometryCounters& counters)
{
    std::vector<VisualRef> visuals;

    CollectScene(
        modelDefinition,
        parentSceneNode,
        sceneNodes,
        visuals);

    if (visuals.empty())
        return;

    std::vector<RenderListResult> renderLists;
    renderLists.reserve(
        geometryModel.children.size());

    for (const auto& child :
         geometryModel.children)
    {
        renderLists.push_back(
            ParseWorldRenderList(
                geometry,
                geometryModel,
                child));
    }

    std::size_t renderCursor = 0;

    for (std::size_t i = 0;
         i < visuals.size();
         ++i)
    {
        const auto* vp =
            mdl.PayloadAt(
                visuals[i].vparams->dataOffset,
                visuals[i].vparams->dataSize);

        const std::uint32_t packedCounts =
            ReadLE<std::uint32_t>(vp + 8);

        const std::uint32_t expectedVertices =
            (packedCounts >> 16) & 0xFFFFu;

        const std::uint32_t expectedTriangles =
            packedCounts & 0xFFFFu;

        std::size_t matched =
            renderLists.size();

        for (std::size_t j = renderCursor;
             j < renderLists.size();
             ++j)
        {
            if (renderLists[j].triangleVertices ==
                    expectedVertices &&
                renderLists[j].triangles ==
                    expectedTriangles)
            {
                matched = j;
                break;
            }
        }

        if (matched == renderLists.size())
        {
            throw std::runtime_error(
                "could not map logical visual " +
                std::to_string(i) +
                " for model " +
                modelDefinition.name);
        }

        counters.skippedRenderVariants +=
            matched - renderCursor;

        renderCursor =
            matched + 1;

        auto& target =
            sceneNodes[
                static_cast<std::size_t>(
                    visuals[i].sceneNode)];

        auto& list =
            renderLists[matched];

        for (auto& primitive :
             list.primitives)
        {
            if (!primitive.texture.empty())
                usedTextures.insert(
                    primitive.texture);

            counters.vertices +=
                primitive.vertices.size();

            if (primitive.mode == 4)
            {
                ++counters.trianglePackets;

                counters.triangles +=
                    primitive.indices.size() / 3;
            }
            else
            {
                ++counters.linePackets;

                if (!primitive.vertices.empty())
                    counters.lineSegments +=
                        primitive.vertices.size() - 1;
            }

            target.primitives.push_back(
                std::move(primitive));
        }
    }

    counters.logicalVisuals +=
        visuals.size();

    counters.skippedRenderVariants +=
        renderLists.size() - renderCursor;
}

// ---------------------------------------------------------------------
// glTF binary builder
// ---------------------------------------------------------------------

struct BufferViewDesc
{
    std::size_t offset{};
    std::size_t length{};
    int target{};
};

struct AccessorDesc
{
    int view{};
    int componentType{};
    std::size_t count{};
    std::string type;
    bool normalized{};
    std::vector<double> minValues;
    std::vector<double> maxValues;
};

class BufferBuilder
{
public:
    int AddRaw(
        const void* data,
        std::size_t bytes,
        int target)
    {
        while (data_.size() % 4)
            data_.push_back(0);

        const std::size_t offset =
            data_.size();

        const auto* p =
            static_cast<const std::uint8_t*>(data);

        data_.insert(
            data_.end(),
            p,
            p + bytes);

        const int index =
            static_cast<int>(views_.size());

        views_.push_back({
            offset,
            bytes,
            target
        });

        return index;
    }

    template <typename T>
    int AddAccessor(
        const std::vector<T>& values,
        int componentType,
        std::size_t count,
        const std::string& type,
        int target,
        bool normalized = false,
        std::vector<double> minValues = {},
        std::vector<double> maxValues = {})
    {
        const int view =
            AddRaw(
                values.data(),
                values.size() * sizeof(T),
                target);

        const int index =
            static_cast<int>(accessors_.size());

        accessors_.push_back({
            view,
            componentType,
            count,
            type,
            normalized,
            std::move(minValues),
            std::move(maxValues)
        });

        return index;
    }

    const std::vector<std::uint8_t>& Data() const
    {
        return data_;
    }

    const std::vector<BufferViewDesc>& Views() const
    {
        return views_;
    }

    const std::vector<AccessorDesc>& Accessors() const
    {
        return accessors_;
    }

private:
    std::vector<std::uint8_t> data_;
    std::vector<BufferViewDesc> views_;
    std::vector<AccessorDesc> accessors_;
};

struct GltfPrimitive
{
    int position = -1;
    int normal = -1;
    int uv = -1;
    int color = -1;
    int indices = -1;
    int material = -1;
    int mode = 4;
};

struct GltfMesh
{
    std::string name;
    std::vector<GltfPrimitive> primitives;
};

static std::string JsonEscape(const std::string& s)
{
    std::ostringstream out;

    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b";  break;
            case '\f': out << "\\f";  break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;

            default:
                if (c < 0x20)
                {
                    out
                        << "\\u"
                        << std::hex
                        << std::setw(4)
                        << std::setfill('0')
                        << static_cast<int>(c)
                        << std::dec;
                }
                else
                {
                    out << static_cast<char>(c);
                }
                break;
        }
    }

    return out.str();
}

static std::string PngName(
    const std::string& original)
{
    fs::path p(original);
    p.replace_extension(".png");
    return p.filename().string();
}

static void ExportWorld(
    const fs::path& geometryPath,
    const fs::path& mdlPath,
    const fs::path& txrPath,
    const fs::path& palPath,
    const fs::path& outDir)
{
    ZarArchive geometry(geometryPath);
    ZarArchive mdl(mdlPath);
    ZarArchive txr(txrPath);
    ZarArchive pal(palPath);

    const Node* worldGeometry =
        FindTopLevel(
            geometry.Root(),
            "worldmodel");

    if (!worldGeometry)
        throw std::runtime_error(
            "models.zar has no worldmodel");

    const Node* modelsRoot =
        FindTopLevel(
            mdl.Root(),
            "models");

    if (!modelsRoot)
        throw std::runtime_error(
            "_mdl.zed has no models root");

    const Node* worldDefinition =
        FindTopLevel(
            *modelsRoot,
            "worldmodel");

    if (!worldDefinition)
        throw std::runtime_error(
            "_mdl.zed models has no worldmodel");

    std::vector<SceneNode> sceneNodes;
    std::set<std::string> usedTextures;
    GeometryCounters counters;

    // Base world geometry.
    AttachModelGeometry(
        geometry,
        mdl,
        *worldGeometry,
        *worldDefinition,
        -1,
        sceneNodes,
        usedTextures,
        counters);

    if (sceneNodes.empty())
        throw std::runtime_error(
            "world hierarchy is empty");

    // Capture only the authored world nodes before adding referenced model
    // definitions. model_name records inside reusable definitions describe
    // their own behavior/state and are not recursively instantiated here.
    const std::size_t authoredWorldNodeCount =
        sceneNodes.size();

    std::size_t modelReferences = 0;
    std::size_t instantiatedModels = 0;
    std::vector<std::string> skippedModelReferences;

    for (std::size_t i = 0;
         i < authoredWorldNodeCount;
         ++i)
    {
        const Node* modelNameNode =
            FindChild(
                *sceneNodes[i].source,
                "model_name");

        if (!modelNameNode)
            continue;

        ++modelReferences;

        const std::string modelName =
            ReadNodeString(
                mdl,
                *modelNameNode);

        const Node* definition =
            FindTopLevel(
                *modelsRoot,
                modelName);

        const Node* geometryModel =
            FindTopLevel(
                geometry.Root(),
                modelName);

        if (!definition ||
            !geometryModel ||
            !ContainsLogicalVisual(*definition) ||
            geometryModel->children.empty())
        {
            skippedModelReferences.push_back(
                modelName);
            continue;
        }

        AttachModelGeometry(
            geometry,
            mdl,
            *geometryModel,
            *definition,
            static_cast<int>(i),
            sceneNodes,
            usedTextures,
            counters);

        ++instantiatedModels;
    }

    fs::create_directories(outDir);
    fs::create_directories(outDir / "textures");

    const auto palettes =
        LoadPalettes(pal);

    const auto textureInfo =
        LoadTextureMetadata(txr);

    std::vector<std::string> textureNames(
        usedTextures.begin(),
        usedTextures.end());

    std::map<std::string,int> materialIndex;
    std::map<std::string,bool> materialHasTransparency;

    for (std::size_t i = 0;
         i < textureNames.size();
         ++i)
    {
        const std::string& name =
            textureNames[i];

        const auto it =
            textureInfo.find(name);

        bool hasTransparency = false;

        if (it == textureInfo.end())
        {
            // Some M8 props refer to shared/placeholder textures from
            // another asset library (for example flare_red or null_xmas).
            // Preserve the material name and emit a visible placeholder
            // rather than aborting the entire world conversion.
            const std::vector<Rgba> missingPixels = {
                {255, 0, 255, 255},
                {32, 32, 32, 255},
                {32, 32, 32, 255},
                {255, 0, 255, 255}
            };

            WritePng(
                outDir / "textures" / PngName(name),
                2,
                2,
                missingPixels);
        }
        else
        {
            const auto result =
                DecodeAndWriteTexture(
                    txr,
                    palettes,
                    it->second,
                    outDir / "textures" / PngName(name));

            hasTransparency =
                result.hasTransparency;
        }

        materialIndex[name] =
            static_cast<int>(i);

        materialHasTransparency[name] =
            hasTransparency;
    }

    BufferBuilder builder;
    std::vector<GltfMesh> gltfMeshes;
    std::vector<int> sceneMeshIndex(
        sceneNodes.size(),
        -1);

    for (std::size_t nodeIndex = 0;
         nodeIndex < sceneNodes.size();
         ++nodeIndex)
    {
        const auto& source =
            sceneNodes[nodeIndex];

        if (source.primitives.empty())
            continue;

        GltfMesh mesh;
        mesh.name = source.source->name;

        for (const auto& p : source.primitives)
        {
            if (p.vertices.empty())
                continue;

            std::vector<float> positions;
            std::vector<float> normals;
            std::vector<float> uvs;
            std::vector<std::uint8_t> colors;

            positions.reserve(
                p.vertices.size() * 3);

            normals.reserve(
                p.vertices.size() * 3);

            uvs.reserve(
                p.vertices.size() * 2);

            colors.reserve(
                p.vertices.size() * 4);

            std::array<double,3> minPos{
                1e30,1e30,1e30
            };

            std::array<double,3> maxPos{
                -1e30,-1e30,-1e30
            };

            for (const auto& v : p.vertices)
            {
                const float xyz[3] = {
                    v.position.x,
                    v.position.y,
                    v.position.z
                };

                const float nrm[3] = {
                    v.normal.x,
                    v.normal.y,
                    v.normal.z
                };

                for (int k = 0; k < 3; ++k)
                {
                    positions.push_back(xyz[k]);

                    minPos[k] =
                        std::min(
                            minPos[k],
                            static_cast<double>(xyz[k]));

                    maxPos[k] =
                        std::max(
                            maxPos[k],
                            static_cast<double>(xyz[k]));
                }

                if (p.mode == 4)
                {
                    normals.push_back(nrm[0]);
                    normals.push_back(nrm[1]);
                    normals.push_back(nrm[2]);
                }

                uvs.push_back(v.uv.x);
                uvs.push_back(v.uv.y);

                colors.push_back(v.rgba[0]);
                colors.push_back(v.rgba[1]);
                colors.push_back(v.rgba[2]);
                colors.push_back(v.rgba[3]);
            }

            GltfPrimitive gp;
            gp.mode = p.mode;

            gp.position =
                builder.AddAccessor(
                    positions,
                    5126,
                    p.vertices.size(),
                    "VEC3",
                    34962,
                    false,
                    {minPos[0],minPos[1],minPos[2]},
                    {maxPos[0],maxPos[1],maxPos[2]});

            gp.uv =
                builder.AddAccessor(
                    uvs,
                    5126,
                    p.vertices.size(),
                    "VEC2",
                    34962);

            gp.color =
                builder.AddAccessor(
                    colors,
                    5121,
                    p.vertices.size(),
                    "VEC4",
                    34962,
                    true);

            if (p.mode == 4)
            {
                gp.normal =
                    builder.AddAccessor(
                        normals,
                        5126,
                        p.vertices.size(),
                        "VEC3",
                        34962);

                gp.indices =
                    builder.AddAccessor(
                        p.indices,
                        5123,
                        p.indices.size(),
                        "SCALAR",
                        34963);
            }

            const auto mit =
                materialIndex.find(p.texture);

            if (mit == materialIndex.end())
                throw std::runtime_error(
                    "no material for texture " + p.texture);

            gp.material = mit->second;

            mesh.primitives.push_back(gp);
        }

        sceneMeshIndex[nodeIndex] =
            static_cast<int>(gltfMeshes.size());

        gltfMeshes.push_back(std::move(mesh));
    }

    const fs::path binPath =
        outDir / "m8_scene.bin";

    std::ofstream bin(binPath, std::ios::binary);

    if (!bin)
        throw std::runtime_error(
            "cannot create " + binPath.string());

    const auto& binary =
        builder.Data();

    bin.write(
        reinterpret_cast<const char*>(binary.data()),
        static_cast<std::streamsize>(binary.size()));

    const fs::path gltfPath =
        outDir / "m8_scene.gltf";

    std::ofstream out(gltfPath);

    if (!out)
        throw std::runtime_error(
            "cannot create " + gltfPath.string());

    out << std::setprecision(9);

    out <<
        "{\n"
        "\"asset\":{\"version\":\"2.0\","
        "\"generator\":\"SOCOM GameZ M8 world exporter\"},\n"
        "\"scene\":0,\n"
        "\"scenes\":[{\"nodes\":[0]}],\n";

    // Synthetic root converts GameZ world units to meters.
    out <<
        "\"nodes\":["
        "{\"name\":\"M8_world\",\"scale\":[0.1,0.1,0.1],\"children\":[1]}";

    for (std::size_t i = 0;
         i < sceneNodes.size();
         ++i)
    {
        const auto& s = sceneNodes[i];

        const Node* nparams =
            FindChild(*s.source, "nparams");

        if (!nparams ||
            nparams->dataSize < 0x40)
        {
            throw std::runtime_error(
                "scene node missing nparams");
        }

        const auto* raw =
            mdl.PayloadAt(
                nparams->dataOffset,
                nparams->dataSize);

        std::array<float,16> matrix{};

        std::memcpy(
            matrix.data(),
            raw,
            sizeof(float) * 16);

        out <<
            ",{\"name\":\""
            << JsonEscape(s.source->name)
            << "\",\"matrix\":[";

        for (std::size_t k = 0;
             k < matrix.size();
             ++k)
        {
            if (k)
                out << ',';

            out << matrix[k];
        }

        out << ']';

        if (sceneMeshIndex[i] >= 0)
            out <<
                ",\"mesh\":"
                << sceneMeshIndex[i];

        if (!s.children.empty())
        {
            out << ",\"children\":[";

            for (std::size_t c = 0;
                 c < s.children.size();
                 ++c)
            {
                if (c)
                    out << ',';

                // +1 for synthetic root.
                out <<
                    (s.children[c] + 1);
            }

            out << ']';
        }

        out << '}';
    }

    out << "],\n";

    out << "\"meshes\":[";

    for (std::size_t i = 0;
         i < gltfMeshes.size();
         ++i)
    {
        if (i)
            out << ',';

        const auto& mesh =
            gltfMeshes[i];

        out <<
            "{\"name\":\""
            << JsonEscape(mesh.name)
            << "\",\"primitives\":[";

        for (std::size_t p = 0;
             p < mesh.primitives.size();
             ++p)
        {
            if (p)
                out << ',';

            const auto& gp =
                mesh.primitives[p];

            out <<
                "{\"attributes\":{\"POSITION\":"
                << gp.position
                << ",\"TEXCOORD_0\":"
                << gp.uv
                << ",\"COLOR_0\":"
                << gp.color;

            if (gp.normal >= 0)
                out <<
                    ",\"NORMAL\":"
                    << gp.normal;

            out << '}';

            if (gp.indices >= 0)
                out <<
                    ",\"indices\":"
                    << gp.indices;

            out <<
                ",\"material\":"
                << gp.material
                << ",\"mode\":"
                << gp.mode
                << '}';
        }

        out << "]}";
    }

    out << "],\n";

    out <<
        "\"samplers\":[{\"magFilter\":9729,"
        "\"minFilter\":9987,"
        "\"wrapS\":10497,"
        "\"wrapT\":10497}],\n";

    out << "\"images\":[";

    for (std::size_t i = 0;
         i < textureNames.size();
         ++i)
    {
        if (i)
            out << ',';

        out <<
            "{\"uri\":\"textures/"
            << JsonEscape(PngName(textureNames[i]))
            << "\"}";
    }

    out << "],\n";

    out << "\"textures\":[";

    for (std::size_t i = 0;
         i < textureNames.size();
         ++i)
    {
        if (i)
            out << ',';

        out <<
            "{\"sampler\":0,\"source\":"
            << i
            << '}';
    }

    out << "],\n";

    out << "\"materials\":[";

    for (std::size_t i = 0;
         i < textureNames.size();
         ++i)
    {
        if (i)
            out << ',';

        const auto& name =
            textureNames[i];

        out <<
            "{\"name\":\""
            << JsonEscape(name)
            << "\",\"pbrMetallicRoughness\":{"
            "\"baseColorTexture\":{\"index\":"
            << i
            << "},\"metallicFactor\":0,"
            "\"roughnessFactor\":1},"
            "\"doubleSided\":true";

        if (materialHasTransparency[name])
            out << ",\"alphaMode\":\"BLEND\"";

        out << '}';
    }

    out << "],\n";

    out <<
        "\"buffers\":[{\"byteLength\":"
        << builder.Data().size()
        << ",\"uri\":\"m8_scene.bin\"}],\n";

    out << "\"bufferViews\":[";

    const auto& views =
        builder.Views();

    for (std::size_t i = 0;
         i < views.size();
         ++i)
    {
        if (i)
            out << ',';

        out <<
            "{\"buffer\":0,"
            "\"byteOffset\":"
            << views[i].offset
            << ",\"byteLength\":"
            << views[i].length;

        if (views[i].target)
            out <<
                ",\"target\":"
                << views[i].target;

        out << '}';
    }

    out << "],\n";

    out << "\"accessors\":[";

    const auto& accessors =
        builder.Accessors();

    for (std::size_t i = 0;
         i < accessors.size();
         ++i)
    {
        if (i)
            out << ',';

        const auto& a =
            accessors[i];

        out <<
            "{\"bufferView\":"
            << a.view
            << ",\"byteOffset\":0,"
            "\"componentType\":"
            << a.componentType
            << ",\"count\":"
            << a.count
            << ",\"type\":\""
            << a.type
            << "\"";

        if (a.normalized)
            out << ",\"normalized\":true";

        if (!a.minValues.empty())
        {
            out << ",\"min\":[";

            for (std::size_t k = 0;
                 k < a.minValues.size();
                 ++k)
            {
                if (k)
                    out << ',';

                out << a.minValues[k];
            }

            out << ']';
        }

        if (!a.maxValues.empty())
        {
            out << ",\"max\":[";

            for (std::size_t k = 0;
                 k < a.maxValues.size();
                 ++k)
            {
                if (k)
                    out << ',';

                out << a.maxValues[k];
            }

            out << ']';
        }

        out << '}';
    }

    out << "]\n}\n";

    std::cout
        << "Exported M8 full scene:\n"
        << "  glTF: " << gltfPath << '\n'
        << "  scene nodes: " << sceneNodes.size() << '\n'
        << "  logical visual components: "
        << counters.logicalVisuals << '\n'
        << "  skipped render variants: "
        << counters.skippedRenderVariants << '\n'
        << "  placed model references: "
        << modelReferences << '\n'
        << "  instantiated reusable models: "
        << instantiatedModels << '\n'
        << "  skipped special/empty model references: "
        << skippedModelReferences.size() << '\n'
        << "  triangle packets: "
        << counters.trianglePackets << '\n'
        << "  line packets: "
        << counters.linePackets << '\n'
        << "  vertices: "
        << counters.vertices << '\n'
        << "  triangles: "
        << counters.triangles << '\n'
        << "  line segments: "
        << counters.lineSegments << '\n'
        << "  used textures: "
        << textureNames.size() << '\n';

    if (!skippedModelReferences.empty())
    {
        std::cout << "  skipped refs:";

        for (const auto& name :
             skippedModelReferences)
            std::cout << ' ' << name;

        std::cout << '\n';
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 6)
        {
            std::cerr <<
                "Usage:\n"
                "  socom_world_gltf <models.zar> <*_mdl.zed> "
                "<*_txr.zed> <*_pal.zed> <output_dir>\n";

            return 1;
        }

        ExportWorld(
            fs::path(argv[1]),
            fs::path(argv[2]),
            fs::path(argv[3]),
            fs::path(argv[4]),
            fs::path(argv[5]));

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "error: "
            << e.what()
            << '\n';

        return 2;
    }
}
