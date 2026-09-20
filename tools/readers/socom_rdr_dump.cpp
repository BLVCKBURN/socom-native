#include <algorithm>
#include <cstdint>
#include <cctype>
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

static uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
    if (o + 4 > b.size()) throw std::runtime_error("read outside buffer");
    uint32_t v{}; std::memcpy(&v, b.data() + o, 4); return v;
}
static size_t align_up(size_t v, size_t a) {
    if (!a) throw std::runtime_error("zero alignment");
    return (v + a - 1) / a * a;
}
static std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + p.string());
    f.seekg(0, std::ios::end); const auto n = f.tellg(); f.seekg(0);
    if (n < 0) throw std::runtime_error("cannot size: " + p.string());
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (!b.empty()) f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return b;
}
static std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for (unsigned char c : s) {
        switch (c) {
        case '"': o << "\\\""; break; case '\\': o << "\\\\"; break;
        case '\b': o << "\\b"; break; case '\f': o << "\\f"; break;
        case '\n': o << "\\n"; break; case '\r': o << "\\r"; break; case '\t': o << "\\t"; break;
        default:
            if (c < 0x20) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
            else o << char(c);
        }
    }
    return o.str();
}
static std::string hex32(uint32_t v) {
    std::ostringstream o; o << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << v; return o.str();
}

#pragma pack(push,1)
struct ZarHeader {
    uint32_t unknown00, nodeCount, stringBlockSize, preferredBase, alignment;
    uint8_t unknown14[0x40];
    uint32_t payloadSize, unknown58, unknown5C, version;
};
struct ZarNode { uint32_t nameAddress, dataOffset, dataSize, childCount; };
#pragma pack(pop)
static_assert(sizeof(ZarHeader) == 0x64);
static_assert(sizeof(ZarNode) == 0x10);

struct Leaf { std::string name; std::vector<uint8_t> bytes; };

class ZarReader {
public:
    explicit ZarReader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {
        if (bytes_.size() < sizeof(ZarHeader)) throw std::runtime_error("ZAR too small");
        std::memcpy(&h_, bytes_.data(), sizeof(h_));
        if (h_.version != 0x00020002) throw std::runtime_error("unexpected ZAR version " + hex32(h_.version));
        strOff_ = sizeof(ZarHeader);
        nodeOff_ = strOff_ + h_.stringBlockSize;
        const uint64_t nodeBytes = uint64_t(h_.nodeCount) * sizeof(ZarNode);
        if (nodeOff_ + nodeBytes > bytes_.size()) throw std::runtime_error("ZAR node table outside file");
        payloadOff_ = align_up(nodeOff_ + static_cast<size_t>(nodeBytes), h_.alignment);
        if (uint64_t(payloadOff_) + h_.payloadSize != bytes_.size()) throw std::runtime_error("ZAR payload does not end at EOF");
    }
    std::vector<Leaf> rdrLeaves() const {
        std::vector<Leaf> out;
        for (uint32_t i=0;i<h_.nodeCount;i++) {
            ZarNode n{}; std::memcpy(&n, bytes_.data()+nodeOff_+size_t(i)*sizeof(n), sizeof(n));
            if (!n.dataSize) continue;
            const auto name = resolveName(n.nameAddress);
            if (name.size() < 4 || name.substr(name.size()-4) != ".rdr") continue;
            if (uint64_t(n.dataOffset)+n.dataSize > h_.payloadSize) throw std::runtime_error("leaf outside ZAR payload: "+name);
            out.push_back({name, std::vector<uint8_t>(bytes_.begin()+payloadOff_+n.dataOffset,
                                                       bytes_.begin()+payloadOff_+n.dataOffset+n.dataSize)});
        }
        return out;
    }
private:
    std::string resolveName(uint32_t addr) const {
        if (!addr || addr < h_.preferredBase) return "<root>";
        uint64_t rel = uint64_t(addr)-h_.preferredBase;
        if (rel >= h_.stringBlockSize) return "<bad-name>";
        size_t p=strOff_+size_t(rel), lim=strOff_+h_.stringBlockSize, e=p;
        while (e<lim && bytes_[e]) ++e;
        if (e==lim) return "<unterminated-name>";
        return std::string(reinterpret_cast<const char*>(bytes_.data()+p), e-p);
    }
    std::vector<uint8_t> bytes_; ZarHeader h_{}; size_t strOff_{},nodeOff_{},payloadOff_{};
};

struct Stats {
    uint64_t lists=0, cells=0, tags[5]{}; uint64_t badStrings=0, badPointers=0, cycles=0; size_t maxDepth=0;
};

