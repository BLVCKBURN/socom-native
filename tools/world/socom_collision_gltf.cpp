// socom_collision_gltf.cpp
// SOCOM 1 GameZ CNode DI collision exporter.
// C++17, no third-party dependencies.
//
// Confirmed M8 DI record:
//   params (0x1C bytes)
//     +0x00 float normal.x
//     +0x04 float normal.y
//     +0x08 float normal.z
//     +0x0C uint32 0
//     +0x10 uint32 0
//     +0x14 uint32 surface/material ID
//     +0x18 uint32 collision/property flags
//
//   points
//     N * 16 bytes:
//       float x,y,z,w
//     w is 0 in the observed collision polygons.
//
// Polygon winding matches the stored normal. Polygons are convex in the
// observed M8 data and are fan-triangulated for glTF.
//
// The exporter preserves the original CNode hierarchy and nparams matrices.
// A synthetic root scales GameZ units by 0.1 to meters.
//
// Build:
//   clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
//     socom_collision_gltf.cpp -o socom_collision_gltf
//
// Usage:
//   ./socom_collision_gltf m8_mdl.zed worldmodel out_dir
//
// Output:
//   out_dir/worldmodel_collision.gltf
//   out_dir/worldmodel_collision.bin
//   out_dir/worldmodel_collision.csv

#include <algorithm>
#include <array>
#include <cmath>
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

    const auto r = v % a;
    return r ? v + (a - r) : v;
}

struct Node
{
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
            throw std::runtime_error("file too small");

        bytes_.resize(static_cast<std::size_t>(length));

        in.read(
            reinterpret_cast<char*>(bytes_.data()),
            static_cast<std::streamsize>(bytes_.size()));

        if (!in)
            throw std::runtime_error("read failed");

        std::memcpy(&header_, bytes_.data(), sizeof(header_));

        if (header_.version != 0x00020002)
            throw std::runtime_error("unexpected ZAR version");

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
            throw std::runtime_error("payload does not reach EOF");

        disk_.resize(header_.nodeCount);

        std::memcpy(
            disk_.data(),
            bytes_.data() + nodesOffset_,
            static_cast<std::size_t>(nodeBytes));

        std::size_t cursor = 0;
        root_ = ParseNode(cursor);

        if (cursor != disk_.size())
            throw std::runtime_error("tree parse mismatch");
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
        if (!address)
            return "<root>";

        if (address < header_.preferredBase)
            throw std::runtime_error("bad name pointer");

        const std::uint64_t rel =
            static_cast<std::uint64_t>(address) -
            header_.preferredBase;

        if (rel >= header_.stringBlockSize)
            throw std::runtime_error("name outside string block");

        const std::size_t start =
            stringsOffset_ + static_cast<std::size_t>(rel);

        const std::size_t limit =
            stringsOffset_ + header_.stringBlockSize;

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

        const ZarNodeDisk d = disk_[cursor++];

        Node node;
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
    std::vector<ZarNodeDisk> disk_;
    Node root_;
    std::size_t stringsOffset_{};
    std::size_t nodesOffset_{};
    std::size_t payloadOffset_{};
};

static const Node* FindChild(
    const Node& parent,
    const std::string& name)
{
    for (const auto& child : parent.children)
        if (child.name == name)
            return &child;

    return nullptr;
}

static const Node* FindTopLevel(
    const Node& root,
    const std::string& name)
{
    for (const auto& child : root.children)
        if (child.name == name)
            return &child;

    return nullptr;
}

struct Vec3
{
    float x{}, y{}, z{};
};

struct CollisionPolygon
{
    Vec3 normal;
    std::uint32_t materialId{};
    std::uint32_t flags{};
    std::vector<Vec3> points;
};

struct SceneNode
{
    const Node* source{};
    int parent = -1;
    std::vector<int> children;
    std::vector<CollisionPolygon> polygons;
};

static double Length(const Vec3& v)
{
    return std::sqrt(
        static_cast<double>(v.x) * v.x +
        static_cast<double>(v.y) * v.y +
        static_cast<double>(v.z) * v.z);
}

