// SOCOM 1 GameZ skinned character -> glTF 2.0 exporter
// C++17, no third-party dependencies.
//
// Inputs recovered/validated against SCUS_971.34:
//   * character models.zar  (MESH_* VIF skin packets)
//   * matching clib_mdl.zed (CNode skeleton + exact matrix_id + bind matrices)
//   * decoded PNG directory from clib_txr.zed/clib_pal.zed
//   * optional motion.zar + clip name for skeletal animation
//
// Character packet facts used here:
//   0x1006 : texture name (.pic)
//   0x3009 : matrix selection/state
//   0x300A : bone-local position/normal/weight contributions
//   0x300B : final vertex topology + UVs
//
// Packed contribution record (16 bytes):
//   int16 localPosX, localPosY, localPosZ, targetSlot;
//   int16 localNrmX, localNrmY, localNrmZ, q15Weight;
//
// targetSlot is two VU qwords per final vertex => vertex = targetSlot / 2.
// position scale is 10/32768 game units. Output glTF uses 0.1 m/game-unit.
// normals use signed16 / 32767. UVs use signed16 / 4096.
//
// macOS / Apple Silicon:
//   clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic socom_character_gltf.cpp -o socom_character_gltf
//
// Windows (Developer Command Prompt):
//   cl /std:c++17 /EHsc /O2 socom_character_gltf.cpp
//
// Usage:
//   socom_character_gltf <character_models.zar> <clib_mdl.zed> <png_dir>
//                        <MESH_model_name> <out_dir>
//                        [motion.zar clip_name]
//
// Example:
//   ./socom_character_gltf models.zar clib_mdl.zed textures \
//       MESH_seal_A_des out motion.zar seal_stand

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
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

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
static_assert(sizeof(ZarHeader)==0x64);
static_assert(sizeof(ZarNodeDisk)==0x10);

template<class T>
static T ReadLE(const std::uint8_t* p) {
    T v{};
    std::memcpy(&v,p,sizeof(v));
    return v;
}

static std::size_t AlignUp(std::size_t v,std::size_t a) {
    if(!a) throw std::runtime_error("zero alignment");
    const std::size_t r=v%a;
    return r ? v+(a-r) : v;
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
        std::ifstream in(path,std::ios::binary);
        if(!in) throw std::runtime_error("cannot open "+path.string());
        in.seekg(0,std::ios::end);
        const auto n=in.tellg();
        in.seekg(0,std::ios::beg);
        if(n<static_cast<std::streamoff>(sizeof(ZarHeader)))
            throw std::runtime_error("file too small: "+path.string());
        bytes_.resize(static_cast<std::size_t>(n));
        in.read(reinterpret_cast<char*>(bytes_.data()),
                static_cast<std::streamsize>(bytes_.size()));
        if(!in) throw std::runtime_error("read failed: "+path.string());

        std::memcpy(&header_,bytes_.data(),sizeof(header_));
        if(header_.version!=0x00020002)
            throw std::runtime_error("unsupported CZAR/ZAR version");

        stringsOffset_=sizeof(ZarHeader);
        nodesOffset_=stringsOffset_+header_.stringBlockSize;
        const std::uint64_t nodeBytes=
            static_cast<std::uint64_t>(header_.nodeCount)*sizeof(ZarNodeDisk);
        if(nodesOffset_+nodeBytes>bytes_.size())
            throw std::runtime_error("node table outside file");
        payloadOffset_=AlignUp(
            static_cast<std::size_t>(nodesOffset_+nodeBytes),header_.alignment);
        if(payloadOffset_+header_.payloadSize!=bytes_.size())
            throw std::runtime_error("payload does not end at EOF");

        disk_.resize(header_.nodeCount);
        std::memcpy(disk_.data(),bytes_.data()+nodesOffset_,
                    static_cast<std::size_t>(nodeBytes));
        std::size_t cursor=0;
        root_=Parse(cursor);
        if(cursor!=disk_.size())
            throw std::runtime_error("ZAR tree parse mismatch");
    }

    const Node& Root() const { return root_; }
    std::uint32_t PayloadSize() const { return header_.payloadSize; }

    const std::uint8_t* PayloadAt(std::uint32_t off,std::size_t size=1) const {
        if(static_cast<std::uint64_t>(off)+size>header_.payloadSize)
            throw std::runtime_error("payload access out of bounds");
        return bytes_.data()+payloadOffset_+off;
    }

