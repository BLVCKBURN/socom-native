#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static std::uint16_t U16(const std::uint8_t* p) { return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
static std::uint32_t U32(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
static std::int32_t S16(std::uint16_t x) { return (x & 0x8000u) ? std::int32_t(x) - 0x10000 : std::int32_t(x); }

// Compact SHA-256 implementation used only to identify the retail executable.
struct Sha256 {
    std::array<std::uint32_t,8> h{{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u}};
    std::array<std::uint8_t,64> block{}; std::uint64_t bits=0; std::size_t used=0;
    static std::uint32_t rr(std::uint32_t x,int n){return (x>>n)|(x<<(32-n));}
    void compress(const std::uint8_t* p){
        static constexpr std::uint32_t k[64]={
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        std::uint32_t w[64];
        for(int i=0;i<16;i++) w[i]=(std::uint32_t(p[i*4])<<24)|(std::uint32_t(p[i*4+1])<<16)|(std::uint32_t(p[i*4+2])<<8)|p[i*4+3];
        for(int i=16;i<64;i++){auto s0=rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3);auto s1=rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int i=0;i<64;i++){auto S1=rr(e,6)^rr(e,11)^rr(e,25);auto ch=(e&f)^((~e)&g);auto t1=hh+S1+ch+k[i]+w[i];auto S0=rr(a,2)^rr(a,13)^rr(a,22);auto maj=(a&b)^(a&c)^(b&c);auto t2=S0+maj;hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    void add(const std::uint8_t* p,std::size_t n){bits+=std::uint64_t(n)*8;while(n){auto take=std::min(n,64-used);std::memcpy(block.data()+used,p,take);used+=take;p+=take;n-=take;if(used==64){compress(block.data());used=0;}}}
    std::string finish(){auto totalBits=bits;block[used++]=0x80;if(used>56){while(used<64)block[used++]=0;compress(block.data());used=0;}while(used<56)block[used++]=0;for(int i=7;i>=0;i--)block[used++]=std::uint8_t(totalBits>>(i*8));compress(block.data());std::ostringstream o;o<<std::hex<<std::setfill('0');for(auto x:h)o<<std::setw(8)<<x;return o.str();}
};

struct LoadSegment { std::uint32_t off{}, va{}, filesz{}, memsz{}, flags{}; };
struct CString { std::uint32_t va{}; std::string text; };

class ElfImage {
public:
    std::vector<std::uint8_t> b; std::uint32_t entry{}; LoadSegment load{};
    explicit ElfImage(const fs::path& p){
        std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("cannot open ELF: "+p.string());
        f.seekg(0,std::ios::end); auto n=f.tellg(); f.seekg(0); b.resize(std::size_t(n)); f.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()));
        if(b.size()<52 || std::memcmp(b.data(),"\x7f" "ELF",4)!=0 || b[4]!=1 || b[5]!=1) throw std::runtime_error("expected ELF32 little-endian image");
        entry=U32(&b[24]); auto phoff=U32(&b[28]); auto phentsz=U16(&b[42]); auto phnum=U16(&b[44]);
        bool got=false; for(std::uint16_t i=0;i<phnum;i++){auto p0=phoff+std::uint32_t(i)*phentsz;if(p0+32>b.size())throw std::runtime_error("program header outside file");auto type=U32(&b[p0]);if(type==1){LoadSegment s{U32(&b[p0+4]),U32(&b[p0+8]),U32(&b[p0+16]),U32(&b[p0+20]),U32(&b[p0+24])};if(!got || s.filesz>load.filesz){load=s;got=true;}}}
        if(!got) throw std::runtime_error("no PT_LOAD segment");
        if(std::uint64_t(load.off)+load.filesz>b.size()) throw std::runtime_error("load segment outside file");
    }
    bool mapped(std::uint32_t va,std::size_t n=1) const { return va>=load.va && std::uint64_t(va-load.va)+n<=load.filesz; }
    std::size_t off(std::uint32_t va) const { if(!mapped(va))throw std::runtime_error("VA outside file mapping"); return load.off+(va-load.va); }
    std::uint32_t word(std::uint32_t va) const { auto o=off(va); if(o+4>b.size())throw std::runtime_error("word outside file"); return U32(&b[o]); }
    std::string sha256() const { Sha256 s;s.add(b.data(),b.size());return s.finish(); }
};

static bool printable(unsigned char c){return c>=32 && c<127;}
static std::string csvq(const std::string& s){ if(s.find_first_of(",\"\r\n")==std::string::npos)return s;std::string o="\"";for(char c:s){if(c=='\"')o+="\"\"";else o+=c;}return o+="\""; }
static std::string hx(std::uint32_t v){std::ostringstream o;o<<"0x"<<std::uppercase<<std::hex<<std::setw(8)<<std::setfill('0')<<v;return o.str();}
static bool sourceName(const std::string& s){ static const std::regex r(R"((?:.*[\\/])?[A-Za-z0-9_+.-]+\.(?:cpp|c|h))",std::regex::icase);return std::regex_match(s,r);}

int main(int argc,char** argv){
 try{
    if(argc<2||argc>3){std::cerr<<"Usage: socom_ee_analyze <SCUS_971.34> [output_dir]\n";return 1;}
    fs::path in=argv[1], out=argc==3?fs::path(argv[2]):fs::path("generated/ee/scus_971_34"); fs::create_directories(out);
    ElfImage e(in); const auto loadEnd=e.load.va+e.load.filesz; const auto codeEnd=std::min<std::uint32_t>(0x00440000u,loadEnd);
    std::vector<CString> strings; std::unordered_map<std::uint32_t,std::string> strByVa;
    for(std::size_t i=e.load.off,end=e.load.off+e.load.filesz;i<end;){std::size_t j=i;while(j<end&&printable(e.b[j]))++j;if(j-i>=4&&j<end&&e.b[j]==0){auto va=e.load.va+std::uint32_t(i-e.load.off);std::string s(reinterpret_cast<const char*>(&e.b[i]),j-i);strings.push_back({va,s});strByVa.emplace(va,s);i=j+1;}else ++i;}

    std::map<std::uint32_t,std::vector<std::uint32_t>> calls; std::set<std::uint32_t> starts; std::array<std::uint64_t,64> opc{};
    for(std::uint32_t pc=e.load.va;pc+4<=codeEnd;pc+=4){auto w=e.word(pc);auto op=w>>26;opc[op]++;if(op==3){auto tgt=((pc+4)&0xF0000000u)|((w&0x03FFFFFFu)<<2);if(tgt>=e.load.va&&tgt<codeEnd){calls[tgt].push_back(pc);starts.insert(tgt);}}
       if(op==9&&((w>>21)&31)==29&&((w>>16)&31)==29&&(w&0x8000u)){bool ra=false;for(int j=1;j<=5&&pc+4*j<codeEnd;j++){auto q=e.word(pc+4*j);if((q>>26)==0x3Fu&&((q>>21)&31)==29&&((q>>16)&31)==31){ra=true;break;}}if(ra)starts.insert(pc);} }
    std::vector<std::uint32_t> startv(starts.begin(),starts.end());
    auto funcFor=[&](std::uint32_t pc)->std::uint32_t{auto it=std::upper_bound(startv.begin(),startv.end(),pc);return it==startv.begin()?0:*std::prev(it);};

    struct Xref{std::uint32_t hi{},lo{},sva{},fn{};std::string s;}; std::vector<Xref> xrefs;
    for(std::uint32_t pc=e.load.va;pc+4<=codeEnd;pc+=4){auto w=e.word(pc);if((w>>26)!=0x0F)continue;auto rt=(w>>16)&31;auto imm=w&0xFFFFu;for(int j=1;j<=12&&pc+4*j<codeEnd;j++){auto pc2=pc+4*j,q=e.word(pc2),op2=q>>26,rs2=(q>>21)&31,rt2=(q>>16)&31,im2=q&0xFFFFu;std::uint32_t val=0;bool have=false;if(rt2==rt&&rs2==rt&&op2==9){val=(imm<<16)+std::uint32_t(S16(std::uint16_t(im2)));have=true;}else if(rt2==rt&&rs2==rt&&op2==0x0D){val=(imm<<16)|im2;have=true;}if(have){auto it=strByVa.find(val);if(it!=strByVa.end())xrefs.push_back({pc,pc2,val,funcFor(pc),it->second});break;}}}

    struct Mod{std::vector<std::uint32_t>x;std::set<std::uint32_t>f;};std::map<std::string,Mod> mods;
    for(const auto& x:xrefs) if(sourceName(x.s)){auto&m=mods[x.s];m.x.push_back(x.hi);if(x.fn)m.f.insert(x.fn);}

    // UI command table is self-locating: pointer to the unique "SetMission" string marks word 2 of record 1.
    struct Cmd{std::uint32_t id{},nameva{},fn{},rec{};std::string name;};std::vector<Cmd> cmds;
    std::uint32_t smva=0;for(auto&s:strings)if(s.text=="SetMission"){smva=s.va;break;}
    if(smva){std::uint32_t ptrva=0;for(std::uint32_t va=e.load.va;va+4<=loadEnd;va+=4)if(e.word(va)==smva){ptrva=va;break;}if(ptrva>=e.load.va+4){auto rec=ptrva-4;for(;rec+12<=loadEnd;rec+=12){auto id=e.word(rec),nv=e.word(rec+4),fn=e.word(rec+8);auto it=strByVa.find(nv);if(id==0||id>10000||it==strByVa.end()||fn<e.load.va||fn>=codeEnd)break;cmds.push_back({id,nv,fn,rec,it->second});}}}

    std::ofstream meta(out/"binary_metadata.txt");auto sha=e.sha256();meta<<"file="<<in.filename().string()<<"\nsize="<<e.b.size()<<"\nsha256="<<sha<<"\nentry="<<hx(e.entry)<<"\nload_base="<<hx(e.load.va)<<"\nfile_load_size="<<hx(e.load.filesz)<<"\nmemory_load_size="<<hx(e.load.memsz)<<"\nui_command_records="<<cmds.size()<<"\ndirect_jal_targets="<<calls.size()<<"\ndirect_jal_calls=";std::size_t total=0;for(auto&x:calls)total+=x.second.size();meta<<total<<"\nsource_modules="<<mods.size()<<"\n";
    if(sha!="5111e776f64611ae1dcdc725f0ce9db660e90e1763e897e8f5b90f7402ce30d1")meta<<"warning=SHA256 does not match the analyzed SCUS_971.34 retail executable\n";

    std::ofstream cf(out/"direct_calls.csv");cf<<"target,caller_count,callers\n";for(auto&[t,src]:calls){cf<<hx(t)<<','<<src.size()<<',';for(std::size_t i=0;i<src.size();i++){if(i)cf<<';';cf<<hx(src[i]);}cf<<'\n';}
    std::ofstream xf(out/"string_xrefs.csv");xf<<"load_hi,load_lo,string_va,function,string\n";for(auto&x:xrefs)xf<<hx(x.hi)<<','<<hx(x.lo)<<','<<hx(x.sva)<<','<<(x.fn?hx(x.fn):"")<<','<<csvq(x.s)<<'\n';
    std::ofstream mf(out/"source_module_anchors.csv");mf<<"source_file,xref_count,function_count,first_xref,first_function,functions\n";for(auto&[name,m]:mods){std::sort(m.x.begin(),m.x.end());mf<<csvq(name)<<','<<m.x.size()<<','<<m.f.size()<<','<<(m.x.empty()?"":hx(m.x.front()))<<','<<(m.f.empty()?"":hx(*m.f.begin()))<<',';bool first=true;for(auto f:m.f){if(!first)mf<<';';mf<<hx(f);first=false;}mf<<'\n';}
    std::ofstream uf(out/"ui_command_dispatch.csv");uf<<"command_id,command_name,handler_va,record_va\n";for(auto&c:cmds)uf<<c.id<<','<<csvq(c.name)<<','<<hx(c.fn)<<','<<hx(c.rec)<<'\n';
    if(sha=="5111e776f64611ae1dcdc725f0ce9db660e90e1763e897e8f5b90f7402ce30d1") {
        struct A { const char* name; const char* desc; std::uint32_t va; const char* module; };
        static const A a[] = {
            {"elf_entry","ELF entry point",0x00140008,""},
            {"app_main_loop","top-level application initialization/update loop",0x00141630,"main.cpp"},
            {"ui_SetMission","UI command handler: SetMission",0x001D61A0,""},
            {"ui_SwitchMenu","UI command handler: SwitchMenu",0x001D5DB0,""},
            {"ui_ClearGameState","UI command handler: ClearGameState",0x001D4740,""},
            {"ui_SetMenuState","UI command handler: SetMenuState",0x001D49B0,""},
            {"ui_RebootIOP","UI command handler: RebootIOP",0x001D1740,""},
            {"mission_frame","actual per-frame mission routine",0x001F8EB0,"fts_mission.cpp"},
            {"mission_tick_wrapper","registered mission tick wrapper",0x001F94A0,"fts_mission.cpp"},
            {"mission_construct_init","CMission construction/runtime initialization",0x001FA610,"fts_mission.cpp"},
            {"mission_load_rdr","mission.rdr / UiVars / Valves loader",0x001FAE10,"fts_mission.cpp"},
            {"diTick","DI scheduler callback",0x00231DC0,""},
            {"visual_effects_tick","Update visual effects callback",0x00228C70,""},
            {"UnitTick","core scheduler callback",0x002E1A20,""},
            {"ai_pre_tick","core scheduler callback",0x002C4910,""},
            {"entity_pre_tick","core scheduler callback",0x001F08F0,""},
            {"weapon_pre_tick","core scheduler callback",0x003390E0,"zwep_weapon.cpp"},
            {"CAiEventTick","core scheduler callback",0x0017E900,""},
            {"entity_tick","core scheduler callback",0x001F07D0,""},
            {"ai_direg_tick","core scheduler callback",0x002C4690,""},
            {"entity_post_tick","core scheduler callback",0x001F05C0,""},
            {"weapon_post_tick","core scheduler callback",0x003390A0,"zwep_weapon.cpp"},
            {"ai_route_planner","core scheduler callback",0x0017FDB0,""},
            {"ai_post_tick","core scheduler callback",0x002C4340,""},
            {"sound_post_tick","core scheduler callback",0x00302DF0,""}
        };
        std::ofstream af(out/"known_retail_anchors.csv"); af<<"symbol,description,va,source_module\n";
        for(const auto& x:a) af<<x.name<<','<<csvq(x.desc)<<','<<hx(x.va)<<','<<x.module<<'\n';
    }
    static const char* opnames[64]={"SPECIAL","REGIMM","J","JAL","BEQ","BNE","BLEZ","BGTZ","ADDI","ADDIU","SLTI","SLTIU","ANDI","ORI","XORI","LUI","COP0","COP1","COP2","OP13","BEQL","BNEL","BLEZL","BGTZL","DADDI","DADDIU","LDL","LDR","MMI","OP1D","LQ","SQ","LB","LH","LWL","LW","LBU","LHU","LWR","LWU","SB","SH","SWL","SW","SDL","SDR","SWR","CACHE","OP30","LWC1","OP32","OP33","OP34","LQC2","LD","OP37","OP38","SWC1","OP3A","OP3B","OP3C","OP3D","SQC2","SD"};
    std::ofstream of(out/"instruction_primary_opcodes.csv");of<<"opcode,name,count\n";for(int i=0;i<64;i++)if(opc[i])of<<"0x"<<std::hex<<std::uppercase<<std::setw(2)<<std::setfill('0')<<i<<std::dec<<','<<opnames[i]<<','<<opc[i]<<'\n';

    std::cout<<"SOCOM EE analysis complete\n"<<"  SHA256: "<<sha<<"\n"<<"  entry: "<<hx(e.entry)<<"\n"<<"  direct JAL targets: "<<calls.size()<<"\n"<<"  direct JAL calls: "<<total<<"\n"<<"  source modules anchored: "<<mods.size()<<"\n"<<"  UI command records: "<<cmds.size()<<"\n"<<"  output: "<<fs::absolute(out).string()<<"\n";
    return 0;
 }catch(const std::exception&ex){std::cerr<<"error: "<<ex.what()<<"\n";return 2;}
}