static Vec3 Sub(const Vec3& a, const Vec3& b)
{
    return {
        a.x - b.x,
        a.y - b.y,
        a.z - b.z
    };
}

static Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static double Dot(const Vec3& a, const Vec3& b)
{
    return
        static_cast<double>(a.x) * b.x +
        static_cast<double>(a.y) * b.y +
        static_cast<double>(a.z) * b.z;
}

static CollisionPolygon DecodeDi(
    const ZarArchive& archive,
    const Node& di)
{
    const Node* params =
        FindChild(di, "params");

    const Node* points =
        FindChild(di, "points");

    if (!params || !points)
        throw std::runtime_error("DI record missing params/points");

    if (params->dataSize != 0x1C)
        throw std::runtime_error("unexpected DI params size");

    if (points->dataSize < 0x30 ||
        points->dataSize % 16 != 0)
    {
        throw std::runtime_error("unexpected DI points size");
    }

    const auto* p =
        archive.PayloadAt(
            params->dataOffset,
            params->dataSize);

    CollisionPolygon out;

    out.normal = {
        ReadLE<float>(p + 0),
        ReadLE<float>(p + 4),
        ReadLE<float>(p + 8)
    };

    const std::uint32_t zero0 =
        ReadLE<std::uint32_t>(p + 0x0C);

    const std::uint32_t zero1 =
        ReadLE<std::uint32_t>(p + 0x10);

    if (zero0 != 0 || zero1 != 0)
        throw std::runtime_error("unexpected nonzero DI reserved fields");

    out.materialId =
        ReadLE<std::uint32_t>(p + 0x14);

    out.flags =
        ReadLE<std::uint32_t>(p + 0x18);

    if (std::abs(Length(out.normal) - 1.0) > 0.001)
        throw std::runtime_error("DI normal is not unit length");

    const auto* rawPoints =
        archive.PayloadAt(
            points->dataOffset,
            points->dataSize);

    const std::size_t pointCount =
        points->dataSize / 16;

    out.points.reserve(pointCount);

    for (std::size_t i = 0; i < pointCount; ++i)
    {
        const auto* q =
            rawPoints + i * 16;

        const Vec3 v{
            ReadLE<float>(q + 0),
            ReadLE<float>(q + 4),
            ReadLE<float>(q + 8)
        };

        // The fourth dword is not a homogeneous W value. It is auxiliary
        // per-point data and can contain packed/non-float bits. XYZ are the
        // collision position fields used by the intersection polygon.
        out.points.push_back(v);
    }

    // Validate polygon winding against the stored plane normal.
    const Vec3 e0 =
        Sub(out.points[1], out.points[0]);

    const Vec3 e1 =
        Sub(out.points[2], out.points[0]);

    const Vec3 c =
        Cross(e0, e1);

    const double len = Length(c);

    if (len > 1e-10)
    {
        Vec3 cn{
            static_cast<float>(c.x / len),
            static_cast<float>(c.y / len),
            static_cast<float>(c.z / len)
        };

        if (Dot(cn, out.normal) < 0.999)
            throw std::runtime_error(
                "DI polygon winding disagrees with stored normal");
    }

    return out;
}

static void CollectScene(
    const ZarArchive& archive,
    const Node& node,
    int currentParent,
    std::vector<SceneNode>& scene)
{
    int current = currentParent;

    if (FindChild(node, "nparams"))
    {
        SceneNode s;
        s.source = &node;
        s.parent = currentParent;

        current =
            static_cast<int>(scene.size());

        scene.push_back(std::move(s));

        if (currentParent >= 0)
            scene[
                static_cast<std::size_t>(currentParent)]
                .children.push_back(current);
    }

    if (node.name == "di" &&
        FindChild(node, "params") &&
        FindChild(node, "points"))
    {
        if (current < 0)
            throw std::runtime_error(
                "DI polygon has no containing CNode");

        scene[
            static_cast<std::size_t>(current)]
            .polygons.push_back(
                DecodeDi(archive, node));
    }

    for (const auto& child : node.children)
        CollectScene(
            archive,
            child,
            current,
            scene);
}