class RdrReader {
public:
    explicit RdrReader(const std::vector<uint8_t>& bytes) : b_(bytes) {
        if (b_.size() < 8) throw std::runtime_error("RDR payload too small");
        poolSize_=rd32(b_,0); dataOff_=rd32(b_,4);
        if (uint64_t(8)+poolSize_ > b_.size()) throw std::runtime_error("RDR string pool outside payload");
        if (dataOff_ > b_.size() || dataOff_+8 > b_.size()) throw std::runtime_error("RDR data offset outside payload");
        validateList(0,0);
    }
    uint32_t poolSize() const {return poolSize_;} uint32_t dataOff() const {return dataOff_;}
    const Stats& stats() const {return stats_;}
    void writeJson(std::ostream& os) const {
        os << "{\n  \"pool_size\": " << poolSize_ << ",\n  \"data_offset\": " << dataOff_ << ",\n";
        os << "  \"stats\": {\"lists\": "<<stats_.lists<<", \"cells\": "<<stats_.cells
           <<", \"max_depth\": "<<stats_.maxDepth<<", \"bad_strings\": "<<stats_.badStrings
           <<", \"bad_pointers\": "<<stats_.badPointers<<", \"cycles\": "<<stats_.cycles<<"},\n";
        os << "  \"root\": "; std::set<uint32_t> active; emitList(os,0,2,active); os << "\n}\n";
    }
private:
    uint32_t rel32(uint32_t rel) const {
        uint64_t o=uint64_t(dataOff_)+rel; if (o+4>b_.size()) throw std::runtime_error("RDR relative read outside payload");
        return rd32(b_,static_cast<size_t>(o));
    }
    bool stringAt(uint32_t off,std::string& out) const {
        if (off>=poolSize_) return false;
        size_t s=8+off, lim=8+poolSize_, e=s; while(e<lim && b_[e]) ++e; if(e==lim) return false;
        out.assign(reinterpret_cast<const char*>(b_.data()+s),e-s); return true;
    }
    void validateList(uint32_t rel,size_t depth) {
        if (visited_.count(rel)) return;
        if (uint64_t(dataOff_)+rel+8>b_.size()) { stats_.badPointers++; return; }
        uint32_t tag=rel32(rel), count=rel32(rel+4);
        if(tag!=1) throw std::runtime_error("RDR list at "+hex32(rel)+" does not begin with tag 1");
        if(count<1) throw std::runtime_error("RDR zero-length list at "+hex32(rel));
        uint64_t end=uint64_t(dataOff_)+rel+uint64_t(count)*8;
        if(end>b_.size()) throw std::runtime_error("RDR list overruns payload at "+hex32(rel));
        visited_.insert(rel); stats_.lists++; stats_.cells+=count; stats_.tags[1]++; stats_.maxDepth=std::max(stats_.maxDepth,depth);
        for(uint32_t i=1;i<count;i++) {
            uint32_t p=rel+i*8,t=rel32(p),v=rel32(p+4);
            if(t>=1 && t<=4) stats_.tags[t]++;
            else throw std::runtime_error("unknown RDR tag "+std::to_string(t)+" at "+hex32(p));
            if(t==3){std::string x;if(!stringAt(v,x))stats_.badStrings++;}
            if(t==4){
                if(uint64_t(dataOff_)+v+8>b_.size()){stats_.badPointers++;continue;}
                if(activeValidate_.count(v)){stats_.cycles++;continue;}
                activeValidate_.insert(v);validateList(v,depth+1);activeValidate_.erase(v);
            }
        }
    }
    void indent(std::ostream& os,int n) const { for(int i=0;i<n;i++)os.put(' '); }
    void emitCell(std::ostream& os,uint32_t p,int ind,std::set<uint32_t>& active) const {
        uint32_t t=rel32(p),v=rel32(p+4);
        os << "{\"offset\": \""<<hex32(p)<<"\", \"tag\": "<<t<<", ";
        if(t==1){os<<"\"type\": \"int32\", \"value\": "<<static_cast<int32_t>(v);}
        else if(t==2){float f;std::memcpy(&f,&v,4);os<<"\"type\": \"float32\", \"value\": "<<std::setprecision(9)<<f;}
        else if(t==3){std::string s;os<<"\"type\": \"string\", \"raw\": \""<<hex32(v)<<"\"";if(stringAt(v,s))os<<", \"valid\": true, \"value\": \""<<json_escape(s)<<"\"";else os<<", \"valid\": false";}
        else if(t==4){
            os<<"\"type\": \"pointer\", \"target_offset\": \""<<hex32(v)<<"\"";
            if(uint64_t(dataOff_)+v+8>b_.size()) os<<", \"valid\": false";
            else if(active.count(v)) os<<", \"valid\": true, \"cycle\": true";
            else {os<<", \"valid\": true, \"target\": "; emitList(os,v,ind,active);}
        }
        os<<"}";
    }
    void emitList(std::ostream& os,uint32_t rel,int ind,std::set<uint32_t>& active) const {
        if(uint64_t(dataOff_)+rel+8>b_.size()){os<<"{\"invalid_pointer\": \""<<hex32(rel)<<"\"}";return;}
        uint32_t tag=rel32(rel),count=rel32(rel+4);
        if(tag!=1 || count<1 || uint64_t(dataOff_)+rel+uint64_t(count)*8>b_.size()){os<<"{\"invalid_list\": \""<<hex32(rel)<<"\"}";return;}
        active.insert(rel);
        os<<"{\n";indent(os,ind+2);os<<"\"offset\": \""<<hex32(rel)<<"\",\n";indent(os,ind+2);os<<"\"cell_count\": "<<count<<",\n";indent(os,ind+2);os<<"\"values\": [";
        if(count>1)os<<"\n";
        for(uint32_t i=1;i<count;i++){
            indent(os,ind+4);emitCell(os,rel+i*8,ind+4,active);if(i+1<count)os<<",";os<<"\n";
        }
        if (count > 1) indent(os, ind + 2);
        os << "]\n";
        indent(os, ind);
        os << "}";
        active.erase(rel);
    }
    const std::vector<uint8_t>& b_; uint32_t poolSize_{},dataOff_{}; mutable Stats stats_{}; std::set<uint32_t> visited_,activeValidate_;
};

