#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace json {
struct Value {
    using Object = std::map<std::string, Value>;
    using Array = std::vector<Value>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data{nullptr};

    bool isObject() const { return std::holds_alternative<Object>(data); }
    bool isArray() const { return std::holds_alternative<Array>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isBool() const { return std::holds_alternative<bool>(data); }

    const Object& object() const { return std::get<Object>(data); }
    const Array& array() const { return std::get<Array>(data); }
    const std::string& string() const { return std::get<std::string>(data); }
    double number() const { return std::get<double>(data); }
    bool boolean() const { return std::get<bool>(data); }

    const Value& at(const std::string& key) const {
        const auto& o = object();
        auto it = o.find(key);
        if (it == o.end()) throw std::runtime_error("JSON key missing: " + key);
        return it->second;
    }
    bool has(const std::string& key) const {
        if (!isObject()) return false;
        return object().find(key) != object().end();
    }
    const Value& at(std::size_t i) const { return array().at(i); }
    std::size_t size() const {
        if (isArray()) return array().size();
        if (isObject()) return object().size();
        return 0;
    }
    int intValue() const { return static_cast<int>(number()); }
    std::uint32_t u32Value() const { return static_cast<std::uint32_t>(number()); }
};

class Parser {
public:
    explicit Parser(std::string s) : s_(std::move(s)) {}
    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        if (p_ != s_.size()) fail("trailing characters");
        return v;
    }
private:
    [[noreturn]] void fail(const std::string& msg) const {
        throw std::runtime_error("JSON parse error at byte " + std::to_string(p_) + ": " + msg);
    }
    void skipWs() { while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_]))) ++p_; }
    bool take(char c) { if (p_ < s_.size() && s_[p_] == c) { ++p_; return true; } return false; }
    void expect(char c) { if (!take(c)) fail(std::string("expected '") + c + "'"); }
    Value parseValue() {
        skipWs();
        if (p_ >= s_.size()) fail("unexpected EOF");
        const char c = s_[p_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') { Value v; v.data = parseString(); return v; }
        if (c == '-' || (c >= '0' && c <= '9')) { Value v; v.data = parseNumber(); return v; }
        if (s_.compare(p_, 4, "true") == 0) { p_ += 4; Value v; v.data = true; return v; }
        if (s_.compare(p_, 5, "false") == 0) { p_ += 5; Value v; v.data = false; return v; }
        if (s_.compare(p_, 4, "null") == 0) { p_ += 4; return Value{}; }
        fail("invalid value");
    }
    Value parseObject() {
        expect('{'); skipWs();
        Value::Object o;
        if (take('}')) { Value v; v.data = std::move(o); return v; }
        for (;;) {
            skipWs();
            if (p_ >= s_.size() || s_[p_] != '"') fail("object key must be string");
            std::string key = parseString();
            skipWs(); expect(':'); skipWs();
            o.emplace(std::move(key), parseValue());
            skipWs();
            if (take('}')) break;
            expect(',');
        }
        Value v; v.data = std::move(o); return v;
    }
    Value parseArray() {
        expect('['); skipWs();
        Value::Array a;
        if (take(']')) { Value v; v.data = std::move(a); return v; }
        for (;;) {
            a.push_back(parseValue());
            skipWs();
            if (take(']')) break;
            expect(','); skipWs();
        }
        Value v; v.data = std::move(a); return v;
    }
    static int hex(char c) {
        if (c >= '0' && c <= '9') return c-'0';
        if (c >= 'a' && c <= 'f') return 10+c-'a';
        if (c >= 'A' && c <= 'F') return 10+c-'A';
        return -1;
    }
    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    std::string parseString() {
        expect('"');
        std::string out;
        while (p_ < s_.size()) {
            char c = s_[p_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') { out.push_back(c); continue; }
            if (p_ >= s_.size()) fail("bad escape");
            char e = s_[p_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (p_ + 4 > s_.size()) fail("short unicode escape");
                    std::uint32_t cp = 0;
                    for (int i=0;i<4;++i) { int h=hex(s_[p_++]); if (h<0) fail("bad unicode escape"); cp=(cp<<4)|static_cast<std::uint32_t>(h); }
                    appendUtf8(out, cp); break;
                }
                default: fail("unknown escape");
            }
        }
        fail("unterminated string");
    }
    double parseNumber() {
        const std::size_t start = p_;
        if (take('-')) {}
        if (take('0')) {}
        else {
            if (p_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[p_]))) fail("bad number");
            while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) ++p_;
        }
        if (take('.')) {
            if (p_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[p_]))) fail("bad fraction");
            while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) ++p_;
        }
        if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
            ++p_; if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) ++p_;
            if (p_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[p_]))) fail("bad exponent");
            while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) ++p_;
        }
        return std::stod(s_.substr(start, p_-start));
    }
    std::string s_;
    std::size_t p_{};
};

