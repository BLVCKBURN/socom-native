#pragma once

#include "runtime_data.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace socom {

struct Quat {
    float x{}, y{}, z{}, w{1.0f};
};

struct Mat4f {
    std::array<float,16> v{{
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    }};
};

inline Mat4f MatMul(const Mat4f& a, const Mat4f& b) {
    Mat4f o{};
    for (int c=0;c<4;++c) {
        for (int r=0;r<4;++r) {
            float s=0.0f;
            for (int k=0;k<4;++k)
                s += a.v[k*4+r] * b.v[c*4+k];
            o.v[c*4+r]=s;
        }
    }
    return o;
}

inline Quat NormalizeQuat(Quat q) {
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if (n <= 1.0e-8f) return {0,0,0,1};
    const float s=1.0f/n;
    return {q.x*s,q.y*s,q.z*s,q.w*s};
}

inline Quat Nlerp(Quat a, Quat b, float t) {
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if (d < 0.0f) { b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w; }
    return NormalizeQuat({
        a.x+(b.x-a.x)*t,
        a.y+(b.y-a.y)*t,
        a.z+(b.z-a.z)*t,
        a.w+(b.w-a.w)*t
    });
}

inline Mat4f LocalMatrix(Vec3 t, Quat q0) {
    const Quat q=NormalizeQuat(q0);
    const float x=q.x,y=q.y,z=q.z,w=q.w;
    const float xx=x*x,yy=y*y,zz=z*z,xy=x*y,xz=x*z,yz=y*z,wx=w*x,wy=w*y,wz=w*z;
    Mat4f m;
    m.v={
        1-2*(yy+zz), 2*(xy+wz),   2*(xz-wy),   0,
        2*(xy-wz),   1-2*(xx+zz), 2*(yz+wx),   0,
        2*(xz+wy),   2*(yz-wx),   1-2*(xx+yy), 0,
        t.x,          t.y,          t.z,          1
    };
    return m;
}

inline Vec3 TransformPoint(const Mat4f& m, Vec3 p) {
    return {
        m.v[0]*p.x+m.v[4]*p.y+m.v[8]*p.z+m.v[12],
        m.v[1]*p.x+m.v[5]*p.y+m.v[9]*p.z+m.v[13],
        m.v[2]*p.x+m.v[6]*p.y+m.v[10]*p.z+m.v[14]
    };
}

inline Vec3 TransformVector(const Mat4f& m, Vec3 p) {
    return {
        m.v[0]*p.x+m.v[4]*p.y+m.v[8]*p.z,
        m.v[1]*p.x+m.v[5]*p.y+m.v[9]*p.z,
        m.v[2]*p.x+m.v[6]*p.y+m.v[10]*p.z
    };
}

struct PlayerVertex {
    Vec3 position{};
    Vec3 normal{};
    Vec2 uv{};
    std::array<std::uint8_t,4> joints{{0,0,0,0}};
    std::array<float,4> weights{{1,0,0,0}};
};

struct PlayerBatch {
    std::uint32_t firstVertex{};
    std::uint32_t vertexCount{};
    std::uint32_t material{};
};

struct PlayerTexture {
    std::string name;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t flags{};
    std::vector<std::uint8_t> rgba;
    bool hasTransparency() const { return (flags & 1u) != 0u; }
};

struct PlayerJoint {
    std::string name;
    std::int32_t parent{-1};
    Vec3 bindTranslation{};
    Quat bindRotation{};
    Mat4f inverseBind{};
};

struct PlayerPose {
    Vec3 translation{};
    Quat rotation{};
};

struct PlayerClip {
    std::string name;
    float duration{};
    float sampleRate{30.0f};
    std::uint32_t sampleCount{};
    std::vector<PlayerPose> poses; // sample-major, joint-major
};

struct PlayerPack {
    std::uint32_t version{};
    std::vector<PlayerVertex> vertices;
    std::vector<PlayerBatch> batches;
    std::vector<PlayerTexture> textures;
    std::vector<PlayerJoint> joints;
    std::vector<PlayerClip> clips;
};

