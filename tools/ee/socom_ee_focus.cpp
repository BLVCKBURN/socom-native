#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
static std::uint16_t U16(const std::uint8_t* p){return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);}
static std::uint32_t U32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
static std::int32_t S16(std::uint16_t x){return (x&0x8000u)?std::int32_t(x)-0x10000:std::int32_t(x);}
static std::string H(std::uint32_t v){std::ostringstream o;o<<"0x"<<std::uppercase<<std::hex<<std::setw(8)<<std::setfill('0')<<v;return o.str();}
static std::string Csv(const std::string& s){if(s.find_first_of(",\"\r\n")==std::string::npos)return s;std::string o="\"";for(char c:s){if(c=='\"')o+="\"\"";else o+=c;}o+='\"';return o;}
static bool Printable(unsigned char c){return c>=32&&c<127;}

struct Seg{std::uint32_t off{},va{},filesz{},memsz{};};
class Elf {
public:
 std::vector<std::uint8_t>b; std::uint32_t entry{}; Seg s{};
 explicit Elf(const fs::path&p){
  std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot open ELF");
  f.seekg(0,std::ios::end);auto n=f.tellg();f.seekg(0);b.resize((std::size_t)n);f.read((char*)b.data(),(std::streamsize)b.size());
  if(b.size()<52||std::memcmp(b.data(),"\x7f" "ELF",4)||b[4]!=1||b[5]!=1)throw std::runtime_error("expected ELF32 little-endian");
  entry=U32(&b[24]);auto phoff=U32(&b[28]);auto psz=U16(&b[42]);auto pn=U16(&b[44]);bool got=false;
  for(unsigned i=0;i<pn;i++){auto o=phoff+i*psz;if(U32(&b[o])!=1)continue;Seg q{U32(&b[o+4]),U32(&b[o+8]),U32(&b[o+16]),U32(&b[o+20])};if(!got||q.filesz>s.filesz){s=q;got=true;}}
  if(!got)throw std::runtime_error("no PT_LOAD");
 }
 bool mapped(std::uint32_t va,std::size_t n=1)const{return va>=s.va&&std::uint64_t(va-s.va)+n<=s.filesz;}
 std::uint32_t word(std::uint32_t va)const{if(!mapped(va,4))throw std::runtime_error("VA outside file segment");return U32(&b[s.off+(va-s.va)]);}
};
struct Str{std::uint32_t va{};std::string text;};
struct Xref{std::uint32_t pc{},sva{};std::string text;};
struct Focus{const char* name;std::uint32_t va;const char* note;};

