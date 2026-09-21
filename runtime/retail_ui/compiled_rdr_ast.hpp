#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace socom::retail_menu {

struct RdrNode;
using RdrValue = std::variant<std::int32_t,float,std::string,RdrNode>;

struct RdrNode {
    std::vector<RdrValue> values;
};

class CompiledRdr {
public:
    explicit CompiledRdr(std::vector<std::uint8_t> bytes):bytes_(std::move(bytes)){
        if(bytes_.size()<8) throw std::runtime_error("RDR too small");
        std::memcpy(&poolSize_,bytes_.data(),4);
        std::memcpy(&dataOff_,bytes_.data()+4,4);
        if(8ull+poolSize_>bytes_.size() || dataOff_>bytes_.size())
            throw std::runtime_error("invalid compiled RDR header");
    }

    RdrNode ParseRoot() const { return ParseList(0,0); }

private:
    std::uint32_t U32(std::uint32_t rel) const {
        if(static_cast<std::uint64_t>(dataOff_)+rel+4>bytes_.size())
            throw std::runtime_error("RDR read outside data");
        std::uint32_t v{}; std::memcpy(&v,bytes_.data()+dataOff_+rel,4); return v;
    }
    std::string Str(std::uint32_t off) const {
        if(off>=poolSize_) throw std::runtime_error("RDR string offset outside pool");
        std::size_t p=8+off,e=p,lim=8+poolSize_;
        while(e<lim && bytes_[e])++e;
        if(e==lim)throw std::runtime_error("unterminated RDR string");
        return std::string(reinterpret_cast<const char*>(bytes_.data()+p),e-p);
    }
    RdrNode ParseList(std::uint32_t rel,int depth) const {
        if(depth>64) throw std::runtime_error("RDR recursion too deep");
        const auto type=U32(rel), count=U32(rel+4);
        if(type!=1) throw std::runtime_error("expected RDR list");
        RdrNode n;
        for(std::uint32_t i=1;i<count;++i){
            const auto t=U32(rel+i*8);
            const auto v=U32(rel+i*8+4);
            switch(t){
            case 1:n.values.emplace_back(static_cast<std::int32_t>(v));break;
            case 2:{float f{};std::memcpy(&f,&v,4);n.values.emplace_back(f);break;}
            case 3:n.values.emplace_back(Str(v));break;
            case 4:n.values.emplace_back(ParseList(v,depth+1));break;
            default:throw std::runtime_error("unknown RDR atom type");
            }
        }
        return n;
    }
    std::vector<std::uint8_t> bytes_;
    std::uint32_t poolSize_{},dataOff_{};
};

inline const std::string* AsString(const RdrValue&v){return std::get_if<std::string>(&v);}
inline const RdrNode* AsNode(const RdrValue&v){return std::get_if<RdrNode>(&v);}
inline const std::int32_t* AsInt(const RdrValue&v){return std::get_if<std::int32_t>(&v);}
inline const float* AsFloat(const RdrValue&v){return std::get_if<float>(&v);}

inline const RdrNode* FindPairNode(const RdrNode&n,const std::string&key){
    for(std::size_t i=0;i+1<n.values.size();++i){
        auto s=AsString(n.values[i]);
        if(s && *s==key) return AsNode(n.values[i+1]);
    }
    return nullptr;
}
inline std::string FirstString(const RdrNode&n){
    for(const auto&v:n.values) if(auto s=AsString(v))return *s;
    return {};
}
inline int FirstInt(const RdrNode&n,int def=0){
    for(const auto&v:n.values)if(auto i=AsInt(v))return *i;
    return def;
}
inline float FirstNumber(const RdrNode&n,float def=0){
    for(const auto&v:n.values){
        if(auto f=AsFloat(v))return *f;
        if(auto i=AsInt(v))return static_cast<float>(*i);
    }
    return def;
}

} // namespace
