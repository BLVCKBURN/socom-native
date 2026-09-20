#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace socom {

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
inline Vec3 operator/(Vec3 a, float s) { return {a.x/s, a.y/s, a.z/s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
inline Vec3& operator*=(Vec3& a, float s) { a = a * s; return a; }

inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) {
    return {
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}
inline float LengthSq(Vec3 a) { return Dot(a,a); }
inline float Length(Vec3 a) { return std::sqrt(LengthSq(a)); }
inline Vec3 Normalize(Vec3 a) {
    const float len = Length(a);
    return len > 1.0e-8f ? a / len : Vec3{0.0f,1.0f,0.0f};
}

struct Vec2 {
    float x{};
    float y{};
};

struct RenderVertex {
    Vec3 position{};
    Vec3 normal{};
    Vec2 uv{};
    std::array<std::uint8_t,4> color{{255,255,255,255}};
};

struct RenderBatch {
    std::uint32_t firstVertex{};
    std::uint32_t vertexCount{};
    std::uint32_t material{};
};

struct RuntimeTexture {
    std::string name;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t flags{};
    std::vector<std::uint8_t> rgba;

    bool hasTransparency() const {
        return (flags & 1u) != 0u;
    }
};

struct CollisionTriangle {
    Vec3 a{};
    Vec3 b{};
    Vec3 c{};
    Vec3 normal{};
    std::uint32_t material{};
    std::uint32_t flags{};
    Vec3 boundsMin{};
    Vec3 boundsMax{};
};

struct RuntimePack {
    std::uint32_t version{};
    std::vector<RenderVertex> renderVertices;
    std::vector<RenderBatch> renderBatches;
    std::vector<RuntimeTexture> textures;
    std::vector<CollisionTriangle> collisionTriangles;
    Vec3 boundsMin{};
    Vec3 boundsMax{};
    Vec3 spawn{};
};

namespace detail {

template <typename T>
inline T ReadScalar(std::istream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!in) throw std::runtime_error("unexpected end of runtime pack");
    return value;
}

inline Vec3 ReadVec3(std::istream& in) {
    Vec3 value{};
    value.x = ReadScalar<float>(in);
    value.y = ReadScalar<float>(in);
    value.z = ReadScalar<float>(in);
    return value;
}

inline Vec2 ReadVec2(std::istream& in) {
    Vec2 value{};
    value.x = ReadScalar<float>(in);
    value.y = ReadScalar<float>(in);
    return value;
}

inline std::array<std::uint8_t,4> ReadColor(std::istream& in) {
    std::array<std::uint8_t,4> out{};
    in.read(reinterpret_cast<char*>(out.data()), 4);
    if (!in) throw std::runtime_error("unexpected end of runtime pack color");
    return out;
}

inline std::string ReadString(std::istream& in, std::uint32_t length) {
    constexpr std::uint32_t kReasonableString = 1u << 20;
    if (length > kReasonableString)
        throw std::runtime_error("runtime pack string length is unreasonable");
    std::string out(length, '\0');
    if (length) {
        in.read(out.data(), static_cast<std::streamsize>(length));
        if (!in) throw std::runtime_error("unexpected end of runtime pack string");
    }
    return out;
}

inline void FinalizeCollisionTriangle(CollisionTriangle& t) {
    t.normal = Normalize(t.normal);
    t.boundsMin = {
        std::min({t.a.x,t.b.x,t.c.x}),
        std::min({t.a.y,t.b.y,t.c.y}),
        std::min({t.a.z,t.b.z,t.c.z})
    };
    t.boundsMax = {
        std::max({t.a.x,t.b.x,t.c.x}),
        std::max({t.a.y,t.b.y,t.c.y}),
        std::max({t.a.z,t.b.z,t.c.z})
    };
}

} // namespace detail

inline RuntimePack LoadRuntimePack(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open runtime pack: " + path);

    char magic[8]{};
    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, "SOCOMR1\0", 8) != 0)
        throw std::runtime_error("not a SOCOMR1 runtime pack: " + path);

    RuntimePack pack;
    pack.version = detail::ReadScalar<std::uint32_t>(in);
    if (pack.version != 1 && pack.version != 2)
        throw std::runtime_error(
            "unsupported runtime pack version: " + std::to_string(pack.version));

    const auto renderVertexCount = detail::ReadScalar<std::uint32_t>(in);
    const auto collisionTriangleCount = detail::ReadScalar<std::uint32_t>(in);
    (void)detail::ReadScalar<std::uint32_t>(in); // flags/reserved

    std::uint32_t renderBatchCount = 0;
    std::uint32_t textureCount = 0;
    if (pack.version >= 2) {
        renderBatchCount = detail::ReadScalar<std::uint32_t>(in);
        textureCount = detail::ReadScalar<std::uint32_t>(in);
    }

    constexpr std::uint32_t kReasonableMaxVertices = 20'000'000;
    constexpr std::uint32_t kReasonableMaxTriangles = 10'000'000;
    constexpr std::uint32_t kReasonableMaxBatches = 1'000'000;
    constexpr std::uint32_t kReasonableMaxTextures = 100'000;
    constexpr std::uint64_t kReasonableMaxTextureBytes = 512ull * 1024ull * 1024ull;

    if (renderVertexCount > kReasonableMaxVertices ||
        collisionTriangleCount > kReasonableMaxTriangles ||
        renderBatchCount > kReasonableMaxBatches ||
        textureCount > kReasonableMaxTextures)
    {
        throw std::runtime_error("runtime pack counts are unreasonable/corrupt");
    }

    pack.boundsMin = detail::ReadVec3(in);
    pack.boundsMax = detail::ReadVec3(in);
    pack.spawn = detail::ReadVec3(in);

    pack.renderVertices.resize(renderVertexCount);
    for (auto& v : pack.renderVertices) {
        v.position = detail::ReadVec3(in);
        v.normal = detail::ReadVec3(in);
        if (pack.version >= 2)
            v.uv = detail::ReadVec2(in);
        v.color = detail::ReadColor(in);
    }

    if (pack.version >= 2) {
        pack.renderBatches.resize(renderBatchCount);
        for (auto& batch : pack.renderBatches) {
            batch.firstVertex = detail::ReadScalar<std::uint32_t>(in);
            batch.vertexCount = detail::ReadScalar<std::uint32_t>(in);
            batch.material = detail::ReadScalar<std::uint32_t>(in);

            const std::uint64_t end =
                static_cast<std::uint64_t>(batch.firstVertex) +
                static_cast<std::uint64_t>(batch.vertexCount);
            if (end > pack.renderVertices.size())
                throw std::runtime_error("render batch points outside vertex array");
            if ((batch.vertexCount % 3u) != 0u)
                throw std::runtime_error("render batch is not triangle-aligned");
        }

        pack.textures.resize(textureCount);
        std::uint64_t totalTextureBytes = 0;
        for (auto& texture : pack.textures) {
            texture.width = detail::ReadScalar<std::uint32_t>(in);
            texture.height = detail::ReadScalar<std::uint32_t>(in);
            texture.flags = detail::ReadScalar<std::uint32_t>(in);
            const auto nameLength = detail::ReadScalar<std::uint32_t>(in);
            const auto pixelBytes = detail::ReadScalar<std::uint32_t>(in);
            texture.name = detail::ReadString(in, nameLength);

            const std::uint64_t expected =
                static_cast<std::uint64_t>(texture.width) *
                static_cast<std::uint64_t>(texture.height) * 4ull;
            if (!texture.width || !texture.height ||
                expected != pixelBytes)
            {
                throw std::runtime_error(
                    "runtime texture dimensions/byte count mismatch: " +
                    texture.name);
            }

            totalTextureBytes += pixelBytes;
            if (totalTextureBytes > kReasonableMaxTextureBytes)
                throw std::runtime_error("runtime pack texture data is unreasonable");

            texture.rgba.resize(pixelBytes);
            in.read(
                reinterpret_cast<char*>(texture.rgba.data()),
                static_cast<std::streamsize>(texture.rgba.size()));
            if (!in)
                throw std::runtime_error(
                    "unexpected end of runtime texture data: " +
                    texture.name);
        }

        for (const auto& batch : pack.renderBatches) {
            if (batch.material >= pack.textures.size())
                throw std::runtime_error(
                    "render batch references missing material/texture");
        }
    } else if (!pack.renderVertices.empty()) {
        // Backward-compatible v1 packs are untextured and represented as one batch.
        pack.renderBatches.push_back({
            0u,
            static_cast<std::uint32_t>(pack.renderVertices.size()),
            0u
        });
    }

    pack.collisionTriangles.resize(collisionTriangleCount);
    for (auto& t : pack.collisionTriangles) {
        t.a = detail::ReadVec3(in);
        t.b = detail::ReadVec3(in);
        t.c = detail::ReadVec3(in);
        t.normal = detail::ReadVec3(in);
        t.material = detail::ReadScalar<std::uint32_t>(in);
        t.flags = detail::ReadScalar<std::uint32_t>(in);
        detail::FinalizeCollisionTriangle(t);
    }

    const auto current = in.tellg();
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (current != end)
        throw std::runtime_error("runtime pack has trailing bytes or a layout mismatch");

    return pack;
}

