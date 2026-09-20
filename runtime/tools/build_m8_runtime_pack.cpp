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
static std::array<std::uint8_t,4> fallback(int material){std::uint32_t x=static_cast<std::uint32_t>(material+1)*0x9E3779B1u;return{static_cast<std::uint8_t>(std::min(255u,80u+((x>>0)&0x7Fu))),static_cast<std::uint8_t>(std::min(255u,80u+((x>>8)&0x7Fu))),static_cast<std::uint8_t>(std::min(255u,80u+((x>>16)&0x7Fu))),255};}
static std::array<std::uint8_t,4> colorU8(const std::vector<double>*v,int mat){if(!v)return fallback(mat);std::array<std::uint8_t,4>o{255,255,255,255};for(std::size_t i=0;i<std::min<std::size_t>(4,v->size());++i){double x=(*v)[i];if(x<=1.0001)x*=255.;x=std::max(0.,std::min(255.,x));o[i]=static_cast<std::uint8_t>(std::lround(x));}return o;}

struct Stats{std::uint64_t renderPrimitives{},renderTriangles{},renderVertices{},renderBatches{},textures{},textureBytes{},skippedNonTriangles{},collisionTriangles{},skippedDegenerate{};};
struct Bounds{double minx=std::numeric_limits<double>::infinity(),miny=minx,minz=minx,maxx=-minx,maxy=-minx,maxz=-minx;void add(Vec3 p){minx=std::min(minx,(double)p.x);miny=std::min(miny,(double)p.y);minz=std::min(minz,(double)p.z);maxx=std::max(maxx,(double)p.x);maxy=std::max(maxy,(double)p.y);maxz=std::max(maxz,(double)p.z);} };

static RuntimeTexture loadSidecarTexture(
    const fs::path& path,
    const std::string& name,
    bool materialBlend)
{
    const auto bytes=readBytes(path);
    if(bytes.size()<20) throw std::runtime_error("runtime texture sidecar too small: "+path.string());
    const char expected[8]={'S','R','T','X','1','\0','\0','\0'};
    if(std::memcmp(bytes.data(),expected,8)!=0)
        throw std::runtime_error("bad runtime texture sidecar magic: "+path.string());

    RuntimeTexture out;
    out.name=name;
    out.width=readLE<std::uint32_t>(bytes.data()+8);
    out.height=readLE<std::uint32_t>(bytes.data()+12);
    out.flags=readLE<std::uint32_t>(bytes.data()+16);
    if(materialBlend) out.flags|=1u;

    const std::uint64_t expectedBytes=
        static_cast<std::uint64_t>(out.width)*
        static_cast<std::uint64_t>(out.height)*4ull;
    if(!out.width||!out.height||20ull+expectedBytes!=bytes.size())
        throw std::runtime_error("runtime texture sidecar size mismatch: "+path.string());

    out.rgba.assign(bytes.begin()+20,bytes.end());
    return out;
}

