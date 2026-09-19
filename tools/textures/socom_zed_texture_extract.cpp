// socom_zed_texture_extract.cpp
// SOCOM 1 GameZ *_txr.zed + *_pal.zed texture extractor.
// C++17, macOS/Apple Silicon friendly, no external dependencies.
//
// Build:
//   clang++ -std=c++17 -O2 -Wall -Wextra socom_zed_texture_extract.cpp -o socom_zed_texture_extract
//
// Usage:
//   ./socom_zed_texture_extract clib_txr.zed clib_pal.zed out_dir
//
// Writes one uncompressed 32-bit TGA per texture.
// Confirmed for the supplied SOCOM 1 character and weapon banks:
//   - CZAR/ZAR v0x00020002
//   - linear PSMT8 source indices
//   - TEX0.CBP -> texpal_<id>
//   - CSM1 palette permutation
//   - PSMCT16 palettes

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>

namespace fs = std::filesystem;

#pragma pack(push,1)
struct ZarHeader {
    uint32_t u00, nodeCount, stringSize, preferredBase, alignment;
    uint8_t reserved[0x40];
    uint32_t payloadSize, u58, u5c, version;
};
struct ZarNodeDisk {
    uint32_t nameAddress, dataOffset, dataSize, childCount;
};
#pragma pack(pop)

static_assert(sizeof(ZarHeader)==0x64);
static_assert(sizeof(ZarNodeDisk)==0x10);

template<class T>
T rd(const uint8_t* p) {
    T v{};
    std::memcpy(&v,p,sizeof(v));
    return v;
}
size_t align_up(size_t v,size_t a) {
    if(!a) throw std::runtime_error("zero alignment");
    size_t r=v%a;
    return r ? v+(a-r) : v;
}

struct Node {
    std::string name;
    uint32_t off{}, size{};
    std::vector<Node> children;
};

class Zar {
public:
    explicit Zar(const fs::path& p) {
        std::ifstream f(p,std::ios::binary);
        if(!f) throw std::runtime_error("cannot open "+p.string());
        f.seekg(0,std::ios::end);
        auto n=f.tellg();
        f.seekg(0);
        bytes.resize((size_t)n);
        f.read((char*)bytes.data(),(std::streamsize)bytes.size());

        if(bytes.size()<sizeof(ZarHeader))
            throw std::runtime_error("file too small");
        std::memcpy(&h,bytes.data(),sizeof(h));
        if(h.version!=0x00020002)
            throw std::runtime_error("unexpected ZAR version");

        strOff=sizeof(ZarHeader);
        nodeOff=strOff+h.stringSize;
        size_t nodeBytes=(size_t)h.nodeCount*sizeof(ZarNodeDisk);
        payloadOff=align_up(nodeOff+nodeBytes,h.alignment);
        if(payloadOff+h.payloadSize!=bytes.size())
            throw std::runtime_error("bad ZAR layout");

        disk.resize(h.nodeCount);
        std::memcpy(disk.data(),bytes.data()+nodeOff,nodeBytes);

        size_t c=0;
        root=parse(c);
        if(c!=disk.size())
            throw std::runtime_error("tree parse mismatch");
    }

    const uint8_t* payload(uint32_t o,size_t n=1) const {
        if((uint64_t)o+n>h.payloadSize)
            throw std::runtime_error("payload out of range");
        return bytes.data()+payloadOff+o;
    }

    Node root;

private:
    std::string name(uint32_t a) const {
        if(!a) return "<root>";
        if(a<h.preferredBase) throw std::runtime_error("bad name ptr");
        size_t rel=(size_t)(a-h.preferredBase);
        if(rel>=h.stringSize) throw std::runtime_error("name ptr OOB");
        size_t s=strOff+rel, e=s, lim=strOff+h.stringSize;
        while(e<lim && bytes[e]) ++e;
        if(e==lim) throw std::runtime_error("unterminated name");
        return std::string((const char*)bytes.data()+s,e-s);
    }

    Node parse(size_t& c) {
        if(c>=disk.size()) throw std::runtime_error("tree overflow");
        auto d=disk[c++];
        Node n;
        n.name=name(d.nameAddress);
        n.off=d.dataOffset;
        n.size=d.dataSize;
        for(uint32_t i=0;i<d.childCount;i++)
            n.children.push_back(parse(c));
        return n;
    }

    std::vector<uint8_t> bytes;
    ZarHeader h{};
    std::vector<ZarNodeDisk> disk;
    size_t strOff{},nodeOff{},payloadOff{};
};

const Node* child(const Node& n,const std::string& s) {
    for(auto& c:n.children) if(c.name==s) return &c;
    return nullptr;
}

struct RGBA { uint8_t r,g,b,a; };