inline bool AabbOverlapsSphere(const CollisionTriangle& t, Vec3 center, float radius) {
    float d2 = 0.0f;
    auto axis = [&](float v, float lo, float hi) {
        if (v < lo) { const float d = lo-v; d2 += d*d; }
        else if (v > hi) { const float d = v-hi; d2 += d*d; }
    };
    axis(center.x, t.boundsMin.x, t.boundsMax.x);
    axis(center.y, t.boundsMin.y, t.boundsMax.y);
    axis(center.z, t.boundsMin.z, t.boundsMax.z);
    return d2 <= radius*radius;
}

// Closest point on triangle from Real-Time Collision Detection (Ericson),
// expressed here independently for the runtime prototype.
inline Vec3 ClosestPointOnTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 ab = b-a;
    const Vec3 ac = c-a;
    const Vec3 ap = p-a;
    const float d1 = Dot(ab,ap);
    const float d2 = Dot(ac,ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    const Vec3 bp = p-b;
    const float d3 = Dot(ab,bp);
    const float d4 = Dot(ac,bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    const float vc = d1*d4 - d3*d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1/(d1-d3);
        return a + ab*v;
    }

    const Vec3 cp = p-c;
    const float d5 = Dot(ab,cp);
    const float d6 = Dot(ac,cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    const float vb = d5*d2 - d1*d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2/(d2-d6);
        return a + ac*w;
    }

    const float va = d3*d6 - d5*d4;
    if (va <= 0.0f && (d4-d3) >= 0.0f && (d5-d6) >= 0.0f) {
        const float w = (d4-d3)/((d4-d3)+(d5-d6));
        return b + (c-b)*w;
    }

    const float denom = 1.0f/(va+vb+vc);
    const float v = vb*denom;
    const float w = vc*denom;
    return a + ab*v + ac*w;
}