static std::vector<RuntimeTexture> loadTextures(
    const fs::path& scenePath,
    const Gltf& g,
    Stats& stats)
{
    if(!g.doc().has("materials")||!g.doc().has("textures")||!g.doc().has("images"))
        throw std::runtime_error("scene glTF is missing materials/textures/images");

    const auto& materials=g.doc().at("materials").array();
    const auto& textures=g.doc().at("textures").array();
    const auto& images=g.doc().at("images").array();

    std::vector<RuntimeTexture> out;
    out.reserve(materials.size());

    for(std::size_t mi=0;mi<materials.size();++mi){
        const auto& mat=materials[mi];
        const std::string name=
            mat.has("name")?mat.at("name").string():
            ("material_"+std::to_string(mi));

        if(!mat.has("pbrMetallicRoughness"))
            throw std::runtime_error("material missing pbrMetallicRoughness: "+name);
        const auto& pbr=mat.at("pbrMetallicRoughness");
        if(!pbr.has("baseColorTexture"))
            throw std::runtime_error("material missing baseColorTexture: "+name);

        const int textureIndex=pbr.at("baseColorTexture").at("index").intValue();
        if(textureIndex<0||static_cast<std::size_t>(textureIndex)>=textures.size())
            throw std::runtime_error("material texture index outside glTF: "+name);

        const auto& tex=textures[static_cast<std::size_t>(textureIndex)];
        const int source=tex.at("source").intValue();
        if(source<0||static_cast<std::size_t>(source)>=images.size())
            throw std::runtime_error("texture source outside glTF: "+name);

        const std::string uri=images[static_cast<std::size_t>(source)].at("uri").string();
        fs::path raw=scenePath.parent_path()/fs::u8path(uri);
        raw.replace_extension(".srtx");

        const bool blend=
            mat.has("alphaMode")&&
            mat.at("alphaMode").string()=="BLEND";

        auto texture=loadSidecarTexture(raw,name,blend);
        stats.textureBytes+=texture.rgba.size();
        out.push_back(std::move(texture));
    }

    stats.textures=out.size();
    return out;
}

static void extractRender(
    const fs::path&path,
    std::vector<RenderVertex>&verts,
    std::vector<RenderBatch>&batches,
    Bounds&b,
    Stats&s)
{
    Gltf g(path);
    g.forWorldNodes([&](int ni,const Mat4&w){
        const auto&node=g.doc().at("nodes").at(ni);
        if(!node.has("mesh"))return;
        const auto&mesh=g.doc().at("meshes").at(node.at("mesh").intValue());
        for(const auto&prim:mesh.at("primitives").array()){
            int mode=prim.has("mode")?prim.at("mode").intValue():4;
            if(mode!=4){++s.skippedNonTriangles;continue;}

            const auto&attrs=prim.at("attributes");
            auto pos=g.accessor(attrs.at("POSITION").intValue());
            if(!attrs.has("TEXCOORD_0"))
                throw std::runtime_error("render primitive is missing TEXCOORD_0");
            auto uvs=g.accessor(attrs.at("TEXCOORD_0").intValue());

            std::vector<std::vector<double>> normals,colors;
            bool hn=attrs.has("NORMAL"),hc=attrs.has("COLOR_0");
            if(hn)normals=g.accessor(attrs.at("NORMAL").intValue());
            if(hc)colors=g.accessor(attrs.at("COLOR_0").intValue());

            if(uvs.size()!=pos.size())
                throw std::runtime_error("UV/position accessor count mismatch");

            std::vector<int> idx;
            if(prim.has("indices")){
                auto raw=g.accessor(prim.at("indices").intValue());
                idx.reserve(raw.size());
                for(auto&x:raw)idx.push_back(static_cast<int>(x[0]));
            }else{
                idx.resize(pos.size());
                for(std::size_t i=0;i<idx.size();++i)idx[i]=static_cast<int>(i);
            }
            if(idx.size()%3)throw std::runtime_error("triangle index count not divisible by 3");

            const int mat=prim.has("material")?prim.at("material").intValue():0;
            if(mat<0)throw std::runtime_error("negative material index");

            const auto first=static_cast<std::uint32_t>(verts.size());
            ++s.renderPrimitives;

            for(int si:idx){
                if(si<0||static_cast<std::size_t>(si)>=pos.size())
                    throw std::runtime_error("render index outside position accessor");
                RenderVertex rv;
                rv.p=transformPoint(w,pos[si]);
                rv.n=hn?transformNormal(w,normals[si]):Vec3{0,1,0};
                rv.uv={
                    static_cast<float>(uvs[si][0]),
                    static_cast<float>(uvs[si][1])
                };
                rv.c=colorU8(hc?&colors[si]:nullptr,mat);
                verts.push_back(rv);
                b.add(rv.p);
            }

            if(!idx.empty()){
                batches.push_back({
                    first,
                    static_cast<std::uint32_t>(idx.size()),
                    static_cast<std::uint32_t>(mat)
                });
            }
        }
    });
    s.renderVertices=verts.size();
    s.renderTriangles=verts.size()/3;
    s.renderBatches=batches.size();
}