int main(int argc,char**argv){
 try{
  if(argc!=3){std::cerr<<"Usage: socom_ee_focus <SCUS_971.34> <output_dir>\n";return 1;}
  Elf e(argv[1]);fs::path out=argv[2];fs::create_directories(out);
  if(e.entry!=0x00140008u||e.s.va!=0x00100000u||e.s.filesz!=0x0038D200u)throw std::runtime_error("input does not match expected SCUS_971.34 layout");
  const std::uint32_t codeEnd=std::min<std::uint32_t>(0x00440000u,e.s.va+e.s.filesz);

  std::vector<Str> strings;std::unordered_map<std::uint32_t,std::string> byva;
  for(std::size_t i=e.s.off,end=e.s.off+e.s.filesz;i<end;){std::size_t j=i;while(j<end&&Printable(e.b[j]))++j;if(j-i>=4&&j<end&&e.b[j]==0){auto va=e.s.va+std::uint32_t(i-e.s.off);std::string s((const char*)&e.b[i],j-i);strings.push_back({va,s});byva.emplace(va,s);i=j+1;}else ++i;}

  std::map<std::uint32_t,std::vector<std::uint32_t>> incoming;std::set<std::uint32_t> starts;
  for(std::uint32_t pc=e.s.va;pc+4<=codeEnd;pc+=4){auto w=e.word(pc),op=w>>26;if(op==3){auto t=((pc+4)&0xF0000000u)|((w&0x03ffffffu)<<2);if(t>=e.s.va&&t<codeEnd){incoming[t].push_back(pc);starts.insert(t);}}
   if(op==9&&((w>>21)&31)==29&&((w>>16)&31)==29&&(w&0x8000u)){bool ra=false;for(int j=1;j<=5&&pc+4*j<codeEnd;j++){auto q=e.word(pc+4*j);if((q>>26)==0x3f&&((q>>21)&31)==29&&((q>>16)&31)==31){ra=true;break;}}if(ra)starts.insert(pc);}}

  static const Focus focus[]={
   {"elf_entry",0x00140008u,"ELF entry"},
   {"app_main_loop",0x00141630u,"top-level application init/update loop"},
   {"ui_RebootIOP",0x001D1740u,"UI command handler"},
   {"ui_ClearGameState",0x001D4740u,"UI command handler"},
   {"ui_SetMenuState",0x001D49B0u,"UI command handler"},
   {"ui_SwitchMenu",0x001D5DB0u,"UI command handler"},
   {"ui_SetMission",0x001D61A0u,"UI command handler"},
   {"mission_frame",0x001F8EB0u,"actual per-frame mission routine"},
   {"mission_tick_wrapper",0x001F94A0u,"thin wrapper calling mission_frame"},
   {"mission_activate",0x001FA610u,"Heavy selected-mission activation/load path"},
   {"mission_load_descriptor",0x001FAE10u,"mission.rdr / UiVars / Valves configuration loader"},
   {"mission_construct_global",0x001FB4F0u,"One-time singleton construction/init called during application startup"}
  };
  for(const auto&f:focus)starts.insert(f.va);
  std::vector<std::uint32_t> sv(starts.begin(),starts.end());

  std::vector<Xref>xrefs;
  for(std::uint32_t pc=e.s.va;pc+4<=codeEnd;pc+=4){auto w=e.word(pc);if((w>>26)!=0x0f)continue;auto rt=(w>>16)&31,hi=w&0xffffu;for(int j=1;j<=12&&pc+4*j<codeEnd;j++){auto q=e.word(pc+4*j),op=q>>26,rs=(q>>21)&31,rt2=(q>>16)&31,lo=q&0xffffu;std::uint32_t va=0;bool ok=false;if(rt2==rt&&rs==rt&&op==9){va=(hi<<16)+std::uint32_t(S16((std::uint16_t)lo));ok=true;}else if(rt2==rt&&rs==rt&&op==0x0d){va=(hi<<16)|lo;ok=true;}if(ok){auto it=byva.find(va);if(it!=byva.end())xrefs.push_back({pc,va,it->second});break;}}}

  auto endFor=[&](std::uint32_t a){auto it=std::upper_bound(sv.begin(),sv.end(),a);return it==sv.end()?codeEnd:*it;};
  std::ofstream csv(out/"focus_functions.csv");
  csv<<"symbol,start,end,size,incoming_direct_calls,outgoing_direct_calls,string_refs,callees,strings,note\n";
  for(const auto&f:focus){
   auto end=endFor(f.va);std::vector<std::uint32_t>outcalls;for(std::uint32_t pc=f.va;pc+4<=end;pc+=4){auto w=e.word(pc);if((w>>26)==3){auto t=((pc+4)&0xf0000000u)|((w&0x03ffffffu)<<2);outcalls.push_back(t);}}
   std::vector<std::string>ss;for(const auto&x:xrefs)if(x.pc>=f.va&&x.pc<end)ss.push_back(x.text);
   csv<<f.name<<','<<H(f.va)<<','<<H(end)<<','<<(end-f.va)<<','<<incoming[f.va].size()<<','<<outcalls.size()<<','<<ss.size()<<',';
   for(std::size_t i=0;i<outcalls.size();i++){if(i)csv<<';';csv<<H(outcalls[i]);}csv<<',';
   std::ostringstream so;for(std::size_t i=0;i<ss.size();i++){if(i)so<<" | ";so<<ss[i];}csv<<Csv(so.str())<<','<<Csv(f.note)<<'\n';
  }

  std::ofstream edges(out/"focus_call_edges.csv");edges<<"caller_symbol,callsite,target\n";
  for(const auto&f:focus){auto end=endFor(f.va);for(std::uint32_t pc=f.va;pc+4<=end;pc+=4){auto w=e.word(pc);if((w>>26)==3){auto t=((pc+4)&0xf0000000u)|((w&0x03ffffffu)<<2);edges<<f.name<<','<<H(pc)<<','<<H(t)<<'\n';}}}

  std::ofstream off(out/"mission_object_offsets.csv");
  off<<"offset,name,confidence,evidence\n"
     <<"0x001C,mission_name_ptr,high,Written from selected mission string by 0x001FA610 and 0x001FAE10\n"
     <<"0x0020,mission_name_buffer,high,Destination backing mission_name_ptr when mission string is copied\n"
     <<"0x0060,elapsed_time,high,0x001F8EB0 adds frame delta to this float every frame\n"
     <<"0x007C,countdown_or_delay,medium,0x001F8EB0 decrements positive value by frame delta\n"
     <<"0x00AC,mission_state_flag,medium,Branches mission completion handling in 0x001F8EB0\n"
     <<"0x00F4,mission_complete_valve,high,Assigned from named valve mission_complete\n"
     <<"0x00F8,mission_failure_valve,high,Assigned from named valve mission_failure\n"
     <<"0x00FC,mission_abort_valve,high,Assigned from named valve mission_abort\n"
     <<"0x0100,mission_timeout_valve,high,Assigned from named valve mission_timeout\n"
     <<"0x0104,mission_replay_valve,high,Assigned from named valve mission_replay\n"
     <<"0x0594,active_gameplay_object,medium,Dereferenced repeatedly by mission frame path\n"
     <<"0x0598,init_flag,medium,Explicitly cleared during mission initialization\n"
     <<"0x05C4,loading_screen_assets,high,Populated from mission.rdr LoadingScreenAssets entry\n";

  std::cout<<"SOCOM EE focus recovery complete\n  output: "<<fs::absolute(out).string()<<"\n";
  return 0;
 }catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 2;}
}