namespace player_detail {
template<class T> inline T Read(std::istream& in) {
    T v{}; in.read(reinterpret_cast<char*>(&v),sizeof(v));
    if(!in) throw std::runtime_error("unexpected end of player pack");
    return v;
}
inline Vec3 ReadVec3(std::istream& in){return{Read<float>(in),Read<float>(in),Read<float>(in)};}
inline Quat ReadQuat(std::istream& in){return{Read<float>(in),Read<float>(in),Read<float>(in),Read<float>(in)};}
inline std::string ReadString(std::istream& in){
    const auto n=Read<std::uint32_t>(in);
    if(n>(1u<<20)) throw std::runtime_error("player pack string too large");
    std::string s(n,'\0'); if(n){in.read(s.data(),n);if(!in)throw std::runtime_error("unexpected end of player pack string");} return s;
}
}

inline PlayerPack LoadPlayerPack(const std::string& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("cannot open player pack: "+path);
    char magic[8]{}; in.read(magic,8);
    if(!in||std::memcmp(magic,"SOCOMP1\0",8)!=0) throw std::runtime_error("not a SOCOMP1 player pack: "+path);
    PlayerPack p; p.version=player_detail::Read<std::uint32_t>(in);
    if(p.version!=1) throw std::runtime_error("unsupported player pack version");
    const auto vc=player_detail::Read<std::uint32_t>(in);
    const auto bc=player_detail::Read<std::uint32_t>(in);
    const auto tc=player_detail::Read<std::uint32_t>(in);
    const auto jc=player_detail::Read<std::uint32_t>(in);
    const auto cc=player_detail::Read<std::uint32_t>(in);
    (void)player_detail::Read<std::uint32_t>(in);
    if(vc>2'000'000||bc>200'000||tc>10'000||jc>512||cc>1024) throw std::runtime_error("unreasonable player pack counts");
    p.vertices.resize(vc);
    for(auto&v:p.vertices){
        v.position=player_detail::ReadVec3(in); v.normal=player_detail::ReadVec3(in);
        v.uv={player_detail::Read<float>(in),player_detail::Read<float>(in)};
        in.read(reinterpret_cast<char*>(v.joints.data()),4); if(!in)throw std::runtime_error("player vertex joint read failed");
        for(float&w:v.weights)w=player_detail::Read<float>(in);
    }
    p.batches.resize(bc);
    for(auto&b:p.batches){b.firstVertex=player_detail::Read<std::uint32_t>(in);b.vertexCount=player_detail::Read<std::uint32_t>(in);b.material=player_detail::Read<std::uint32_t>(in);if(static_cast<std::uint64_t>(b.firstVertex)+b.vertexCount>p.vertices.size())throw std::runtime_error("player batch outside vertex array");}
    p.textures.resize(tc);
    std::uint64_t textureBytes=0;
    for(auto&t:p.textures){t.width=player_detail::Read<std::uint32_t>(in);t.height=player_detail::Read<std::uint32_t>(in);t.flags=player_detail::Read<std::uint32_t>(in);t.name=player_detail::ReadString(in);const auto n=player_detail::Read<std::uint32_t>(in);const std::uint64_t expected=static_cast<std::uint64_t>(t.width)*t.height*4ull;if(!t.width||!t.height||expected!=n)throw std::runtime_error("player texture size mismatch: "+t.name);textureBytes+=n;if(textureBytes>256ull*1024ull*1024ull)throw std::runtime_error("player texture bytes unreasonable");t.rgba.resize(n);if(n){in.read(reinterpret_cast<char*>(t.rgba.data()),n);if(!in)throw std::runtime_error("player texture read failed");}}
    p.joints.resize(jc);
    for(auto&j:p.joints){j.name=player_detail::ReadString(in);j.parent=player_detail::Read<std::int32_t>(in);j.bindTranslation=player_detail::ReadVec3(in);j.bindRotation=player_detail::ReadQuat(in);for(float&x:j.inverseBind.v)x=player_detail::Read<float>(in);if(j.parent>=static_cast<std::int32_t>(jc))throw std::runtime_error("player joint parent out of range");}
    p.clips.resize(cc);
    for(auto&c:p.clips){c.name=player_detail::ReadString(in);c.duration=player_detail::Read<float>(in);c.sampleRate=player_detail::Read<float>(in);c.sampleCount=player_detail::Read<std::uint32_t>(in);if(!c.sampleCount||c.sampleCount>10000)throw std::runtime_error("player clip sample count invalid");c.poses.resize(static_cast<std::size_t>(c.sampleCount)*jc);for(auto&pose:c.poses){pose.translation=player_detail::ReadVec3(in);pose.rotation=player_detail::ReadQuat(in);}}
    const auto here=in.tellg();in.seekg(0,std::ios::end);if(here!=in.tellg())throw std::runtime_error("player pack has trailing bytes/layout mismatch");
    for(const auto&b:p.batches)if(b.material>=p.textures.size())throw std::runtime_error("player batch references missing texture");
    for(const auto&v:p.vertices)for(auto j:v.joints)if(j>=p.joints.size())throw std::runtime_error("player vertex references missing joint");
    return p;
}