private:
    std::string ResolveName(std::uint32_t addr) const {
        if(!addr) return "<root>";
        if(addr<header_.preferredBase)
            throw std::runtime_error("invalid serialized name pointer");
        const std::uint64_t rel=
            static_cast<std::uint64_t>(addr)-header_.preferredBase;
        if(rel>=header_.stringBlockSize)
            throw std::runtime_error("name outside string block");
        const std::size_t s=stringsOffset_+static_cast<std::size_t>(rel);
        const std::size_t lim=stringsOffset_+header_.stringBlockSize;
        std::size_t e=s;
        while(e<lim && bytes_[e]) ++e;
        if(e==lim) throw std::runtime_error("unterminated node name");
        return std::string(reinterpret_cast<const char*>(bytes_.data()+s),e-s);
    }

    Node Parse(std::size_t& cursor) {
        if(cursor>=disk_.size()) throw std::runtime_error("tree overrun");
        const auto d=disk_[cursor++];
        Node n;
        n.name=ResolveName(d.nameAddress);
        n.dataOffset=d.dataOffset;
        n.dataSize=d.dataSize;
        n.children.reserve(d.childCount);
        for(std::uint32_t i=0;i<d.childCount;++i)
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

static const Node* Child(const Node& n,const std::string& name) {
    for(const auto& c:n.children)
        if(c.name==name) return &c;
    return nullptr;
}

static const Node* FindTop(const Node& root,const std::string& name) {
    for(const auto& c:root.children)
        if(c.name==name) return &c;
    return nullptr;
}

struct Mat4 {
    // Row-major GameZ matrix, row-vector convention.
    double m[4][4]{};
};

static Mat4 MulRow(const Mat4& a,const Mat4& b) {
    Mat4 r{};
    for(int i=0;i<4;++i)
        for(int j=0;j<4;++j)
            for(int k=0;k<4;++k)
                r.m[i][j]+=a.m[i][k]*b.m[k][j];
    return r;
}

static std::array<double,3> TransformPointRow(
    const std::array<double,3>& p,const Mat4& m)
{
    return {
        p[0]*m.m[0][0]+p[1]*m.m[1][0]+p[2]*m.m[2][0]+m.m[3][0],
        p[0]*m.m[0][1]+p[1]*m.m[1][1]+p[2]*m.m[2][1]+m.m[3][1],
        p[0]*m.m[0][2]+p[1]*m.m[1][2]+p[2]*m.m[2][2]+m.m[3][2]
    };
}

static std::array<double,3> TransformVectorRow(
    const std::array<double,3>& p,const Mat4& m)
{
    return {
        p[0]*m.m[0][0]+p[1]*m.m[1][0]+p[2]*m.m[2][0],
        p[0]*m.m[0][1]+p[1]*m.m[1][1]+p[2]*m.m[2][1],
        p[0]*m.m[0][2]+p[1]*m.m[1][2]+p[2]*m.m[2][2]
    };
}

static std::array<double,3> Normalize3(std::array<double,3> v) {
    const double l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(l>1e-20) { v[0]/=l;v[1]/=l;v[2]/=l; }
    return v;
}

static std::array<double,4> QuatFromColumnRotation(const Mat4& row) {
    // Standard column-vector rotation is transpose(GameZ row-vector 3x3).
    const double m00=row.m[0][0], m01=row.m[1][0], m02=row.m[2][0];
    const double m10=row.m[0][1], m11=row.m[1][1], m12=row.m[2][1];
    const double m20=row.m[0][2], m21=row.m[1][2], m22=row.m[2][2];
    double x{},y{},z{},w{};
    const double tr=m00+m11+m22;
    if(tr>0.0) {
        const double s=std::sqrt(tr+1.0)*2.0;
        w=0.25*s; x=(m21-m12)/s; y=(m02-m20)/s; z=(m10-m01)/s;
    } else if(m00>m11 && m00>m22) {
        const double s=std::sqrt(1.0+m00-m11-m22)*2.0;
        w=(m21-m12)/s; x=0.25*s; y=(m01+m10)/s; z=(m02+m20)/s;
    } else if(m11>m22) {
        const double s=std::sqrt(1.0+m11-m00-m22)*2.0;
        w=(m02-m20)/s; x=(m01+m10)/s; y=0.25*s; z=(m12+m21)/s;
    } else {
        const double s=std::sqrt(1.0+m22-m00-m11)*2.0;
        w=(m10-m01)/s; x=(m02+m20)/s; y=(m12+m21)/s; z=0.25*s;
    }
    const double l=std::sqrt(x*x+y*y+z*z+w*w);
    if(l>0.0) {x/=l;y/=l;z/=l;w/=l;}
    if(w<0.0) {x=-x;y=-y;z=-z;w=-w;}
    return {x,y,z,w};
}

struct SkeletonJoint {
    std::string name;
    int matrixId{-1};
    int parent{-1};
    Mat4 local{};
    Mat4 global{};
};

static Mat4 ReadNParamsMatrix(const ZarArchive& mdl,const Node& node) {
    const Node* np=Child(node,"nparams");
    if(!np || np->dataSize<64)
        throw std::runtime_error("node missing 0x40-byte nparams matrix: "+node.name);
    const auto* p=mdl.PayloadAt(np->dataOffset,np->dataSize);
    Mat4 m{};
    for(int r=0;r<4;++r)
        for(int c=0;c<4;++c)
            m.m[r][c]=ReadLE<float>(p+(r*4+c)*4);
    return m;
}

static int ReadMatrixId(const ZarArchive& mdl,const Node& node) {
    const Node* visuals=Child(node,"visuals");
    if(!visuals) throw std::runtime_error("node missing visuals: "+node.name);
    const Node* vis=Child(*visuals,"vis");
    if(!vis) throw std::runtime_error("node missing vis: "+node.name);
    const Node* mid=Child(*vis,"matrix_id");
    if(!mid || mid->dataSize!=4)
        throw std::runtime_error("node missing matrix_id: "+node.name);
    return static_cast<int>(ReadLE<std::uint32_t>(mdl.PayloadAt(mid->dataOffset,4)));
}

static void GatherSkeletonRecursive(
    const ZarArchive& mdl,const Node& node,int parentId,
    std::map<int,SkeletonJoint>& joints)
{
    const int id=ReadMatrixId(mdl,node);
    if(id<0 || id>255) throw std::runtime_error("unreasonable matrix_id");
    SkeletonJoint j;
    j.name=node.name;
    j.matrixId=id;
    j.parent=parentId;
    j.local=ReadNParamsMatrix(mdl,node);
    if(parentId<0) j.global=j.local;
    else {
        auto it=joints.find(parentId);
        if(it==joints.end()) throw std::runtime_error("parent joint not yet inserted");
        j.global=MulRow(j.local,it->second.global);
    }
    joints[id]=j;

    const Node* children=Child(node,"children");
    if(children)
        for(const auto& c:children->children)
            GatherSkeletonRecursive(mdl,c,id,joints);
}

static std::map<int,SkeletonJoint> LoadSkeleton(
    const ZarArchive& mdl,const std::string& modelName)
{
    const Node* models=FindTop(mdl.Root(),"models");
    if(!models) throw std::runtime_error("clib_mdl.zed missing models collection");
    const Node* model=FindTop(*models,modelName);
    if(!model) throw std::runtime_error("model not found in clib_mdl.zed: "+modelName);
    const Node* c=Child(*model,"children");
    if(!c) throw std::runtime_error("mdl model missing children: "+modelName);
    const Node* skel=FindTop(*c,"skel_root");
    if(!skel) throw std::runtime_error("mdl model missing skel_root: "+modelName);

    std::map<int,SkeletonJoint> joints;
    GatherSkeletonRecursive(mdl,*skel,-1,joints);
    if(joints.size()!=25)
        throw std::runtime_error("expected 25 humanoid skeleton joints");
    for(int i=0;i<25;++i)
        if(!joints.count(i)) throw std::runtime_error("skeleton matrix IDs are not 0..24");
    return joints;
}

struct Contribution {
    int matrixId{};
    int vertex{};
    std::array<double,3> localPosition{};
    std::array<double,3> localNormal{};
    int rawWeight{};
};

struct Triangle { std::uint16_t a{},b{},c{}; };

struct CharacterBatch {
    std::string texturePic;
    int vertexCount{};
    std::vector<std::array<std::int16_t,2>> rawUV;
    std::vector<Triangle> triangles;
    std::vector<Contribution> contributions;
};

static std::string ReadCStringInBlob(
    const std::uint8_t* blob,std::size_t blobSize,std::uint32_t off)
{
    if(off>=blobSize) throw std::runtime_error("string offset outside model blob");
    std::size_t e=off;
    while(e<blobSize && blob[e]) ++e;
    if(e==blobSize) throw std::runtime_error("unterminated string in model blob");
    return std::string(reinterpret_cast<const char*>(blob+off),e-off);
}

static std::vector<CharacterBatch> ParseCharacterModel(
    const ZarArchive& geo,const Node& model)
{
    const std::uint8_t* blob=geo.PayloadAt(model.dataOffset,model.dataSize);
    const std::size_t blobSize=model.dataSize;

    const Node* startNode=nullptr;
    const Node* refCountNode=Child(model,"ref_count");
    for(const auto& c:model.children)
        if(c.name.size()>=6 && c.name.substr(c.name.size()-6)=="_START")
            startNode=&c;
    if(!startNode || !refCountNode)
        throw std::runtime_error("character model missing START/ref_count");

    const std::uint32_t tableStart=
        ReadLE<std::uint32_t>(geo.PayloadAt(startNode->dataOffset,4));
    const std::uint32_t refCount=
        ReadLE<std::uint32_t>(geo.PayloadAt(refCountNode->dataOffset,4));

    if(static_cast<std::uint64_t>(tableStart)+16ull+16ull*refCount>blobSize)
        throw std::runtime_error("character reference table outside model blob");

    const std::uint32_t listHeader=ReadLE<std::uint32_t>(blob+tableStart);
    if((listHeader>>16)!=0x7000 || (listHeader&0xFFFF)!=refCount)
        throw std::runtime_error("unexpected character reference-list header");

    std::string currentTexture;
    std::vector<Contribution> pending;
    std::vector<CharacterBatch> batches;

    for(std::uint32_t i=0;i<refCount;++i) {
        const std::uint8_t* r=blob+tableStart+16+static_cast<std::size_t>(i)*16;
        const std::uint32_t w0=ReadLE<std::uint32_t>(r+0);
        const std::uint32_t w1=ReadLE<std::uint32_t>(r+4);
        const std::uint32_t w2=ReadLE<std::uint32_t>(r+8);
        const std::uint32_t w3=ReadLE<std::uint32_t>(r+12);
        const std::uint32_t type=w0>>16;
        const std::uint32_t qwc=w0&0xFFFF;

        if(type==0x1006) {
            currentTexture=ReadCStringInBlob(blob,blobSize,w1);
            continue;
        }

        if(type==0x3009) {
            // Matrix/VIF setup reference. Matrix ID is w1. The following
            // 0x300A record carries the same authoritative ID.
            continue;
        }

        if(type==0x300A) {
            if(static_cast<std::uint64_t>(w1)+static_cast<std::uint64_t>(qwc)*16>blobSize)
                throw std::runtime_error("0x300A packet outside model blob");
            const std::uint8_t* p=blob+w1;
            const std::uint32_t c0=ReadLE<std::uint32_t>(p+0);
            const std::uint32_t c1=ReadLE<std::uint32_t>(p+4);
            if(((c0>>24)&0x7F)!=0x01 || ((c1>>24)&0x7F)!=0x6D)
                throw std::runtime_error("unexpected 0x300A VIF header");
            const std::uint32_t unpackCount=(c1>>16)&0xFF;
            if(unpackCount!=1+2*w3)
                throw std::runtime_error("0x300A contribution-count invariant failed");

            const auto h0=ReadLE<std::int16_t>(p+8);
            const auto h1=ReadLE<std::int16_t>(p+10);
            const auto h2=ReadLE<std::int16_t>(p+12);
            const auto h3=ReadLE<std::int16_t>(p+14);
            (void)h0;(void)h1;
            if(static_cast<std::uint16_t>(h2)!=w2 || static_cast<std::uint16_t>(h3)!=w3)
                throw std::runtime_error("0x300A packet header mismatch");

            for(std::uint32_t j=0;j<w3;++j) {
                const std::uint8_t* v=p+16+static_cast<std::size_t>(j)*16;
                const std::int16_t px=ReadLE<std::int16_t>(v+0);
                const std::int16_t py=ReadLE<std::int16_t>(v+2);
                const std::int16_t pz=ReadLE<std::int16_t>(v+4);
                const std::int16_t target=ReadLE<std::int16_t>(v+6);
                const std::int16_t nx=ReadLE<std::int16_t>(v+8);
                const std::int16_t ny=ReadLE<std::int16_t>(v+10);
                const std::int16_t nz=ReadLE<std::int16_t>(v+12);
                const std::int16_t weight=ReadLE<std::int16_t>(v+14);
                if(target<0 || (target&1))
                    throw std::runtime_error("character target slot is not nonnegative/even");
                Contribution c;
                c.matrixId=static_cast<int>(w2);
                c.vertex=target/2;
                constexpr double kPos=10.0/32768.0;
                constexpr double kNrm=1.0/32767.0;
                c.localPosition={px*kPos,py*kPos,pz*kPos};
                c.localNormal={nx*kNrm,ny*kNrm,nz*kNrm};
                c.rawWeight=weight;
                pending.push_back(c);
            }
            continue;
        }

        if(type==0x300B) {
            if(currentTexture.empty())
                throw std::runtime_error("0x300B before texture selection");
            if(static_cast<std::uint64_t>(w1)+static_cast<std::uint64_t>(qwc)*16>blobSize)
                throw std::runtime_error("0x300B packet outside model blob");
            const std::uint8_t* p=blob+w1;
            std::size_t pos=0;

            const std::uint32_t st0=ReadLE<std::uint32_t>(p+pos);pos+=4;
            const std::uint32_t un0=ReadLE<std::uint32_t>(p+pos);pos+=4;
            if(((st0>>24)&0x7F)!=0x01 || (st0&0xFFFF)!=0x0102 ||
               ((un0>>24)&0x7F)!=0x6E)
                throw std::runtime_error("unexpected 0x300B triangle VIF layout");
            const int triCount=static_cast<int>((un0>>16)&0xFF);
            std::vector<Triangle> tris;
            tris.reserve(triCount);
            for(int t=0;t<triCount;++t) {
                const std::uint8_t a=p[pos+t*4+0];
                const std::uint8_t b=p[pos+t*4+1];
                const std::uint8_t c=p[pos+t*4+2];
                if((a%3)||(b%3)||(c%3))
                    throw std::runtime_error("character triangle not on 3-qword stride");
                tris.push_back({static_cast<std::uint16_t>(a/3),
                                static_cast<std::uint16_t>(b/3),
                                static_cast<std::uint16_t>(c/3)});
            }
            pos+=static_cast<std::size_t>(triCount)*4;
            pos=AlignUp(pos,4);

            const std::uint32_t st1=ReadLE<std::uint32_t>(p+pos);pos+=4;
            const std::uint32_t un1=ReadLE<std::uint32_t>(p+pos);pos+=4;
            if(((st1>>24)&0x7F)!=0x01 || (st1&0xFFFF)!=0x0101 ||
               ((un1>>24)&0x7F)!=0x6C || ((un1>>16)&0xFF)!=4)
                throw std::runtime_error("unexpected 0x300B control VIF layout");
            if(pos+64>qwc*16) throw std::runtime_error("0x300B control block truncated");
            const std::uint32_t controlVertexCount=ReadLE<std::uint32_t>(p+pos+10*4);
            const std::uint32_t controlTriCount=ReadLE<std::uint32_t>(p+pos+11*4);
            pos+=64;

            const std::uint32_t st2=ReadLE<std::uint32_t>(p+pos);pos+=4;
            const std::uint32_t un2=ReadLE<std::uint32_t>(p+pos);pos+=4;
            if(((st2>>24)&0x7F)!=0x01 || (st2&0xFFFF)!=0x0103 ||
               ((un2>>24)&0x7F)!=0x65)
                throw std::runtime_error("unexpected 0x300B UV VIF layout");
            const int vertexCount=static_cast<int>((un2>>16)&0xFF);
            if(controlVertexCount!=static_cast<std::uint32_t>(vertexCount) ||
               controlTriCount!=static_cast<std::uint32_t>(triCount))
                throw std::runtime_error("0x300B control counts disagree");
            std::vector<std::array<std::int16_t,2>> uv;
            uv.reserve(vertexCount);
            for(int v=0;v<vertexCount;++v) {
                uv.push_back({ReadLE<std::int16_t>(p+pos+v*4+0),
                              ReadLE<std::int16_t>(p+pos+v*4+2)});
            }

            for(const auto& t:tris)
                if(t.a>=vertexCount || t.b>=vertexCount || t.c>=vertexCount)
                    throw std::runtime_error("triangle index outside character batch");

            CharacterBatch batch;
            batch.texturePic=currentTexture;
            batch.vertexCount=vertexCount;
            batch.rawUV=std::move(uv);
            batch.triangles=std::move(tris);
            batch.contributions=std::move(pending);
            pending.clear();
            batches.push_back(std::move(batch));
            continue;
        }
    }

    if(!pending.empty())
        throw std::runtime_error("unconsumed character contributions at end of list");
    if(batches.empty())
        throw std::runtime_error("no character render batches decoded");
    return batches;
}

struct VertexOut {
    std::array<float,3> p{};
    std::array<float,3> n{};
    std::array<float,2> uv{};
    std::array<std::uint8_t,8> joints{};
    std::array<float,8> weights{};
};

static std::vector<VertexOut> ReconstructBatchVertices(
    const CharacterBatch& batch,const std::map<int,SkeletonJoint>& skel)
{
    std::vector<std::vector<const Contribution*>> per(batch.vertexCount);
    for(const auto& c:batch.contributions) {
        if(c.vertex<0 || c.vertex>=batch.vertexCount)
            throw std::runtime_error("contribution targets vertex outside batch");
        if(!skel.count(c.matrixId))
            throw std::runtime_error("contribution references absent skeleton matrix ID");
        per[c.vertex].push_back(&c);
    }

    std::vector<VertexOut> out(batch.vertexCount);
    for(int v=0;v<batch.vertexCount;++v) {
        auto& list=per[v];
        if(list.empty()) throw std::runtime_error("character vertex has no skin contributions");
        if(list.size()>8) throw std::runtime_error("character vertex exceeds 8 influences");

        double sum=0.0;
        for(auto* c:list) sum+=std::max(0,c->rawWeight);
        if(sum<=0.0) sum=static_cast<double>(list.size());

        std::array<double,3> p{0,0,0},n{0,0,0};
        for(std::size_t k=0;k<list.size();++k) {
            const auto* c=list[k];
            const double w=(sum>0.0) ? std::max(0,c->rawWeight)/sum : 1.0/list.size();
            const auto& g=skel.at(c->matrixId).global;
            const auto gp=TransformPointRow(c->localPosition,g);
            const auto gn=TransformVectorRow(c->localNormal,g);
            for(int a=0;a<3;++a) {p[a]+=gp[a]*w;n[a]+=gn[a]*w;}
            out[v].joints[k]=static_cast<std::uint8_t>(c->matrixId);
            out[v].weights[k]=static_cast<float>(w);
        }
        n=Normalize3(n);

        // glTF meters: SOCOM/GameZ character geometry is naturally ~19.4 game
        // units tall. 0.1 m/game-unit reproduces a ~1.94 m character.
        out[v].p={static_cast<float>(p[0]*0.1),
                  static_cast<float>(p[1]*0.1),
                  static_cast<float>(p[2]*0.1)};
        out[v].n={static_cast<float>(n[0]),static_cast<float>(n[1]),static_cast<float>(n[2])};
        out[v].uv={batch.rawUV[v][0]/4096.0f,batch.rawUV[v][1]/4096.0f};
    }
    return out;
}

struct MotionTrack {
    std::string name;
    std::uint32_t flags{};
    std::vector<std::array<float,3>> translations;
    std::vector<std::array<float,4>> rotations;
};
struct MotionClip {
    std::string name;
    float duration{};
    int frameCount{};
    std::vector<MotionTrack> tracks;
};

static MotionClip ParseMotionClip(const ZarArchive& motion,const Node& n) {
    const auto* b=motion.PayloadAt(n.dataOffset,n.dataSize);
    const std::size_t size=n.dataSize;
    if(size<0x20 || ReadLE<std::uint32_t>(b)!=5)
        throw std::runtime_error("unexpected motion clip format");
    MotionClip c;
    c.name=n.name;
    c.duration=ReadLE<float>(b+4);
    c.frameCount=b[8];
    const int boneCount=b[0x0C];
    const std::uint32_t namesOff=ReadLE<std::uint32_t>(b+0x18);
    const std::uint32_t tracksOff=ReadLE<std::uint32_t>(b+0x1C);
    if(namesOff>tracksOff || tracksOff>size)
        throw std::runtime_error("invalid motion string/track offsets");
    std::vector<std::string> names;
    std::size_t pos=namesOff;
    for(int i=0;i<boneCount;++i) {
        std::size_t e=pos;
        while(e<tracksOff && b[e]) ++e;
        if(e==tracksOff) throw std::runtime_error("unterminated motion bone name");
        names.emplace_back(reinterpret_cast<const char*>(b+pos),e-pos);
        pos=AlignUp(e+1,4);
    }
    if(pos!=tracksOff) throw std::runtime_error("motion name block alignment mismatch");
    pos=tracksOff;
    for(int i=0;i<boneCount;++i) {
        MotionTrack t;
        t.name=names[i];
        t.flags=ReadLE<std::uint32_t>(b+pos);pos+=4;
        const int tc=(t.flags&0x20)?1:c.frameCount+1;
        const int rc=(t.flags&0x10)?1:c.frameCount+1;
        for(int k=0;k<tc;++k) {
            t.translations.push_back({ReadLE<float>(b+pos),ReadLE<float>(b+pos+4),ReadLE<float>(b+pos+8)});
            pos+=12;
        }
        for(int k=0;k<rc;++k) {
            std::array<float,4> q{ReadLE<float>(b+pos),ReadLE<float>(b+pos+4),ReadLE<float>(b+pos+8),ReadLE<float>(b+pos+12)};
            pos+=16;
            const double l=std::sqrt(double(q[0])*q[0]+double(q[1])*q[1]+double(q[2])*q[2]+double(q[3])*q[3]);
            if(!(l>0.999 && l<1.001)) throw std::runtime_error("non-unit motion quaternion");
            t.rotations.push_back(q);
        }
        c.tracks.push_back(std::move(t));
    }
    if(pos!=size) throw std::runtime_error("motion parser did not consume clip exactly");
    return c;
}

static MotionClip LoadMotionClip(const ZarArchive& motion,const std::string& name) {
    const Node* n=FindTop(motion.Root(),name);
    if(!n) throw std::runtime_error("motion clip not found: "+name);
    return ParseMotionClip(motion,*n);
}

static std::string JsonEscape(const std::string& s) {
    std::ostringstream o;
    for(unsigned char c:s) {
        switch(c) {
            case '\\': o<<"\\\\";break;
            case '"': o<<"\\\"";break;
            case '\n': o<<"\\n";break;
            case '\r': o<<"\\r";break;
            case '\t': o<<"\\t";break;
            default:
                if(c<0x20) {o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;}
                else o<<char(c);
        }
    }
    return o.str();
}

struct BufferView { std::size_t offset{},length{}; std::optional<int> target; };
struct Accessor {
    int view{};
    int componentType{};
    std::size_t count{};
    std::string type;
    bool normalized{};
    std::vector<double> minv,maxv;
};

class BinaryBuilder {
public:
    template<class T>
    int AddAccessor(const std::vector<T>& values,int componentType,
                    std::size_t count,const std::string& type,
                    std::optional<int> target=std::nullopt,
                    bool normalized=false,
                    std::vector<double> minv={},std::vector<double> maxv={})
    {
        while(data.size()%4) data.push_back(0);
        const std::size_t off=data.size();
        const std::size_t bytes=values.size()*sizeof(T);
        const auto* p=reinterpret_cast<const std::uint8_t*>(values.data());
        data.insert(data.end(),p,p+bytes);
        const int vi=static_cast<int>(views.size());
        views.push_back({off,bytes,target});
        const int ai=static_cast<int>(accessors.size());
        accessors.push_back({vi,componentType,count,type,normalized,std::move(minv),std::move(maxv)});
        return ai;
    }

    std::vector<std::uint8_t> data;
    std::vector<BufferView> views;
    std::vector<Accessor> accessors;
};

struct PrimitiveJson {
    int pos{},nrm{},uv{},joints0{},weights0{},indices{},material{};
    int joints1{-1},weights1{-1};
};
struct MaterialJson { std::string pic,png; int image{},texture{}; };
struct NodeJson {
    std::string name;
    std::array<double,3> t{0,0,0};
    std::array<double,4> q{0,0,0,1};
    std::vector<int> children;
    std::optional<int> mesh;
    std::optional<int> skin;
};
struct AnimSampler { int input{},output{}; };
struct AnimChannel { int sampler{},node{}; std::string path; };

static std::string PicToPng(std::string s) {
    const auto p=s.find_last_of('.');
    if(p!=std::string::npos) s=s.substr(0,p);
    return s+".png";
}

static void WriteGltf(
    const fs::path& geoPath,const fs::path& mdlPath,const fs::path& pngDir,
    const std::string& geoModelName,const fs::path& outRoot,
    const std::optional<fs::path>& motionPath,
    const std::optional<std::string>& clipName)
{
    ZarArchive geo(geoPath);
    ZarArchive mdl(mdlPath);
    const Node* geoModel=FindTop(geo.Root(),geoModelName);
    if(!geoModel) throw std::runtime_error("geometry model not found: "+geoModelName);
    std::string mdlName=geoModelName;
    if(mdlName.rfind("MESH_",0)==0) mdlName=mdlName.substr(5);

    auto skel=LoadSkeleton(mdl,mdlName);
    auto batches=ParseCharacterModel(geo,*geoModel);

    fs::path out=outRoot/mdlName;
    fs::path texOut=out/"textures";
    fs::create_directories(texOut);

    BinaryBuilder bb;
    std::vector<PrimitiveJson> prims;
    std::map<std::string,int> materialByPic;
    std::vector<MaterialJson> materials;

    // Precreate unique materials and copy textures.
    for(const auto& b:batches) {
        if(materialByPic.count(b.texturePic)) continue;
        const std::string png=PicToPng(b.texturePic);
        const fs::path src=pngDir/png;
        if(!fs::exists(src))
            throw std::runtime_error("decoded PNG missing: "+src.string());
        fs::copy_file(src,texOut/png,fs::copy_options::overwrite_existing);
        const int mi=static_cast<int>(materials.size());
        materialByPic[b.texturePic]=mi;
        materials.push_back({b.texturePic,png,mi,mi});
    }

    std::size_t totalV=0,totalT=0;
    for(const auto& b:batches) {
        const auto verts=ReconstructBatchVertices(b,skel);
        totalV+=verts.size(); totalT+=b.triangles.size();
        std::vector<float> pos,nrm,uv,weights0,weights1;
        std::vector<std::uint8_t> joints0,joints1;
        std::vector<std::uint16_t> indices;
        pos.reserve(verts.size()*3);nrm.reserve(verts.size()*3);uv.reserve(verts.size()*2);
        weights0.reserve(verts.size()*4);joints0.reserve(verts.size()*4);
        weights1.reserve(verts.size()*4);joints1.reserve(verts.size()*4);
        std::array<double,3> minp{1e100,1e100,1e100},maxp{-1e100,-1e100,-1e100};
        for(const auto& v:verts) {
            for(int k=0;k<3;++k) {pos.push_back(v.p[k]);nrm.push_back(v.n[k]);minp[k]=std::min(minp[k],double(v.p[k]));maxp[k]=std::max(maxp[k],double(v.p[k]));}
            uv.push_back(v.uv[0]);uv.push_back(v.uv[1]);
            for(int k=0;k<4;++k) {joints0.push_back(v.joints[k]);weights0.push_back(v.weights[k]);}
            for(int k=4;k<8;++k) {joints1.push_back(v.joints[k]);weights1.push_back(v.weights[k]);}
        }
        for(const auto& t:b.triangles) {indices.push_back(t.a);indices.push_back(t.b);indices.push_back(t.c);}

        PrimitiveJson p;
        p.pos=bb.AddAccessor(pos,5126,verts.size(),"VEC3",34962,false,
            {minp[0],minp[1],minp[2]},{maxp[0],maxp[1],maxp[2]});
        p.nrm=bb.AddAccessor(nrm,5126,verts.size(),"VEC3",34962);
        p.uv=bb.AddAccessor(uv,5126,verts.size(),"VEC2",34962);
        p.joints0=bb.AddAccessor(joints0,5121,verts.size(),"VEC4",34962);
        p.weights0=bb.AddAccessor(weights0,5126,verts.size(),"VEC4",34962);
        bool hasSecond=false;
        for(float x:weights1) if(x!=0.0f) {hasSecond=true;break;}
        if(hasSecond) {
            p.joints1=bb.AddAccessor(joints1,5121,verts.size(),"VEC4",34962);
            p.weights1=bb.AddAccessor(weights1,5126,verts.size(),"VEC4",34962);
        }
        p.indices=bb.AddAccessor(indices,5123,indices.size(),"SCALAR",34963);
        p.material=materialByPic.at(b.texturePic);
        prims.push_back(p);
    }

    // Build glTF skeleton nodes in matrix-ID order (0..24).
    std::vector<NodeJson> nodes;
    nodes.reserve(26);
    for(int id=0;id<25;++id) {
        const auto& j=skel.at(id);
        NodeJson n;
        n.name=j.name;
        n.t={j.local.m[3][0]*0.1,j.local.m[3][1]*0.1,j.local.m[3][2]*0.1};
        n.q=QuatFromColumnRotation(j.local);
        nodes.push_back(n);
    }
    for(int id=0;id<25;++id) {
        const int p=skel.at(id).parent;
        if(p>=0) nodes[p].children.push_back(id);
    }

    // Inverse bind matrices: standard column matrix = transpose(GameZ row matrix),
    // with translation converted to meters. Store MAT4 column-major as glTF expects.
    std::vector<float> ibm;
    ibm.reserve(25*16);
    for(int id=0;id<25;++id) {
        const auto& g=skel.at(id).global;
        // Gc = [R t;0 1], R = transpose(row 3x3), t = row translation * 0.1.
        double R[3][3]{};
        for(int r=0;r<3;++r) for(int c=0;c<3;++c) R[r][c]=g.m[c][r];
        const double t[3]{g.m[3][0]*0.1,g.m[3][1]*0.1,g.m[3][2]*0.1};
        // inverse rigid: R^T, -R^T t
        double I[4][4]{};
        for(int r=0;r<3;++r) for(int c=0;c<3;++c) I[r][c]=R[c][r];
        for(int r=0;r<3;++r)
            I[r][3]=-(I[r][0]*t[0]+I[r][1]*t[1]+I[r][2]*t[2]);
        I[3][3]=1.0;
        // glTF column-major flatten
        for(int c=0;c<4;++c) for(int r=0;r<4;++r) ibm.push_back(static_cast<float>(I[r][c]));
    }
    const int ibmAcc=bb.AddAccessor(ibm,5126,25,"MAT4");

    // Mesh node follows 25 joint nodes.
    NodeJson meshNode;
    meshNode.name=geoModelName;
    meshNode.mesh=0;
    meshNode.skin=0;
    const int meshNodeIndex=static_cast<int>(nodes.size());
    nodes.push_back(meshNode);

    // Optional animation.
    std::optional<MotionClip> animClip;
    std::vector<AnimSampler> animSamplers;
    std::vector<AnimChannel> animChannels;
    if(motionPath && clipName) {
        ZarArchive motion(*motionPath);
        animClip=LoadMotionClip(motion,*clipName);
        std::vector<float> times(animClip->frameCount+1);
        for(int i=0;i<=animClip->frameCount;++i) times[i]=i/30.0f;
        const int timeAcc=bb.AddAccessor(times,5126,times.size(),"SCALAR",std::nullopt,false,
                                         {0.0},{times.back()});
        std::unordered_map<std::string,int> idByName;
        for(int id=0;id<25;++id) idByName[skel.at(id).name]=id;

        for(const auto& tr:animClip->tracks) {
            auto it=idByName.find(tr.name);
            if(it==idByName.end()) continue; // attachment/helper not in this body graph
            const int node=it->second;
            const int samples=animClip->frameCount+1;
            std::vector<float> tv;tv.reserve(samples*3);
            for(int s=0;s<samples;++s) {
                const auto& v=tr.translations[tr.translations.size()==1?0:s];
                tv.push_back(v[0]*0.1f);tv.push_back(v[1]*0.1f);tv.push_back(v[2]*0.1f);
            }
            const int ta=bb.AddAccessor(tv,5126,samples,"VEC3");
            int si=static_cast<int>(animSamplers.size());
            animSamplers.push_back({timeAcc,ta});
            animChannels.push_back({si,node,"translation"});

            std::vector<float> qv;qv.reserve(samples*4);
            std::array<float,4> prev{0,0,0,1};
            for(int s=0;s<samples;++s) {
                auto q=tr.rotations[tr.rotations.size()==1?0:s];
                const double dot=double(prev[0])*q[0]+double(prev[1])*q[1]+double(prev[2])*q[2]+double(prev[3])*q[3];
                if(s>0 && dot<0.0) for(float& x:q) x=-x;
                const double l=std::sqrt(double(q[0])*q[0]+double(q[1])*q[1]+double(q[2])*q[2]+double(q[3])*q[3]);
                for(float& x:q) x=static_cast<float>(x/l);
                for(float x:q) qv.push_back(x);
                prev=q;
            }
            const int qa=bb.AddAccessor(qv,5126,samples,"VEC4");
            si=static_cast<int>(animSamplers.size());
            animSamplers.push_back({timeAcc,qa});
            animChannels.push_back({si,node,"rotation"});
        }
    }

    // Write BIN.
    const fs::path binPath=out/(mdlName+".bin");
    {
        std::ofstream f(binPath,std::ios::binary);
        if(!f) throw std::runtime_error("cannot create "+binPath.string());
        f.write(reinterpret_cast<const char*>(bb.data.data()),static_cast<std::streamsize>(bb.data.size()));
    }

    // JSON glTF.
    const fs::path gltfPath=out/(mdlName+".gltf");
    std::ofstream o(gltfPath);
    if(!o) throw std::runtime_error("cannot create "+gltfPath.string());
    o<<std::setprecision(9);
    o<<"{\n";
    o<<"  \"asset\": {\"version\": \"2.0\", \"generator\": \"SOCOM GameZ reverse-engineering exporter\"},\n";
    o<<"  \"scene\": 0,\n";
    o<<"  \"scenes\": [{\"nodes\": [0, "<<meshNodeIndex<<"]}],\n";

    o<<"  \"nodes\": [\n";
    for(std::size_t i=0;i<nodes.size();++i) {
        const auto& n=nodes[i];
        o<<"    {\"name\": \""<<JsonEscape(n.name)<<"\"";
        if(i<25) {
            o<<", \"translation\": ["<<n.t[0]<<","<<n.t[1]<<","<<n.t[2]<<"]";
            o<<", \"rotation\": ["<<n.q[0]<<","<<n.q[1]<<","<<n.q[2]<<","<<n.q[3]<<"]";
            if(!n.children.empty()) {
                o<<", \"children\": [";
                for(std::size_t k=0;k<n.children.size();++k) {if(k)o<<",";o<<n.children[k];}
                o<<"]";
            }
        }
        if(n.mesh) o<<", \"mesh\": "<<*n.mesh;
        if(n.skin) o<<", \"skin\": "<<*n.skin;
        o<<"}"<<(i+1<nodes.size()?",":"")<<"\n";
    }
    o<<"  ],\n";

    o<<"  \"meshes\": [{\"name\": \""<<JsonEscape(geoModelName)<<"\", \"primitives\": [\n";
    for(std::size_t i=0;i<prims.size();++i) {
        const auto& p=prims[i];
        o<<"    {\"attributes\": {\"POSITION\": "<<p.pos<<", \"NORMAL\": "<<p.nrm
         <<", \"TEXCOORD_0\": "<<p.uv<<", \"JOINTS_0\": "<<p.joints0
         <<", \"WEIGHTS_0\": "<<p.weights0;
        if(p.joints1>=0) o<<", \"JOINTS_1\": "<<p.joints1<<", \"WEIGHTS_1\": "<<p.weights1;
        o<<"}, \"indices\": "<<p.indices
         <<", \"material\": "<<p.material<<", \"mode\": 4}"
         <<(i+1<prims.size()?",":"")<<"\n";
    }
    o<<"  ]}],\n";

    o<<"  \"skins\": [{\"name\": \""<<JsonEscape(mdlName)<<"_skin\", \"inverseBindMatrices\": "<<ibmAcc
     <<", \"skeleton\": 0, \"joints\": [";
    for(int i=0;i<25;++i){if(i)o<<",";o<<i;} o<<"]}],\n";

    o<<"  \"samplers\": [{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}],\n";
    o<<"  \"images\": [";
    for(std::size_t i=0;i<materials.size();++i){if(i)o<<",";o<<"{\"name\":\""<<JsonEscape(materials[i].png)<<"\",\"uri\":\"textures/"<<JsonEscape(materials[i].png)<<"\"}";}
    o<<"],\n";
    o<<"  \"textures\": [";
    for(std::size_t i=0;i<materials.size();++i){if(i)o<<",";o<<"{\"sampler\":0,\"source\":"<<i<<"}";}
    o<<"],\n";
    o<<"  \"materials\": [";
    for(std::size_t i=0;i<materials.size();++i){if(i)o<<",";o<<"{\"name\":\""<<JsonEscape(materials[i].pic)<<"\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":"<<i<<"},\"metallicFactor\":0.0,\"roughnessFactor\":1.0}}";}
    o<<"],\n";

    if(animClip) {
        o<<"  \"animations\": [{\"name\": \""<<JsonEscape(animClip->name)<<"\", \"samplers\": [";
        for(std::size_t i=0;i<animSamplers.size();++i){if(i)o<<",";o<<"{\"input\":"<<animSamplers[i].input<<",\"output\":"<<animSamplers[i].output<<",\"interpolation\":\"LINEAR\"}";}
        o<<"], \"channels\": [";
        for(std::size_t i=0;i<animChannels.size();++i){if(i)o<<",";o<<"{\"sampler\":"<<animChannels[i].sampler<<",\"target\":{\"node\":"<<animChannels[i].node<<",\"path\":\""<<animChannels[i].path<<"\"}}";}
        o<<"]}],\n";
    }

    o<<"  \"buffers\": [{\"byteLength\": "<<bb.data.size()<<", \"uri\": \""<<JsonEscape(mdlName+".bin")<<"\"}],\n";
    o<<"  \"bufferViews\": [\n";
    for(std::size_t i=0;i<bb.views.size();++i) {
        const auto& v=bb.views[i];
        o<<"    {\"buffer\":0,\"byteOffset\":"<<v.offset<<",\"byteLength\":"<<v.length;
        if(v.target) o<<",\"target\":"<<*v.target;
        o<<"}"<<(i+1<bb.views.size()?",":"")<<"\n";
    }
    o<<"  ],\n";
    o<<"  \"accessors\": [\n";
    for(std::size_t i=0;i<bb.accessors.size();++i) {
        const auto& a=bb.accessors[i];
        o<<"    {\"bufferView\":"<<a.view<<",\"byteOffset\":0,\"componentType\":"<<a.componentType
         <<",\"count\":"<<a.count<<",\"type\":\""<<a.type<<"\"";
        if(a.normalized) o<<",\"normalized\":true";
        if(!a.minv.empty()) {o<<",\"min\":[";for(std::size_t k=0;k<a.minv.size();++k){if(k)o<<",";o<<a.minv[k];}o<<"]";}
        if(!a.maxv.empty()) {o<<",\"max\":[";for(std::size_t k=0;k<a.maxv.size();++k){if(k)o<<",";o<<a.maxv[k];}o<<"]";}
        o<<"}"<<(i+1<bb.accessors.size()?",":"")<<"\n";
    }
    o<<"  ]\n";
    o<<"}\n";

    std::cout<<"Exported "<<geoModelName<<" -> "<<gltfPath<<"\n"
             <<"  primitives: "<<batches.size()<<"\n"
             <<"  vertices:   "<<totalV<<"\n"
             <<"  triangles:  "<<totalT<<"\n"
             <<"  joints:     25\n"
             <<"  materials:  "<<materials.size()<<"\n";
    if(animClip) std::cout<<"  animation:  "<<animClip->name<<" ("<<animClip->frameCount<<" frames @ 30 Hz)\n";
}

static void Usage() {
    std::cerr<<
      "Usage:\n"
      "  socom_character_gltf <character_models.zar> <clib_mdl.zed> <png_dir>\n"
      "                       <MESH_model_name> <out_dir>\n"
      "                       [motion.zar clip_name]\n";
}

int main(int argc,char** argv) {
    try {
        if(argc!=6 && argc!=8) {Usage();return 1;}
        std::optional<fs::path> motion;
        std::optional<std::string> clip;
        if(argc==8) {motion=fs::path(argv[6]);clip=std::string(argv[7]);}
        WriteGltf(argv[1],argv[2],argv[3],argv[4],argv[5],motion,clip);
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"error: "<<e.what()<<"\n";
        return 2;
    }
}