struct BufferView
{
    std::size_t offset{};
    std::size_t length{};
    int target{};
};

struct Accessor
{
    int view{};
    int componentType{};
    std::size_t count{};
    std::string type;
    std::vector<double> minValues;
    std::vector<double> maxValues;
};

class BinaryBuilder
{
public:
    int AddRaw(
        const void* data,
        std::size_t bytes,
        int target)
    {
        while (bytes_.size() % 4)
            bytes_.push_back(0);

        const std::size_t offset =
            bytes_.size();

        const auto* p =
            static_cast<const std::uint8_t*>(data);

        bytes_.insert(
            bytes_.end(),
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
            std::move(minValues),
            std::move(maxValues)
        });

        return index;
    }

    const auto& Bytes() const { return bytes_; }
    const auto& Views() const { return views_; }
    const auto& Accessors() const { return accessors_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<BufferView> views_;
    std::vector<Accessor> accessors_;
};

struct Primitive
{
    int position{-1};
    int normal{-1};
    int indices{-1};
    int material{-1};
};

struct Mesh
{
    std::string name;
    std::vector<Primitive> primitives;
};

static std::string JsonEscape(const std::string& s)
{
    std::ostringstream out;

    for (unsigned char c : s)
    {
        if (c == '"')
            out << "\\\"";
        else if (c == '\\')
            out << "\\\\";
        else if (c == '\n')
            out << "\\n";
        else if (c == '\r')
            out << "\\r";
        else if (c == '\t')
            out << "\\t";
        else
            out << static_cast<char>(c);
    }

    return out.str();
}

static std::string CsvQuote(const std::string& s)
{
    std::string out = "\"";

    for (char c : s)
    {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }

    out += '"';
    return out;
}