inline int FindPlayerClip(const PlayerPack& p,const std::string& name) {
    for(std::size_t i=0;i<p.clips.size();++i) if(p.clips[i].name==name) return static_cast<int>(i);
    return -1;
}

inline void EvaluatePlayerSkin(
    const PlayerPack& p,
    int clipIndex,
    float timeSeconds,
    std::vector<Mat4f>& skinMatrices)
{
    if(p.joints.empty()) { skinMatrices.clear(); return; }
    if(clipIndex<0||static_cast<std::size_t>(clipIndex)>=p.clips.size()) throw std::runtime_error("player clip index out of range");
    const auto&c=p.clips[static_cast<std::size_t>(clipIndex)];
    const float duration=std::max(c.duration,1.0f/std::max(c.sampleRate,1.0f));
    float t=std::fmod(std::max(timeSeconds,0.0f),duration); if(t<0)t+=duration;
    float frame=t*c.sampleRate;
    std::uint32_t f0=static_cast<std::uint32_t>(std::floor(frame));
    if(f0>=c.sampleCount) f0=c.sampleCount-1;
    std::uint32_t f1=(f0+1<c.sampleCount)?f0+1:0;
    float alpha=frame-static_cast<float>(f0); if(f0==c.sampleCount-1) alpha=0.0f;
    const std::size_t jc=p.joints.size();
    std::vector<Mat4f> local(jc),global(jc); std::vector<std::uint8_t> done(jc,0);
    for(std::size_t j=0;j<jc;++j){
        const auto&a=c.poses[static_cast<std::size_t>(f0)*jc+j];const auto&b=c.poses[static_cast<std::size_t>(f1)*jc+j];
        Vec3 tr={a.translation.x+(b.translation.x-a.translation.x)*alpha,a.translation.y+(b.translation.y-a.translation.y)*alpha,a.translation.z+(b.translation.z-a.translation.z)*alpha};
        local[j]=LocalMatrix(tr,Nlerp(a.rotation,b.rotation,alpha));
    }
    std::function<void(std::size_t)> calc=[&](std::size_t j){if(done[j])return;const int parent=p.joints[j].parent;if(parent>=0){calc(static_cast<std::size_t>(parent));global[j]=MatMul(global[static_cast<std::size_t>(parent)],local[j]);}else global[j]=local[j];done[j]=1;};
    for(std::size_t j=0;j<jc;++j)calc(j);
    skinMatrices.resize(jc);for(std::size_t j=0;j<jc;++j)skinMatrices[j]=MatMul(global[j],p.joints[j].inverseBind);
}

} // namespace socom