static std::string safe_name(std::string s){for(char&c:s)if(!(std::isalnum((unsigned char)c)||c=='.'||c=='_'||c=='-'))c='_';return s;}
static void dump_one(const std::string& label,const std::vector<uint8_t>& bytes,const fs::path& out){
    RdrReader r(bytes); std::ofstream f(out); if(!f)throw std::runtime_error("cannot create: "+out.string()); r.writeJson(f);
    std::cout<<label<<": lists="<<r.stats().lists<<" depth="<<r.stats().maxDepth<<" bad_strings="<<r.stats().badStrings<<" -> "<<out.string()<<"\n";
}
static void usage(){
    std::cerr<<"Usage:\n  socom_rdr_dump <archive.zar> --list\n  socom_rdr_dump <archive.zar> <leaf.rdr> [output.json]\n  socom_rdr_dump <archive.zar> --all <output_dir>\n  socom_rdr_dump <file.rdr> [output.json]\n";
}
int main(int argc,char**argv){
 try{
    if(argc<2){usage();return 2;} fs::path in=argv[1];auto bytes=read_file(in);
    if(in.extension()==".zar"){
        ZarReader z(std::move(bytes));auto leaves=z.rdrLeaves();
        if(argc>=3 && std::string(argv[2])=="--list"){for(auto&l:leaves)std::cout<<l.name<<"\n";return 0;}
        if(argc>=3 && std::string(argv[2])=="--all"){
            if(argc<4){usage();return 2;}fs::path od=argv[3];fs::create_directories(od);uint64_t bad=0;
            for(auto&l:leaves){RdrReader r(l.bytes);bad+=r.stats().badStrings;fs::path o=od/(safe_name(l.name)+".json");std::ofstream f(o);r.writeJson(f);std::cout<<l.name<<": lists="<<r.stats().lists<<" depth="<<r.stats().maxDepth<<" bad_strings="<<r.stats().badStrings<<"\n";}
            std::cout<<"validated "<<leaves.size()<<" RDR leaves; bad string cells="<<bad<<"\n";return 0;
        }
        if(argc<3){usage();return 2;}std::string wanted=argv[2];auto it=std::find_if(leaves.begin(),leaves.end(),[&](const Leaf&l){return l.name==wanted;});if(it==leaves.end())throw std::runtime_error("RDR leaf not found: "+wanted);
        fs::path out=(argc>=4)?fs::path(argv[3]):fs::path(wanted+".json");dump_one(wanted,it->bytes,out);return 0;
    }
    fs::path out=(argc>=3)?fs::path(argv[2]):fs::path(in.filename().string()+".json");dump_one(in.string(),bytes,out);return 0;
 }catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<"\n";return 1;}
}