static void ExportCollision(
    const fs::path& mdlPath,
    const std::string& modelName,
    const fs::path& outDir)
{
    ZarArchive archive(mdlPath);

    const Node* models =
        FindTopLevel(archive.Root(), "models");

    if (!models)
        throw std::runtime_error("_mdl.zed missing models root");

    const Node* model =
        FindTopLevel(*models, modelName);

    if (!model)
        throw std::runtime_error(
            "model not found: " + modelName);

    std::vector<SceneNode> scene;

    CollectScene(
        archive,
        *model,
        -1,
        scene);

    if (scene.empty())
        throw std::runtime_error("model hierarchy is empty");

    std::map<std::uint32_t,std::size_t> materialCounts;
    std::map<std::uint32_t,std::size_t> flagCounts;

    std::size_t polygonCount = 0;
    std::size_t sourcePointCount = 0;
    std::size_t triangleCount = 0;

    for (const auto& s : scene)
    {
        for (const auto& p : s.polygons)
        {
            ++polygonCount;
            sourcePointCount += p.points.size();
            triangleCount += p.points.size() - 2;
            ++materialCounts[p.materialId];
            ++flagCounts[p.flags];
        }
    }

    fs::create_directories(outDir);

    std::vector<std::uint32_t> materialIds;

    for (const auto& kv : materialCounts)
        materialIds.push_back(kv.first);

    std::map<std::uint32_t,int> materialIndex;

    for (std::size_t i = 0; i < materialIds.size(); ++i)
        materialIndex[materialIds[i]] =
            static_cast<int>(i);

    BinaryBuilder builder;

    std::vector<Mesh> meshes;
    std::vector<int> meshForNode(
        scene.size(),
        -1);

    for (std::size_t nodeIndex = 0;
         nodeIndex < scene.size();
         ++nodeIndex)
    {
        const auto& s =
            scene[nodeIndex];

        if (s.polygons.empty())
            continue;

        std::map<
            std::uint32_t,
            std::vector<const CollisionPolygon*>>
            grouped;

        for (const auto& p : s.polygons)
            grouped[p.materialId].push_back(&p);

        Mesh mesh;
        mesh.name =
            s.source->name + "_collision";

        for (const auto& group : grouped)
        {
            std::vector<float> positions;
            std::vector<float> normals;
            std::vector<std::uint32_t> indices;

            std::array<double,3> minPos{
                1e30,1e30,1e30
            };

            std::array<double,3> maxPos{
                -1e30,-1e30,-1e30
            };

            std::uint32_t base = 0;

            for (const auto* polygon : group.second)
            {
                for (const auto& p : polygon->points)
                {
                    positions.push_back(p.x);
                    positions.push_back(p.y);
                    positions.push_back(p.z);

                    normals.push_back(polygon->normal.x);
                    normals.push_back(polygon->normal.y);
                    normals.push_back(polygon->normal.z);

                    const float xyz[3] = {
                        p.x,p.y,p.z
                    };

                    for (int k = 0; k < 3; ++k)
                    {
                        minPos[k] =
                            std::min(
                                minPos[k],
                                static_cast<double>(xyz[k]));

                        maxPos[k] =
                            std::max(
                                maxPos[k],
                                static_cast<double>(xyz[k]));
                    }
                }

                for (std::size_t i = 1;
                     i + 1 < polygon->points.size();
                     ++i)
                {
                    indices.push_back(base);
                    indices.push_back(
                        base +
                        static_cast<std::uint32_t>(i));
                    indices.push_back(
                        base +
                        static_cast<std::uint32_t>(i + 1));
                }

                base +=
                    static_cast<std::uint32_t>(
                        polygon->points.size());
            }

            Primitive primitive;

            primitive.position =
                builder.AddAccessor(
                    positions,
                    5126,
                    positions.size() / 3,
                    "VEC3",
                    34962,
                    {minPos[0],minPos[1],minPos[2]},
                    {maxPos[0],maxPos[1],maxPos[2]});

            primitive.normal =
                builder.AddAccessor(
                    normals,
                    5126,
                    normals.size() / 3,
                    "VEC3",
                    34962);

            primitive.indices =
                builder.AddAccessor(
                    indices,
                    5125,
                    indices.size(),
                    "SCALAR",
                    34963);

            primitive.material =
                materialIndex.at(group.first);

            mesh.primitives.push_back(primitive);
        }

        meshForNode[nodeIndex] =
            static_cast<int>(meshes.size());

        meshes.push_back(std::move(mesh));
    }

    const fs::path binPath =
        outDir /
        (modelName + "_collision.bin");

    std::ofstream bin(binPath, std::ios::binary);

    if (!bin)
        throw std::runtime_error("cannot create collision BIN");

    bin.write(
        reinterpret_cast<const char*>(
            builder.Bytes().data()),
        static_cast<std::streamsize>(
            builder.Bytes().size()));

    const fs::path csvPath =
        outDir /
        (modelName + "_collision.csv");

    std::ofstream csv(csvPath);

    if (!csv)
        throw std::runtime_error("cannot create collision CSV");

    csv <<
        "polygon_index,node_name,material_id,flags_hex,"
        "point_count,nx,ny,nz\n";

    std::size_t polygonIndex = 0;

    for (const auto& s : scene)
    {
        for (const auto& p : s.polygons)
        {
            csv
                << polygonIndex++ << ','
                << CsvQuote(s.source->name) << ','
                << p.materialId << ','
                << "\"0x"
                << std::hex
                << std::setw(8)
                << std::setfill('0')
                << p.flags
                << std::dec
                << "\","
                << p.points.size() << ','
                << p.normal.x << ','
                << p.normal.y << ','
                << p.normal.z
                << '\n';
        }
    }

    const fs::path gltfPath =
        outDir /
        (modelName + "_collision.gltf");

    std::ofstream out(gltfPath);

    if (!out)
        throw std::runtime_error("cannot create collision glTF");

    out << std::setprecision(9);

    out <<
        "{\n"
        "\"asset\":{\"version\":\"2.0\","
        "\"generator\":\"SOCOM GameZ DI collision exporter\"},\n"
        "\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],\n";

    out <<
        "\"nodes\":["
        "{\"name\":\""
        << JsonEscape(modelName)
        << "_collision\",\"scale\":[0.1,0.1,0.1],"
        "\"children\":[1]}";

    for (std::size_t i = 0; i < scene.size(); ++i)
    {
        const auto& s = scene[i];

        const Node* nparams =
            FindChild(*s.source, "nparams");

        if (!nparams || nparams->dataSize < 0x40)
            throw std::runtime_error("scene node missing nparams");

        const auto* raw =
            archive.PayloadAt(
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

        for (std::size_t k = 0; k < 16; ++k)
        {
            if (k)
                out << ',';

            out << matrix[k];
        }

        out << ']';

        if (meshForNode[i] >= 0)
            out <<
                ",\"mesh\":"
                << meshForNode[i];

        if (!s.children.empty())
        {
            out << ",\"children\":[";

            for (std::size_t c = 0;
                 c < s.children.size();
                 ++c)
            {
                if (c)
                    out << ',';

                out << s.children[c] + 1;
            }

            out << ']';
        }

        out << '}';
    }

    out << "],\n";

    out << "\"meshes\":[";

    for (std::size_t i = 0; i < meshes.size(); ++i)
    {
        if (i)
            out << ',';

        const auto& mesh = meshes[i];

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

            const auto& q =
                mesh.primitives[p];

            out <<
                "{\"attributes\":{\"POSITION\":"
                << q.position
                << ",\"NORMAL\":"
                << q.normal
                << "},\"indices\":"
                << q.indices
                << ",\"material\":"
                << q.material
                << ",\"mode\":4}";
        }

        out << "]}";
    }

    out << "],\n";

    out << "\"materials\":[";

    for (std::size_t i = 0;
         i < materialIds.size();
         ++i)
    {
        if (i)
            out << ',';

        const std::uint32_t id =
            materialIds[i];

        // Deterministic visualization color only. The original material
        // semantics remain the numeric DI ID until materials.rdr is mapped.
        const double r =
            (((id * 53) % 200) + 55) / 255.0;

        const double g =
            (((id * 97) % 200) + 55) / 255.0;

        const double b =
            (((id * 149) % 200) + 55) / 255.0;

        out <<
            "{\"name\":\"DI_material_"
            << id
            << "\",\"pbrMetallicRoughness\":{"
            "\"baseColorFactor\":["
            << r << ',' << g << ',' << b << ",0.55],"
            "\"metallicFactor\":0,\"roughnessFactor\":1},"
            "\"alphaMode\":\"BLEND\","
            "\"doubleSided\":true}";
    }

    out << "],\n";

    out <<
        "\"buffers\":[{\"byteLength\":"
        << builder.Bytes().size()
        << ",\"uri\":\""
        << JsonEscape(
            modelName + "_collision.bin")
        << "\"}],\n";

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
            "{\"buffer\":0,\"byteOffset\":"
            << views[i].offset
            << ",\"byteLength\":"
            << views[i].length
            << ",\"target\":"
            << views[i].target
            << '}';
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
        << "Exported collision model: "
        << modelName << '\n'
        << "  scene nodes: " << scene.size() << '\n'
        << "  polygons: " << polygonCount << '\n'
        << "  source polygon vertices: "
        << sourcePointCount << '\n'
        << "  triangulated faces: "
        << triangleCount << '\n'
        << "  material IDs: "
        << materialIds.size() << '\n'
        << "  unique flag values: "
        << flagCounts.size() << '\n'
        << "  glTF: " << gltfPath << '\n'
        << "  metadata: " << csvPath << '\n';
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
        {
            std::cerr <<
                "Usage:\n"
                "  socom_collision_gltf <*_mdl.zed> "
                "<model_name> <output_dir>\n";

            return 1;
        }

        ExportCollision(
            fs::path(argv[1]),
            argv[2],
            fs::path(argv[3]));

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