static void extractCollision(const fs::path&path,std::vector<CollisionTriangle>&tris,Bounds&b,Stats&s){Gltf g(path);g.forWorldNodes([&](int ni,const Mat4&w){const auto&node=g.doc().at("nodes").at(ni);if(!node.has("mesh"))return;const auto&mesh=g.doc().at("meshes").at(node.at("mesh").intValue());for(const auto&prim:mesh.at("primitives").array()){int mode=prim.has("mode")?prim.at("mode").intValue():4;if(mode!=4)continue;const auto&attrs=prim.at("attributes");auto pos=g.accessor(attrs.at("POSITION").intValue());std::vector<int>idx;if(prim.has("indices")){auto raw=g.accessor(prim.at("indices").intValue());for(auto&x:raw)idx.push_back(static_cast<int>(x[0]));}else{idx.resize(pos.size());for(std::size_t i=0;i<idx.size();++i)idx[i]=static_cast<int>(i);}if(idx.size()%3)throw std::runtime_error("collision index count not divisible by 3");int mat=prim.has("material")?prim.at("material").intValue():0;for(std::size_t i=0;i<idx.size();i+=3){Vec3 a=transformPoint(w,pos.at(idx[i])),bb=transformPoint(w,pos.at(idx[i+1])),c=transformPoint(w,pos.at(idx[i+2]));Vec3 cr=cross(sub(bb,a),sub(c,a));if(len(cr)<=1e-8f){++s.skippedDegenerate;continue;}CollisionTriangle t{a,bb,c,norm(cr),static_cast<std::uint32_t>(mat),0};tris.push_back(t);b.add(a);b.add(bb);b.add(c);}}});s.collisionTriangles=tris.size();}
static double triArea(const CollisionTriangle&t){return 0.5*len(cross(sub(t.b,t.a),sub(t.c,t.a)));}
static Vec3 chooseSpawn(const std::vector<CollisionTriangle>&tris,const Bounds&b){if(tris.empty())throw std::runtime_error("no collision triangles");struct C{double x,y,z,area;};std::vector<C>w;double cx=(b.minx+b.maxx)*0.5,cz=(b.minz+b.maxz)*0.5;for(auto&t:tris){if(t.n.y<0.70f)continue;double area=triArea(t);if(area<0.15)continue;w.push_back({(t.a.x+t.b.x+t.c.x)/3.0,(t.a.y+t.b.y+t.c.y)/3.0,(t.a.z+t.b.z+t.c.z)/3.0,area});}if(w.empty()){auto&t=tris[0];return{(t.a.x+t.b.x+t.c.x)/3.0f,(t.a.y+t.b.y+t.c.y)/3.0f+1.0f,(t.a.z+t.b.z+t.c.z)/3.0f};}std::vector<double>ys;for(auto&v:w)ys.push_back(v.y);std::sort(ys.begin(),ys.end());const std::size_t lowBase=static_cast<std::size_t>(ys.size()*0.10); const std::size_t lowIndex=(lowBase>0)?(lowBase-1):0; const std::size_t highIndex=std::min(ys.size()-1,static_cast<std::size_t>(ys.size()*0.70)); double median=ys[ys.size()/2],low=ys[lowIndex],high=ys[highIndex];const C*best=nullptr;double bestScore=1e300;for(auto&v:w){if(v.y<low-2||v.y>high+2)continue;double score=std::hypot(v.x-cx,v.z-cz)+std::abs(v.y-median)*3.0-std::min(v.area,25.0)*0.15;if(score<bestScore){bestScore=score;best=&v;}}if(!best)best=&w[0];return{static_cast<float>(best->x),static_cast<float>(best->y+1.05),static_cast<float>(best->z)};}