Value load(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open JSON: " + path.string());
    std::ostringstream ss; ss << in.rdbuf();
    return Parser(ss.str()).parse();
}
} // namespace json

struct Vec2 { float x{}, y{}; };
struct Vec3 { float x{}, y{}, z{}; };
static Vec3 sub(Vec3 a, Vec3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static Vec3 cross(Vec3 a, Vec3 b){ return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
static float dot(Vec3 a, Vec3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static float len(Vec3 a){ return std::sqrt(dot(a,a)); }
static Vec3 norm(Vec3 a){ float l=len(a); return l>1e-12f?Vec3{a.x/l,a.y/l,a.z/l}:Vec3{0,1,0}; }

struct Mat4 { std::array<double,16> v{}; };
static Mat4 identity(){ Mat4 m; m.v={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; return m; }
static Mat4 mul(const Mat4& a,const Mat4& b){ Mat4 o; for(int c=0;c<4;++c)for(int r=0;r<4;++r){double s=0;for(int k=0;k<4;++k)s+=a.v[k*4+r]*b.v[c*4+k];o.v[c*4+r]=s;}return o; }
static Mat4 quatMatrix(const std::array<double,4>& q0){
    double x=q0[0],y=q0[1],z=q0[2],w=q0[3]; double n=std::sqrt(x*x+y*y+z*z+w*w); if(n<=1e-12)return identity(); x/=n;y/=n;z/=n;w/=n;
    double xx=x*x,yy=y*y,zz=z*z,xy=x*y,xz=x*z,yz=y*z,wx=w*x,wy=w*y,wz=w*z;
    Mat4 m; m.v={1-2*(yy+zz),2*(xy+wz),2*(xz-wy),0, 2*(xy-wz),1-2*(xx+zz),2*(yz+wx),0, 2*(xz+wy),2*(yz-wx),1-2*(xx+yy),0, 0,0,0,1}; return m;
}
static Mat4 nodeMatrix(const json::Value& node){
    if(node.has("matrix")){ Mat4 m; const auto&a=node.at("matrix").array(); if(a.size()!=16) throw std::runtime_error("node matrix is not 16 values"); for(int i=0;i<16;++i)m.v[i]=a[i].number(); return m; }
    std::array<double,3> t{0,0,0},s{1,1,1}; std::array<double,4> r{0,0,0,1};
    if(node.has("translation")){const auto&a=node.at("translation").array();for(int i=0;i<3;++i)t[i]=a[i].number();}
    if(node.has("scale")){const auto&a=node.at("scale").array();for(int i=0;i<3;++i)s[i]=a[i].number();}
    if(node.has("rotation")){const auto&a=node.at("rotation").array();for(int i=0;i<4;++i)r[i]=a[i].number();}
    Mat4 tm=identity();tm.v[12]=t[0];tm.v[13]=t[1];tm.v[14]=t[2]; Mat4 sm=identity();sm.v[0]=s[0];sm.v[5]=s[1];sm.v[10]=s[2]; return mul(tm,mul(quatMatrix(r),sm));
}
static Vec3 transformPoint(const Mat4&m,const std::vector<double>&p){double x=p[0],y=p[1],z=p[2];return{static_cast<float>(m.v[0]*x+m.v[4]*y+m.v[8]*z+m.v[12]),static_cast<float>(m.v[1]*x+m.v[5]*y+m.v[9]*z+m.v[13]),static_cast<float>(m.v[2]*x+m.v[6]*y+m.v[10]*z+m.v[14])};}
static Vec3 transformNormal(const Mat4&m,const std::vector<double>&n){
    double a00=m.v[0],a01=m.v[4],a02=m.v[8],a10=m.v[1],a11=m.v[5],a12=m.v[9],a20=m.v[2],a21=m.v[6],a22=m.v[10];
    double c00=a11*a22-a12*a21,c01=a12*a20-a10*a22,c02=a10*a21-a11*a20,c10=a02*a21-a01*a22,c11=a00*a22-a02*a20,c12=a01*a20-a00*a21,c20=a01*a12-a02*a11,c21=a02*a10-a00*a12,c22=a00*a11-a01*a10;
    double det=a00*c00+a01*c01+a02*c02; double x=n[0],y=n[1],z=n[2];
    if(std::abs(det)<=1e-12) return norm({static_cast<float>(m.v[0]*x+m.v[4]*y+m.v[8]*z),static_cast<float>(m.v[1]*x+m.v[5]*y+m.v[9]*z),static_cast<float>(m.v[2]*x+m.v[6]*y+m.v[10]*z)});
    double d=1.0/det; // transpose(inverse(A)) directly
    return norm({static_cast<float>((c00*x+c01*y+c02*z)*d),static_cast<float>((c10*x+c11*y+c12*z)*d),static_cast<float>((c20*x+c21*y+c22*z)*d)});
}

static std::vector<std::uint8_t> readBytes(const fs::path&p){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("cannot open buffer: "+p.string());in.seekg(0,std::ios::end);auto n=in.tellg();in.seekg(0);std::vector<std::uint8_t>b(static_cast<std::size_t>(n));in.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!in)throw std::runtime_error("buffer read failed: "+p.string());return b;}

template<class T> static T readLE(const std::uint8_t* p){T v{};std::memcpy(&v,p,sizeof(v));return v;}

class Gltf {
public:
    explicit Gltf(const fs::path& path):path_(path),doc_(json::load(path)){
        if(!doc_.has("buffers")) throw std::runtime_error("glTF has no buffers");
        for(const auto& b:doc_.at("buffers").array()){
            std::string uri=b.at("uri").string(); if(uri.rfind("data:",0)==0) throw std::runtime_error("data URI buffers unsupported");
            buffers_.push_back(readBytes(path.parent_path()/fs::u8path(uri)));
        }
    }
    const json::Value& doc() const{return doc_;}
    std::vector<std::vector<double>> accessor(int index) const{
        const auto& acc=doc_.at("accessors").at(static_cast<std::size_t>(index)); if(!acc.has("bufferView"))throw std::runtime_error("sparse/bufferless accessor unsupported");
        const auto& view=doc_.at("bufferViews").at(static_cast<std::size_t>(acc.at("bufferView").intValue()));
        const int comp=acc.at("componentType").intValue(); const int count=acc.at("count").intValue(); const std::string type=acc.at("type").string();
        int ncomp=0; if(type=="SCALAR")ncomp=1;else if(type=="VEC2")ncomp=2;else if(type=="VEC3")ncomp=3;else if(type=="VEC4"||type=="MAT2")ncomp=4;else if(type=="MAT3")ncomp=9;else if(type=="MAT4")ncomp=16;else throw std::runtime_error("unsupported accessor type "+type);
        int csize=0;switch(comp){case 5120:case 5121:csize=1;break;case 5122:case 5123:csize=2;break;case 5125:case 5126:csize=4;break;default:throw std::runtime_error("unsupported componentType "+std::to_string(comp));}
        const int elem=csize*ncomp; const int stride=view.has("byteStride")?view.at("byteStride").intValue():elem;
        const std::size_t base=static_cast<std::size_t>(view.has("byteOffset")?view.at("byteOffset").intValue():0)+static_cast<std::size_t>(acc.has("byteOffset")?acc.at("byteOffset").intValue():0);
        const std::size_t bi=static_cast<std::size_t>(view.has("buffer")?view.at("buffer").intValue():0); const auto& data=buffers_.at(bi); const bool normalized=acc.has("normalized")&&acc.at("normalized").boolean();
        std::vector<std::vector<double>> out; out.reserve(static_cast<std::size_t>(count));
        for(int i=0;i<count;++i){ std::size_t pos=base+static_cast<std::size_t>(i)*static_cast<std::size_t>(stride); if(pos+elem>data.size())throw std::runtime_error("accessor outside buffer"); std::vector<double> vals;vals.reserve(ncomp); for(int j=0;j<ncomp;++j){const std::uint8_t*q=data.data()+pos+static_cast<std::size_t>(j*csize);double v=0;switch(comp){case 5120:v=static_cast<std::int8_t>(*q);break;case 5121:v=*q;break;case 5122:v=readLE<std::int16_t>(q);break;case 5123:v=readLE<std::uint16_t>(q);break;case 5125:v=readLE<std::uint32_t>(q);break;case 5126:v=readLE<float>(q);break;} if(normalized&&comp!=5126){if(comp==5121)v/=255.0;else if(comp==5123)v/=65535.0;else if(comp==5120)v=std::max(-1.0,v/127.0);else if(comp==5122)v=std::max(-1.0,v/32767.0);} vals.push_back(v);} out.push_back(std::move(vals)); }
        return out;
    }
    template<class F> void forWorldNodes(F&& f) const{
        int si=doc_.has("scene")?doc_.at("scene").intValue():0; const auto& roots=doc_.at("scenes").at(static_cast<std::size_t>(si)).at("nodes").array();
        std::function<void(int,const Mat4&)> walk=[&](int idx,const Mat4& parent){const auto& node=doc_.at("nodes").at(static_cast<std::size_t>(idx));Mat4 world=mul(parent,nodeMatrix(node));f(idx,world);if(node.has("children"))for(const auto& c:node.at("children").array())walk(c.intValue(),world);};
        Mat4 id=identity();for(const auto&r:roots)walk(r.intValue(),id);
    }
private: fs::path path_; json::Value doc_; std::vector<std::vector<std::uint8_t>> buffers_;
};

struct RenderVertex{
    Vec3 p{},n{};
    Vec2 uv{};
    std::array<std::uint8_t,4> c{255,255,255,255};
};

struct RenderBatch{
    std::uint32_t firstVertex{};
    std::uint32_t vertexCount{};
    std::uint32_t material{};
};

struct RuntimeTexture{
    std::string name;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t flags{};
    std::vector<std::uint8_t> rgba;
};

struct CollisionTriangle{Vec3 a{},b{},c{},n{};std::uint32_t material{},flags{};};


struct PlayerVertex {
    Vec3 p{}, n{};
    Vec2 uv{};
    std::array<std::uint8_t,4> joints{{0,0,0,0}};
    std::array<float,4> weights{{1.0f,0.0f,0.0f,0.0f}};
};
struct PlayerBatch { std::uint32_t firstVertex{}, vertexCount{}, material{}; };
struct PlayerTexture { std::string name; std::uint32_t width{},height{},flags{}; std::vector<std::uint8_t> rgba; };
struct Quat { float x{},y{},z{},w{1.0f}; };
struct Joint {
    std::string name;
    std::int32_t parent{-1};
    Vec3 bindT{};
    Quat bindR{};
    std::array<float,16> inverseBind{};
};
struct Pose { Vec3 t{}; Quat r{}; };
struct Clip {
    std::string name;
    float duration{};
    float sampleRate{30.0f};
    std::uint32_t sampleCount{};
    std::vector<Pose> poses; // sample-major, then joint
};

static Vec3 valueVec3(const json::Value& node,const char* key,Vec3 fallback={}) {
    if(!node.has(key)) return fallback;
    const auto&a=node.at(key).array();
    if(a.size()!=3) throw std::runtime_error(std::string(key)+" must be vec3");
    return {static_cast<float>(a[0].number()),static_cast<float>(a[1].number()),static_cast<float>(a[2].number())};
}
static Quat valueQuat(const json::Value& node,const char* key,Quat fallback={0,0,0,1}) {
    if(!node.has(key)) return fallback;
    const auto&a=node.at(key).array();
    if(a.size()!=4) throw std::runtime_error(std::string(key)+" must be quat");
    return {static_cast<float>(a[0].number()),static_cast<float>(a[1].number()),static_cast<float>(a[2].number()),static_cast<float>(a[3].number())};
}

static PlayerTexture loadSidecar(const fs::path& p,const std::string& name) {
    auto bytes=readBytes(p);
    if(bytes.size()<20) throw std::runtime_error("texture sidecar too small: "+p.string());
    const char magic[8]={'S','R','T','X','1','\0','\0','\0'};
    if(std::memcmp(bytes.data(),magic,8)!=0) throw std::runtime_error("bad texture sidecar: "+p.string());
    PlayerTexture t; t.name=name;
    t.width=readLE<std::uint32_t>(bytes.data()+8);
    t.height=readLE<std::uint32_t>(bytes.data()+12);
    t.flags=readLE<std::uint32_t>(bytes.data()+16);
    const std::uint64_t expected=20ull+static_cast<std::uint64_t>(t.width)*t.height*4ull;
    if(!t.width||!t.height||expected!=bytes.size()) throw std::runtime_error("texture sidecar size mismatch: "+p.string());
    t.rgba.assign(bytes.begin()+20,bytes.end());
    return t;
}

static std::vector<PlayerTexture> loadPlayerTextures(const Gltf& g,const fs::path& texDir) {
    const auto& doc=g.doc();
    const auto& mats=doc.at("materials").array();
    const auto& texs=doc.at("textures").array();
    const auto& imgs=doc.at("images").array();
    std::vector<PlayerTexture> out; out.reserve(mats.size());
    for(std::size_t mi=0;mi<mats.size();++mi) {
        const auto& mat=mats[mi];
        const auto& pbr=mat.at("pbrMetallicRoughness");
        const int ti=pbr.at("baseColorTexture").at("index").intValue();
        const int si=texs.at(static_cast<std::size_t>(ti)).at("source").intValue();
        fs::path uri=fs::u8path(imgs.at(static_cast<std::size_t>(si)).at("uri").string());
        fs::path side=texDir/uri.filename(); side.replace_extension(".srtx");
        const std::string name=mat.has("name")?mat.at("name").string():uri.filename().string();
        auto t=loadSidecar(side,name);
        if(mat.has("alphaMode") && mat.at("alphaMode").string()=="BLEND") t.flags|=1u;
        out.push_back(std::move(t));
    }
    return out;
}

static void extractMesh(const Gltf& g,std::vector<PlayerVertex>& verts,std::vector<PlayerBatch>& batches) {
    const auto& doc=g.doc();
    if(!doc.has("meshes")||doc.at("meshes").size()==0) throw std::runtime_error("player glTF has no mesh");
    const auto& mesh=doc.at("meshes").at(0);
    for(const auto& prim:mesh.at("primitives").array()) {
        const int mode=prim.has("mode")?prim.at("mode").intValue():4;
        if(mode!=4) continue;
        const auto& attrs=prim.at("attributes");
        auto pos=g.accessor(attrs.at("POSITION").intValue());
        auto nrm=g.accessor(attrs.at("NORMAL").intValue());
        auto uv=g.accessor(attrs.at("TEXCOORD_0").intValue());
        auto joints=g.accessor(attrs.at("JOINTS_0").intValue());
        auto weights=g.accessor(attrs.at("WEIGHTS_0").intValue());
        if(pos.size()!=nrm.size()||pos.size()!=uv.size()||pos.size()!=joints.size()||pos.size()!=weights.size())
            throw std::runtime_error("player attribute count mismatch");
        std::vector<int> indices;
        if(prim.has("indices")) {
            auto idx=g.accessor(prim.at("indices").intValue());
            indices.reserve(idx.size()); for(const auto&v:idx) indices.push_back(static_cast<int>(v[0]));
        } else { indices.resize(pos.size()); for(std::size_t i=0;i<indices.size();++i) indices[i]=static_cast<int>(i); }
        if(indices.size()%3) throw std::runtime_error("player indices not triangle aligned");
        const std::uint32_t first=static_cast<std::uint32_t>(verts.size());
        for(int si:indices) {
            if(si<0||static_cast<std::size_t>(si)>=pos.size()) throw std::runtime_error("player index out of range");
            PlayerVertex v;
            v.p={static_cast<float>(pos[si][0]),static_cast<float>(pos[si][1]),static_cast<float>(pos[si][2])};
            v.n=norm({static_cast<float>(nrm[si][0]),static_cast<float>(nrm[si][1]),static_cast<float>(nrm[si][2])});
            v.uv={static_cast<float>(uv[si][0]),static_cast<float>(uv[si][1])};
            float sum=0.0f;
            for(int k=0;k<4;++k) {
                const int ji=static_cast<int>(std::llround(joints[si][k]));
                if(ji<0||ji>255) throw std::runtime_error("player joint index outside uint8 range");
                v.joints[k]=static_cast<std::uint8_t>(ji);
                v.weights[k]=static_cast<float>(weights[si][k]);
                sum+=v.weights[k];
            }
            if(sum>1e-8f) for(float&w:v.weights) w/=sum;
            verts.push_back(v);
        }
        if(!indices.empty()) batches.push_back({first,static_cast<std::uint32_t>(indices.size()),static_cast<std::uint32_t>(prim.at("material").intValue())});
    }
}

static std::vector<Joint> extractJoints(const Gltf& g,std::map<int,int>& nodeToJoint) {
    const auto& doc=g.doc();
    const auto& skin=doc.at("skins").at(0);
    const auto& jnodes=skin.at("joints").array();
    auto inv=g.accessor(skin.at("inverseBindMatrices").intValue());
    if(inv.size()!=jnodes.size()) throw std::runtime_error("inverse bind count mismatch");
    std::vector<int> parentNode(doc.at("nodes").size(),-1);
    for(std::size_t ni=0;ni<doc.at("nodes").size();++ni) {
        const auto& n=doc.at("nodes").at(ni);
        if(n.has("children")) for(const auto& c:n.at("children").array()) parentNode.at(static_cast<std::size_t>(c.intValue()))=static_cast<int>(ni);
    }
    for(std::size_t ji=0;ji<jnodes.size();++ji) nodeToJoint[jnodes[ji].intValue()]=static_cast<int>(ji);
    std::vector<Joint> out; out.resize(jnodes.size());
    for(std::size_t ji=0;ji<jnodes.size();++ji) {
        const int nodeIndex=jnodes[ji].intValue(); const auto& node=doc.at("nodes").at(static_cast<std::size_t>(nodeIndex));
        Joint j; j.name=node.has("name")?node.at("name").string():("joint_"+std::to_string(ji));
        int pn=parentNode.at(static_cast<std::size_t>(nodeIndex)); auto it=nodeToJoint.find(pn); j.parent=(it==nodeToJoint.end())?-1:it->second;
        j.bindT=valueVec3(node,"translation"); j.bindR=valueQuat(node,"rotation");
        if(inv[ji].size()!=16) throw std::runtime_error("inverse bind accessor is not MAT4");
        for(int k=0;k<16;++k) j.inverseBind[k]=static_cast<float>(inv[ji][k]);
        out[ji]=std::move(j);
    }
    return out;
}

static Clip extractClip(const fs::path& path,const std::vector<Joint>& joints,const std::map<int,int>& nodeToJoint) {
    Gltf g(path); const auto& doc=g.doc();
    if(!doc.has("animations")||doc.at("animations").size()==0) throw std::runtime_error("animation missing in "+path.string());
    const auto& anim=doc.at("animations").at(0); const auto& samplers=anim.at("samplers").array(); const auto& channels=anim.at("channels").array();
    if(samplers.empty()) throw std::runtime_error("animation has no samplers");
    auto times=g.accessor(samplers[0].at("input").intValue());
    if(times.empty()) throw std::runtime_error("animation has no samples");
    Clip c; c.name=anim.has("name")?anim.at("name").string():path.stem().string(); c.sampleCount=static_cast<std::uint32_t>(times.size()); c.duration=static_cast<float>(times.back()[0]);
    if(c.duration>0.0f&&c.sampleCount>1) c.sampleRate=static_cast<float>(c.sampleCount-1)/c.duration;
    c.poses.resize(static_cast<std::size_t>(c.sampleCount)*joints.size());
    for(std::size_t f=0;f<c.sampleCount;++f) for(std::size_t j=0;j<joints.size();++j) c.poses[f*joints.size()+j]={joints[j].bindT,joints[j].bindR};
    for(const auto& ch:channels) {
        const int samplerIndex=ch.at("sampler").intValue(); const auto& samp=samplers.at(static_cast<std::size_t>(samplerIndex));
        auto channelTimes=g.accessor(samp.at("input").intValue()); auto values=g.accessor(samp.at("output").intValue());
        if(channelTimes.size()!=c.sampleCount||values.size()!=c.sampleCount) throw std::runtime_error("animation sampler sample count mismatch");
        const auto& target=ch.at("target"); const int node=target.at("node").intValue(); auto ji=nodeToJoint.find(node); if(ji==nodeToJoint.end()) continue;
        const std::string pathName=target.at("path").string();
        for(std::size_t f=0;f<c.sampleCount;++f) {
            auto& pose=c.poses[f*joints.size()+static_cast<std::size_t>(ji->second)];
            if(pathName=="translation") { if(values[f].size()!=3) throw std::runtime_error("translation output not VEC3"); pose.t={static_cast<float>(values[f][0]),static_cast<float>(values[f][1]),static_cast<float>(values[f][2])}; }
            else if(pathName=="rotation") { if(values[f].size()!=4) throw std::runtime_error("rotation output not VEC4"); pose.r={static_cast<float>(values[f][0]),static_cast<float>(values[f][1]),static_cast<float>(values[f][2]),static_cast<float>(values[f][3])}; }
        }
    }
    return c;
}

static void neutralizeLocomotionRoot(Clip& c,const std::vector<Joint>& joints) {
    if(joints.empty()||c.poses.empty()) return;
    std::size_t root=0;
    for(std::size_t i=0;i<joints.size();++i) {
        if(joints[i].name=="skel_root") { root=i; break; }
    }
    for(std::size_t f=0;f<c.sampleCount;++f) {
        auto& p=c.poses[f*joints.size()+root];
        p.t.x=joints[root].bindT.x;
        p.t.z=joints[root].bindT.z;
    }
}


template<class T>static void writeScalar(std::ofstream&f,const T&v){f.write(reinterpret_cast<const char*>(&v),sizeof(v));if(!f)throw std::runtime_error("write failed");}
static void writeVec3(std::ofstream&f,Vec3 v){writeScalar(f,v.x);writeScalar(f,v.y);writeScalar(f,v.z);}
static void writeQuat(std::ofstream&f,Quat q){writeScalar(f,q.x);writeScalar(f,q.y);writeScalar(f,q.z);writeScalar(f,q.w);}
static void writeString(std::ofstream&f,const std::string&s){const std::uint32_t n=static_cast<std::uint32_t>(s.size());writeScalar(f,n);if(n)f.write(s.data(),n);}

static void writePack(const fs::path& out,const std::vector<PlayerVertex>& verts,const std::vector<PlayerBatch>& batches,const std::vector<PlayerTexture>& textures,const std::vector<Joint>& joints,const std::vector<Clip>& clips) {
    fs::create_directories(out.parent_path()); std::ofstream f(out,std::ios::binary); if(!f) throw std::runtime_error("cannot create player pack");
    const char magic[8]={'S','O','C','O','M','P','1','\0'}; f.write(magic,8); const std::uint32_t version=1,flags=0;
    writeScalar(f,version); writeScalar(f,static_cast<std::uint32_t>(verts.size())); writeScalar(f,static_cast<std::uint32_t>(batches.size())); writeScalar(f,static_cast<std::uint32_t>(textures.size())); writeScalar(f,static_cast<std::uint32_t>(joints.size())); writeScalar(f,static_cast<std::uint32_t>(clips.size())); writeScalar(f,flags);
    for(const auto&v:verts){writeVec3(f,v.p);writeVec3(f,v.n);writeScalar(f,v.uv.x);writeScalar(f,v.uv.y);f.write(reinterpret_cast<const char*>(v.joints.data()),4);for(float w:v.weights)writeScalar(f,w);}
    for(const auto&b:batches){writeScalar(f,b.firstVertex);writeScalar(f,b.vertexCount);writeScalar(f,b.material);}
    for(const auto&t:textures){writeScalar(f,t.width);writeScalar(f,t.height);writeScalar(f,t.flags);writeString(f,t.name);const std::uint32_t bytes=static_cast<std::uint32_t>(t.rgba.size());writeScalar(f,bytes);if(bytes)f.write(reinterpret_cast<const char*>(t.rgba.data()),bytes);}
    for(const auto&j:joints){writeString(f,j.name);writeScalar(f,j.parent);writeVec3(f,j.bindT);writeQuat(f,j.bindR);for(float x:j.inverseBind)writeScalar(f,x);}
    for(const auto&c:clips){writeString(f,c.name);writeScalar(f,c.duration);writeScalar(f,c.sampleRate);writeScalar(f,c.sampleCount);for(const auto&p:c.poses){writeVec3(f,p.t);writeQuat(f,p.r);}}
    if(!f)throw std::runtime_error("player pack write failed");
}

int main(int argc,char**argv){try{
    fs::path stand,walk,run,textures,out,report;
    for(int i=1;i<argc;++i){std::string a=argv[i];auto need=[&](const char*n){if(i+1>=argc)throw std::runtime_error(std::string("missing value for ")+n);return std::string(argv[++i]);};if(a=="--stand")stand=need("--stand");else if(a=="--walk")walk=need("--walk");else if(a=="--run")run=need("--run");else if(a=="--textures")textures=need("--textures");else if(a=="--out")out=need("--out");else if(a=="--report")report=need("--report");else if(a=="--help"||a=="-h"){std::cout<<"Usage: build_player_runtime_pack --stand stand.gltf --walk walk.gltf --run run.gltf --textures decoded_clib_dir --out seal_A_des.spr [--report report.json]\n";return 0;}else throw std::runtime_error("unknown argument: "+a);}
    if(stand.empty()||walk.empty()||run.empty()||textures.empty()||out.empty())throw std::runtime_error("--stand, --walk, --run, --textures and --out are required");
    Gltf base(stand); std::vector<PlayerVertex>verts;std::vector<PlayerBatch>batches;extractMesh(base,verts,batches);auto tex=loadPlayerTextures(base,textures);std::map<int,int>nodeToJoint;auto joints=extractJoints(base,nodeToJoint);std::vector<Clip>clips;clips.push_back(extractClip(stand,joints,nodeToJoint));clips.push_back(extractClip(walk,joints,nodeToJoint));clips.push_back(extractClip(run,joints,nodeToJoint));for(auto& clip:clips) neutralizeLocomotionRoot(clip,joints);
    for(const auto&b:batches)if(b.material>=tex.size())throw std::runtime_error("player batch references missing material");
    writePack(out,verts,batches,tex,joints,clips);
    std::uint64_t rgba=0;for(const auto&t:tex)rgba+=t.rgba.size();
    std::ostringstream ss;
    ss << "{\n"
       << "  \"format\": \"SOCOMP1\",\n"
       << "  \"version\": 1,\n"
       << "  \"vertices\": " << verts.size() << ",\n"
       << "  \"triangles\": " << verts.size()/3 << ",\n"
       << "  \"batches\": " << batches.size() << ",\n"
       << "  \"textures\": " << tex.size() << ",\n"
       << "  \"texture_rgba_bytes\": " << rgba << ",\n"
       << "  \"joints\": " << joints.size() << ",\n"
       << "  \"clips\": [";
    for(std::size_t i=0;i<clips.size();++i){if(i)ss<<", ";ss<<"\""<<clips[i].name<<"\"";}
    ss << "]\n}\n";
    std::cout<<"[player-pack] vertices: "<<verts.size()<<" triangles: "<<verts.size()/3<<" batches: "<<batches.size()<<" textures: "<<tex.size()<<" joints: "<<joints.size()<<" clips: "<<clips.size()<<"\n";
    std::cout<<ss.str();
    if(!report.empty()){fs::create_directories(report.parent_path());std::ofstream rf(report);rf<<ss.str();}
    return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<"\n";return 1;}}
