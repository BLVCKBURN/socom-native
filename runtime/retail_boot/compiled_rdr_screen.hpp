#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace socom::retail_boot {

class CompiledRdrStrings {
public:
    explicit CompiledRdrStrings(std::vector<std::uint8_t> bytes)
        : bytes_(std::move(bytes)) {
        if (bytes_.size() < 8)
            throw std::runtime_error("compiled RDR too small");
        std::memcpy(&poolSize_, bytes_.data(), 4);
        std::memcpy(&dataOffset_, bytes_.data()+4, 4);
        if (8ull + poolSize_ > bytes_.size())
            throw std::runtime_error("bad RDR string pool size");
        if (dataOffset_ > bytes_.size())
            throw std::runtime_error("bad RDR data offset");

        const std::size_t begin=8;
        const std::size_t end=begin+poolSize_;
        std::size_t p=begin;
        while(p<end){
            std::size_t e=p;
            while(e<end && bytes_[e]) ++e;
            if(e>p)
                strings_.emplace_back(reinterpret_cast<const char*>(bytes_.data()+p),e-p);
            p=e+1;
        }
    }

    bool Has(const std::string& s) const {
        for(const auto& x:strings_) if(x==s) return true;
        return false;
    }

    const std::vector<std::string>& Strings() const { return strings_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t poolSize_{};
    std::uint32_t dataOffset_{};
    std::vector<std::string> strings_;
};

struct ScreenDef {
    std::string name;
    std::string backgroundType;
    std::string backgroundFile;
    std::string onMpegEndTarget;
    std::string onStartSwitchTarget;
    std::vector<std::string> menuEntries;
};

inline std::string NextAfter(const std::vector<std::string>& s,const std::string& key,std::size_t start=0){
    for(std::size_t i=start;i+1<s.size();++i)
        if(s[i]==key) return s[i+1];
    return {};
}

inline ScreenDef ParseScreenStrings(
    const std::string& leafName,
    const CompiledRdrStrings& rdr)
{
    ScreenDef out;
    out.name=leafName;
    const auto& s=rdr.Strings();

    for(std::size_t i=0;i+1<s.size();++i){
        if(s[i]=="BACKGROUND"){
            for(std::size_t j=i+1;j+1<s.size() && j<i+12;++j){
                if(s[j]=="TYPE") out.backgroundType=s[j+1];
                if(s[j]=="FILENAME") out.backgroundFile=s[j+1];
            }
        }
    }

    // Retail intro cinematic: ONMPEGEND -> switch_menu -> ARGUMENT -> target.
    for(std::size_t i=0;i<s.size();++i){
        if(s[i]!="ONMPEGEND") continue;
        for(std::size_t j=i+1;j<s.size() && j<i+20;++j){
            if((s[j]=="ARGUMENT" || s[j]=="NAME") && j+1<s.size()){
                if(s[j+1].find(".rdr")!=std::string::npos){
                    out.onMpegEndTarget=s[j+1];
                    break;
                }
            }
        }
    }

    // Intro screen: sequence contains SWITCHMENU / ARGUMENT / dlgIntroCinematic.rdr.
    if(leafName=="dlgIntroScreen.rdr"){
        for(std::size_t i=0;i+2<s.size();++i){
            if(s[i]=="SWITCHMENU" && s[i+1]=="ARGUMENT"){
                out.onStartSwitchTarget=s[i+2];
                break;
            }
        }
    }

    if(leafName=="dlgMenu.rdr"){
        for(const char* label:{"NEW GAME","LOAD GAME","ONLINE","OPTIONS"}){
            for(const auto& x:s){
                if(x==label){out.menuEntries.emplace_back(label);break;}
            }
        }
    }

    return out;
}

} // namespace socom::retail_boot