template<class T>static void writeScalar(std::ofstream&f,const T&v){f.write(reinterpret_cast<const char*>(&v),sizeof(v));if(!f)throw std::runtime_error("write failed");}
static void writeVec(std::ofstream&f,Vec3 v){writeScalar(f,v.x);writeScalar(f,v.y);writeScalar(f,v.z);}
static void writePack(
    const fs::path&out,
    const std::vector<RenderVertex>&v,
    const std::vector<RenderBatch>&batches,
    const std::vector<RuntimeTexture>&textures,
    const std::vector<CollisionTriangle>&t,
    const Bounds&b,
    Vec3 spawn)
{
    fs::create_directories(out.parent_path());
    std::ofstream f(out,std::ios::binary);
    if(!f)throw std::runtime_error("cannot create pack: "+out.string());

    const char magic[8]={'S','O','C','O','M','R','1','\0'};
    f.write(magic,8);

    const std::uint32_t ver=2;
    const std::uint32_t vc=static_cast<std::uint32_t>(v.size());
    const std::uint32_t tc=static_cast<std::uint32_t>(t.size());
    const std::uint32_t flags=1u; // bit 0 = textured render data present
    const std::uint32_t bc=static_cast<std::uint32_t>(batches.size());
    const std::uint32_t texc=static_cast<std::uint32_t>(textures.size());

    writeScalar(f,ver);
    writeScalar(f,vc);
    writeScalar(f,tc);
    writeScalar(f,flags);
    writeScalar(f,bc);
    writeScalar(f,texc);

    float bf[6]={
        (float)b.minx,(float)b.miny,(float)b.minz,
        (float)b.maxx,(float)b.maxy,(float)b.maxz
    };
    f.write(reinterpret_cast<const char*>(bf),sizeof(bf));
    writeVec(f,spawn);

    for(const auto&r:v){
        writeVec(f,r.p);
        writeVec(f,r.n);
        writeScalar(f,r.uv.x);
        writeScalar(f,r.uv.y);
        f.write(reinterpret_cast<const char*>(r.c.data()),4);
    }

    for(const auto&batch:batches){
        writeScalar(f,batch.firstVertex);
        writeScalar(f,batch.vertexCount);
        writeScalar(f,batch.material);
    }

    for(const auto&texture:textures){
        const auto nameLength=static_cast<std::uint32_t>(texture.name.size());
        const auto pixelBytes=static_cast<std::uint32_t>(texture.rgba.size());
        writeScalar(f,texture.width);
        writeScalar(f,texture.height);
        writeScalar(f,texture.flags);
        writeScalar(f,nameLength);
        writeScalar(f,pixelBytes);
        if(nameLength)
            f.write(texture.name.data(),static_cast<std::streamsize>(nameLength));
        if(pixelBytes)
            f.write(
                reinterpret_cast<const char*>(texture.rgba.data()),
                static_cast<std::streamsize>(pixelBytes));
    }

    for(const auto&x:t){
        writeVec(f,x.a);
        writeVec(f,x.b);
        writeVec(f,x.c);
        writeVec(f,x.n);
        writeScalar(f,x.material);
        writeScalar(f,x.flags);
    }

    if(!f)throw std::runtime_error("pack write failed");
}
static std::uintmax_t fileSize(const fs::path&p){return fs::file_size(p);}