struct PlayerCollisionResult {
    Vec3 position{}; // feet position
    bool grounded{};
    std::uint32_t contacts{};
};

inline PlayerCollisionResult ResolvePlayerCapsule(
    Vec3 feet,
    float radius,
    float height,
    const std::vector<CollisionTriangle>& triangles,
    int iterations = 4)
{
    PlayerCollisionResult result{feet,false,0};
    const float topOffset = std::max(radius, height-radius);

    for (int iteration=0; iteration<iterations; ++iteration) {
        bool changed = false;

        const float offsets[3] = {
            radius,
            (radius + topOffset) * 0.5f,
            topOffset
        };

        for (int sample=0; sample<3; ++sample) {
            Vec3 center = result.position + Vec3{0.0f, offsets[sample], 0.0f};
            for (const auto& t : triangles) {
                if (!AabbOverlapsSphere(t, center, radius)) continue;
                const Vec3 q = ClosestPointOnTriangle(center,t.a,t.b,t.c);
                Vec3 delta = center-q;
                float d2 = LengthSq(delta);
                if (d2 >= radius*radius) continue;

                Vec3 pushDir{};
                float dist = 0.0f;
                if (d2 > 1.0e-10f) {
                    dist = std::sqrt(d2);
                    pushDir = delta/dist;
                } else {
                    // If the sphere center lies exactly on the triangle, use the
                    // triangle normal but orient it toward the player.
                    pushDir = t.normal;
                    if (Dot(pushDir, center-t.a) < 0.0f) pushDir *= -1.0f;
                }

                const float penetration = radius-dist;
                if (penetration <= 0.0f) continue;
                const Vec3 correction = pushDir*(penetration + 0.0005f);
                result.position += correction;
                center += correction;
                ++result.contacts;
                changed = true;

                if (pushDir.y > 0.55f && correction.y >= 0.0f)
                    result.grounded = true;
            }
        }

        if (!changed) break;
    }
    return result;
}

} // namespace socom
