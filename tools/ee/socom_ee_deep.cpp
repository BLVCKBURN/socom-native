#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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
class Elf{
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
struct Xref{std::uint32_t pc{},sva{};std::string text;};
struct F{const char*name;std::uint32_t start,end;const char*confidence;const char*note;};

int main(int argc,char**argv){
 try{
  if(argc!=3){std::cerr<<"Usage: socom_ee_deep <SCUS_971.34> <output_dir>\n";return 1;}
  Elf e(argv[1]);fs::path out=argv[2];fs::create_directories(out);
  if(e.entry!=0x00140008u||e.s.va!=0x00100000u||e.s.filesz!=0x0038D200u||e.s.memsz!=0x004D6D00u)
   throw std::runtime_error("input does not match expected SCUS_971.34 retail image");
  const std::uint32_t codeEnd=std::min<std::uint32_t>(0x00440000u,e.s.va+e.s.filesz);

  std::unordered_map<std::uint32_t,std::string> byva;
  for(std::size_t i=e.s.off,end=e.s.off+e.s.filesz;i<end;){std::size_t j=i;while(j<end&&Printable(e.b[j]))++j;if(j-i>=4&&j<end&&e.b[j]==0){auto va=e.s.va+std::uint32_t(i-e.s.off);byva.emplace(va,std::string((const char*)&e.b[i],j-i));i=j+1;}else ++i;}
  std::vector<Xref>xrefs;
  for(std::uint32_t pc=e.s.va;pc+4<=codeEnd;pc+=4){auto w=e.word(pc);if((w>>26)!=0x0f)continue;auto rt=(w>>16)&31,hi=w&0xffffu;for(int j=1;j<=12&&pc+4*j<codeEnd;j++){auto q=e.word(pc+4*j),op=q>>26,rs=(q>>21)&31,rt2=(q>>16)&31,lo=q&0xffffu;std::uint32_t va=0;bool ok=false;if(rt2==rt&&rs==rt&&op==9){va=(hi<<16)+std::uint32_t(S16((std::uint16_t)lo));ok=true;}else if(rt2==rt&&rs==rt&&op==0x0d){va=(hi<<16)|lo;ok=true;}if(ok){auto it=byva.find(va);if(it!=byva.end())xrefs.push_back({pc,va,it->second});break;}}}

  static const F funcs[]={
   {"ui_vector_empty",0x001D4930u,0x001D4940u,"high","Returns (header->count == 0); count is +0x04."},
   {"ui_vector_pop_count",0x001D4940u,0x001D4950u,"high","Decrements 32-bit count at +0x04."},
   {"ui_vector_back",0x001D4950u,0x001D4970u,"high","Returns data(+0x08) + count*4 - 4."},
   {"ui_vector_push_one",0x001D4970u,0x001D4990u,"medium","Thin wrapper over generic insertion helper; inserts one 32-bit element."},
   {"ui_vector_back_const",0x001D4990u,0x001D49B0u,"high","Const-equivalent back() helper."},
   {"ui_SwitchMenu",0x001D5DB0u,0x001D6150u,"high","Original menu transition handler; manipulates 0x004B3450 records and two stack-like containers."},
   {"ui_vector_empty_alt",0x001D6150u,0x001D6160u,"high","Second template-equivalent empty() helper."},
   {"ui_SetMission",0x001D61A0u,0x001D62A0u,"high","Resolves command argument and calls CMission::LoadMissionDescriptor at 0x001FAE10."},
   {"mission_set_result_state",0x001F8580u,0x001F8900u,"high","Transitions terminal mission state; state values 2/3/4/5 map to complete/failure/abort/timeout."},
   {"mission_update_objectives",0x001F8900u,0x001F8EB0u,"high","Periodic objective maintenance; references PRIMARY OBJ and SECONDARY OBJ."},
   {"mission_frame",0x001F8EB0u,0x001F9210u,"high","Actual per-frame CMission routine."},
   {"mission_runtime_cleanup",0x001F92A0u,0x001F94A0u,"high","Runtime cleanup path; removes common/mission animation sets and tears down subsystems."},
   {"mission_tick_wrapper",0x001F94A0u,0x001F94C0u,"high","Thin scheduler callback that invokes mission_frame."},
   {"mission_load_ai_params",0x001F94C0u,0x001F9710u,"high","Loads ai_params block and named mission tuning values."},
   {"mission_ai_params_defaults",0x001F9710u,0x001F9780u,"high","Initializes the 0x30-byte tuning block later populated by mission_load_ai_params."},
   {"mission_state_shutdown",0x001F9780u,0x001F9CF0u,"high","Called from the game-state shutdown callback at 0x00172E3C; clears active mission runtime state."},
   {"mission_small_vector_swap",0x001F9CF0u,0x001F9D50u,"high","Swaps +4/+8 fields of two small container headers after helper call."},
   {"mission_small_vector_init",0x001F9D50u,0x001F9D70u,"high","Zeros three 32-bit fields in a small container header."},
   {"mission_mount_animation_data",0x001F9D70u,0x001F9FA0u,"high","Mounts common/mission zanim and mzanim archives/RDR data."},
   {"mission_state_enter_runtime",0x001F9FA0u,0x001FA2A0u,"high","Called from state transition at 0x0017CC58; creates mission result valves and initializes runtime systems."},
   {"mission_runtime_phase_a",0x001FA2A0u,0x001FA310u,"medium","State/runtime phase helper; exact original name not yet recovered."},
   {"mission_runtime_phase_b",0x001FA310u,0x001FA610u,"medium","Large teardown/reset phase; destroys mission arrays, animation sets and many subsystem objects."},
   {"mission_activate",0x001FA610u,0x001FACB0u,"high","Heavy per-mission activation called by state loader at 0x00172EC0 with selected mission name."},
   {"mission_load_descriptor",0x001FAE10u,0x001FB4F0u,"high","Loads mission.rdr, UiVars, Valves, loading screen data and environment configuration."},
   {"mission_construct_global",0x001FB4F0u,0x001FB930u,"high","One-time construction/init of singleton at 0x004D4880; called during application startup at 0x00140658."}
  };

  std::map<std::uint32_t,std::vector<std::uint32_t>> incoming;
  for(std::uint32_t pc=e.s.va;pc+4<=codeEnd;pc+=4){auto w=e.word(pc);if((w>>26)==3){auto t=((pc+4)&0xF0000000u)|((w&0x03ffffffu)<<2);incoming[t].push_back(pc);}}
  std::ofstream ff(out/"deep_functions.csv");
  ff<<"symbol,start,end,size,confidence,incoming_direct_calls,outgoing_direct_calls,callees,strings,note\n";
  for(const auto&f:funcs){
   std::vector<std::uint32_t> calls; for(std::uint32_t pc=f.start;pc+4<=f.end;pc+=4){auto w=e.word(pc);if((w>>26)==3)calls.push_back(((pc+4)&0xf0000000u)|((w&0x03ffffffu)<<2));}
   std::vector<std::string> ss; for(const auto&x:xrefs)if(x.pc>=f.start&&x.pc<f.end)ss.push_back(x.text);
   ff<<f.name<<','<<H(f.start)<<','<<H(f.end)<<','<<(f.end-f.start)<<','<<f.confidence<<','<<incoming[f.start].size()<<','<<calls.size()<<',';
   for(std::size_t i=0;i<calls.size();i++){if(i)ff<<';';ff<<H(calls[i]);} ff<<',';
   std::ostringstream so;for(std::size_t i=0;i<ss.size();i++){if(i)so<<" | ";so<<ss[i];}
   ff<<Csv(so.str())<<','<<Csv(f.note)<<'\n';
  }

  std::ofstream life(out/"mission_lifecycle.csv");
  life<<"stage,address,confidence,evidence\n"
      <<"global_construct,0x001FB4F0,high,Called on singleton 0x004D4880 during application startup at 0x00140658\n"
      <<"select_load_descriptor,0x001FAE10,high,Called by UI SetMission 0x001D61A0 and loads mission.rdr configuration\n"
      <<"activate_selected_mission,0x001FA610,high,Called by state loader at 0x00172EC0 with selected mission string\n"
      <<"mount_animation_data,0x001F9D70,high,Called from mission_activate and mounts common/mission zanim+mzanim data\n"
      <<"state_enter_runtime,0x001F9FA0,high,Called by state transition at 0x0017CC58; creates mission result valves and runtime systems\n"
      <<"per_frame,0x001F8EB0,high,Actual mission frame routine; wrapper 0x001F94A0 delegates here\n"
      <<"objective_update,0x001F8900,high,Called from mission_frame and periodically updates objective state\n"
      <<"terminal_result_transition,0x001F8580,high,Called for complete/failure/abort/timeout terminal states\n"
      <<"runtime_cleanup,0x001F92A0,high,Called by result/frame paths and tears down active runtime pieces\n"
      <<"state_shutdown,0x001F9780,high,Called by state shutdown callback 0x00172E3C\n";

  std::ofstream mo(out/"mission_object_offsets.csv");
  mo<<"offset,name,confidence,evidence\n"
    <<"0x0018,result_state,high,Byte compared/written by 0x001F8580 during terminal state transitions\n"
    <<"0x001C,mission_name_ptr,high,Selected mission string pointer used by activation/archive path formatting\n"
    <<"0x0020,mission_name_buffer,high,Backing storage used when selected mission name is copied\n"
    <<"0x0060,elapsed_time,high,0x001F8EB0 adds frame delta every frame\n"
    <<"0x0064,result_deadline,high,0x001F8580 uses -1 sentinel then sets elapsed_time + 3.0 on transition\n"
    <<"0x0068,mode_time_accumulator,medium,Updated by frame path in player-active branch; exact original member name unknown\n"
    <<"0x006C,mode_time_scale,medium,Multiplied by frame delta before accumulation into +0x68\n"
    <<"0x007C,countdown_or_delay,medium,Positive value is decremented by frame delta\n"
    <<"0x0084,array0_count,medium,Count consumed during runtime phase-B destruction\n"
    <<"0x0088,array0_ptr,medium,Pointer array whose entries are passed to retail function 0x002526C0\n"
    <<"0x0098,objective_container,medium,Container initialized in constructor and used by objective/runtime paths\n"
    <<"0x009C,objective_count,medium,Count consumed during objective/runtime teardown\n"
    <<"0x00A0,objective_entries_ptr,medium,Entry storage used with +0x9C count\n"
    <<"0x00A8,objective_refresh_timer,high,Accumulates frame delta and is reset around one-second objective refresh\n"
    <<"0x00AC,mission_state_flag,medium,Branches terminal handling in mission frame\n"
    <<"0x00B4,ai_params_block,high,Start of 0x30-byte defaults block initialized at 0x001F9710\n"
    <<"0x00F4,mission_complete_valve,high,Named mission_complete valve handle\n"
    <<"0x00F8,mission_failure_valve,high,Named mission_failure valve handle\n"
    <<"0x00FC,mission_abort_valve,high,Named mission_abort valve handle\n"
    <<"0x0100,mission_timeout_valve,high,Named mission_timeout valve handle\n"
    <<"0x0104,mission_replay_valve,high,Named mission_replay valve handle\n"
    <<"0x0590,runtime_list_or_head,medium,Initialized to null by constructor and traversed by runtime phases\n"
    <<"0x0594,active_gameplay_object,medium,Allocated in constructor/init path and dereferenced by mission frame\n"
    <<"0x0598,init_flag,medium,Cleared on construction/state changes and set by runtime initialization\n"
    <<"0x05C4,loading_screen_assets,high,Populated from mission.rdr LoadingScreenAssets\n"
    <<"0x05C8,runtime_container_a,medium,Container constructed/destructed in lifecycle paths\n"
    <<"0x05D4,runtime_container_b,medium,Container used by mission camera/objective/runtime paths\n"
    <<"0x05E4,satchel_timer,high,Default 10.0 and overridden by SATCHEL_TIMER from ai_params\n";

  std::ofstream ui(out/"ui_runtime_layout.csv");
  ui<<"base_or_offset,name,size,confidence,evidence\n"
    <<"0x004B31D8,ui_command_context_ptr,4,high,Loaded by SetMission/SwitchMenu before command-argument resolver 0x001CB780\n"
    <<"context+0x0018,dynamic_input_source,4,medium,Passed to 0x001CEAE0 when command argument is INPUT_STRING/missing\n"
    <<"context+0x0024,command_argument_storage,unknown,medium,Passed by 0x001CB780 to argument lookup helper 0x001C3AF0\n"
    <<"context+0x0034,command_context_handle,2,high,16-bit handle consumed by 0x001CB780\n"
    <<"0x004B3450,menu_state_storage,unknown,high,Base used directly by SwitchMenu\n"
    <<"menu+0x0000,command_records,0x820,high,40 records * 0x34 bytes\n"
    <<"menu_record+0x0000,record_type,4,high,SwitchMenu writes type 5 and type 2\n"
    <<"menu_record+0x0004,record_text,48,high,String copied here; total record size is 0x34\n"
    <<"menu+0x0820,pending_count,4,medium,Incremented when SwitchMenu queues a record\n"
    <<"menu+0x0824,record_index_or_count,4,medium,Incremented before selecting/writing 0x34-byte record\n"
    <<"menu+0x0828,stack_a_header,12+,high,Small vector-like object with count at +4 and data pointer at +8\n"
    <<"menu+0x0E34,stack_b_header,12+,high,Second small vector-like object receiving popped 32-bit values\n";

  std::cout<<"SOCOM EE deep recovery complete\n  output: "<<fs::absolute(out).string()<<"\n";
  return 0;
 }catch(const std::exception&ex){std::cerr<<"error: "<<ex.what()<<'\n';return 2;}
}