size_t csm1(size_t p) {
    return (p & 0xE7u) + ((p & 0x08u)<<1) + ((p & 0x10u)>>1);
}
uint8_t exp5(uint32_t v) {
    return (uint8_t)((v<<3)|(v>>2));
}

struct Tex0 {
    uint32_t psm{},cbp{},cpsm{},csm{};
};
Tex0 decode_tex0(uint64_t v) {
    Tex0 t;
    t.psm=(v>>20)&0x3f;
    t.cbp=(v>>37)&0x3fff;
    t.cpsm=(v>>51)&0xf;
    t.csm=(v>>55)&1;
    return t;
}

void write_tga(const fs::path& p,uint16_t w,uint16_t h,const std::vector<RGBA>& px) {
    std::ofstream o(p,std::ios::binary);
    if(!o) throw std::runtime_error("cannot create "+p.string());

    uint8_t hdr[18]{};
    hdr[2]=2; // uncompressed true-color
    hdr[12]=(uint8_t)(w&0xff); hdr[13]=(uint8_t)(w>>8);
    hdr[14]=(uint8_t)(h&0xff); hdr[15]=(uint8_t)(h>>8);
    hdr[16]=32;
    hdr[17]=0x28; // 8 alpha bits + top-left origin
    o.write((char*)hdr,18);

    for(auto& c:px) {
        uint8_t bgra[4]{c.b,c.g,c.r,c.a};
        o.write((char*)bgra,4);
    }
}

int main(int argc,char** argv) {
    try {
        if(argc!=4) {
            std::cerr<<"Usage: "<<argv[0]<<" <*_txr.zed> <*_pal.zed> <out_dir>\n";
            return 1;
        }

        Zar txr(argv[1]), pal(argv[2]);
        fs::path out=argv[3];
        fs::create_directories(out);

        const Node* textures=child(txr.root,"textures");
        const Node* palettes=child(pal.root,"palettes");
        if(!textures || !palettes)
            throw std::runtime_error("missing textures/palettes root");

        std::map<uint32_t,std::array<RGBA,256>> palmap;

        for(auto& pn:palettes->children) {
            const Node* buf=child(pn,"buf");
            if(!buf || buf->size!=0x200)
                throw std::runtime_error("expected PSMCT16 palette");

            auto pos=pn.name.rfind('_');
            uint32_t id=(uint32_t)std::stoul(pn.name.substr(pos+1));
            const uint8_t* raw=pal.payload(buf->off,buf->size);

            std::array<RGBA,256> physical{}, logical{};
            for(size_t i=0;i<256;i++) {
                uint16_t c=rd<uint16_t>(raw+i*2);
                physical[i]=RGBA{
                    exp5(c&31),
                    exp5((c>>5)&31),
                    exp5((c>>10)&31),
                    (uint8_t)((c&0x8000)?255:0)
                };
            }
            for(size_t i=0;i<256;i++)
                logical[csm1(i)]=physical[i];

            palmap[id]=logical;
        }

        size_t count=0;
        for(auto& tn:textures->children) {
            const Node* dat=child(tn,"texdat");
            if(!dat || dat->size<0xa0)
                throw std::runtime_error("bad texdat: "+tn.name);

            const uint8_t* raw=txr.payload(dat->off,dat->size);
            uint16_t w=rd<uint16_t>(raw+0);
            uint16_t h=rd<uint16_t>(raw+2);
            uint32_t bytes=rd<uint32_t>(raw+4);

            if(bytes!=(uint32_t)w*h)
                throw std::runtime_error("expected linear PSMT8: "+tn.name);
            if((uint64_t)0x20+bytes+0x80!=dat->size)
                throw std::runtime_error("unexpected texdat length: "+tn.name);

            uint64_t tex0=rd<uint64_t>(raw+0x20+bytes+0x40);
            Tex0 t=decode_tex0(tex0);

            if(t.psm!=0x13 || t.cpsm!=2 || t.csm!=0)
                throw std::runtime_error("unsupported GS texture format: "+tn.name);

            auto pit=palmap.find(t.cbp);
            if(pit==palmap.end())
                throw std::runtime_error("palette missing for "+tn.name);

            std::vector<RGBA> pixels(bytes);
            const uint8_t* idx=raw+0x20;
            for(size_t i=0;i<bytes;i++)
                pixels[i]=pit->second[idx[i]];

            fs::path name=fs::path(tn.name).filename();
            name.replace_extension(".tga");
            write_tga(out/name,w,h,pixels);

            std::cout<<tn.name<<" -> "<<(out/name)<<" "<<w<<"x"<<h
                     <<" palette="<<t.cbp<<"\n";
            ++count;
        }

        std::cout<<"Extracted "<<count<<" textures.\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"error: "<<e.what()<<"\n";
        return 2;
    }
}