int main(int argc,char**argv){try{
    fs::path scene,collision,out,report; bool spawnSet=false;Vec3 spawn{};
    for(int i=1;i<argc;++i){std::string a=argv[i];auto need=[&](const char*n)->std::string{if(i+1>=argc)throw std::runtime_error(std::string("missing value for ")+n);return argv[++i];};if(a=="--scene")scene=need("--scene");else if(a=="--collision")collision=need("--collision");else if(a=="--out")out=need("--out");else if(a=="--report")report=need("--report");else if(a=="--spawn"){if(i+3>=argc)throw std::runtime_error("--spawn needs X Y Z");spawn={std::stof(argv[++i]),std::stof(argv[++i]),std::stof(argv[++i])};spawnSet=true;}else if(a=="--help"||a=="-h"){std::cout<<"Usage: build_m8_runtime_pack --scene m8_scene.gltf --collision worldmodel_collision.gltf --out m8_runtime.snr [--report report.json] [--spawn X Y Z]\n";return 0;}else throw std::runtime_error("unknown argument: "+a);}
    if(scene.empty()||collision.empty()||out.empty()) throw std::runtime_error("--scene, --collision and --out are required");
    if(!fs::is_regular_file(scene)) throw std::runtime_error("scene not found: "+scene.string());
    if(!fs::is_regular_file(collision)) throw std::runtime_error("collision not found: "+collision.string());
    std::cout<<"[pack] Reading visual scene: "<<scene.string()<<"\n";
    std::vector<RenderVertex>verts;
    std::vector<RenderBatch>batches;
    Bounds vb;
    Stats st;
    extractRender(scene,verts,batches,vb,st);

    Gltf sceneGltf(scene);
    auto textures=loadTextures(scene,sceneGltf,st);

    for(const auto&batch:batches){
        if(batch.material>=textures.size())
            throw std::runtime_error("render batch references missing texture");
    }

    std::cout<<"[pack] Textures: "<<textures.size()
             <<" ("<<st.textureBytes<<" RGBA bytes)\n";

    std::cout<<"[pack] Reading collision: "<<collision.string()<<"\n";
    std::vector<CollisionTriangle>tris;
    Bounds cb;
    extractCollision(collision,tris,cb,st);

    Bounds all;
    all.minx=std::min(vb.minx,cb.minx);
    all.miny=std::min(vb.miny,cb.miny);
    all.minz=std::min(vb.minz,cb.minz);
    all.maxx=std::max(vb.maxx,cb.maxx);
    all.maxy=std::max(vb.maxy,cb.maxy);
    all.maxz=std::max(vb.maxz,cb.maxz);
    if(!spawnSet)spawn=chooseSpawn(tris,cb);
    writePack(out,verts,batches,textures,tris,all,spawn);
    std::ostringstream js;
    js<<std::fixed<<std::setprecision(6)
      <<"{\n"
      <<"  \"format\": \"SOCOMR1\",\n"
      <<"  \"version\": 2,\n"
      <<"  \"render_primitives\": "<<st.renderPrimitives<<",\n"
      <<"  \"render_batches\": "<<st.renderBatches<<",\n"
      <<"  \"render_triangles\": "<<st.renderTriangles<<",\n"
      <<"  \"render_vertices\": "<<st.renderVertices<<",\n"
      <<"  \"textures\": "<<st.textures<<",\n"
      <<"  \"texture_rgba_bytes\": "<<st.textureBytes<<",\n"
      <<"  \"skipped_non_triangle_primitives\": "<<st.skippedNonTriangles<<",\n"
      <<"  \"collision_triangles\": "<<st.collisionTriangles<<",\n"
      <<"  \"skipped_degenerate_collision_triangles\": "<<st.skippedDegenerate<<",\n"
      <<"  \"bounds_min_m\": ["<<all.minx<<", "<<all.miny<<", "<<all.minz<<"],\n"
      <<"  \"bounds_max_m\": ["<<all.maxx<<", "<<all.maxy<<", "<<all.maxz<<"],\n"
      <<"  \"spawn_m\": ["<<spawn.x<<", "<<spawn.y<<", "<<spawn.z<<"],\n"
      <<"  \"output\": \""<<out.generic_string()<<"\",\n"
      <<"  \"output_bytes\": "<<fileSize(out)<<"\n"
      <<"}\n";
    std::cout<<js.str();if(!report.empty()){fs::create_directories(report.parent_path());std::ofstream rf(report);if(!rf)throw std::runtime_error("cannot write report: "+report.string());rf<<js.str();}
    return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<"\n";return 1;}}
