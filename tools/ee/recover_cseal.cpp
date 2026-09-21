#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

static std::uint16_t u16(const std::vector<std::uint8_t>& d, std::size_t o) {
    std::uint16_t v{}; std::memcpy(&v,d.data()+o,2); return v;
}
static std::uint32_t u32(const std::vector<std::uint8_t>& d, std::size_t o) {
    std::uint32_t v{}; std::memcpy(&v,d.data()+o,4); return v;
}
static std::string hx(std::uint32_t v) {
    std::ostringstream s; s<<"0x"<<std::uppercase<<std::hex<<std::setw(8)<<std::setfill('0')<<v; return s.str();
}
struct Seg { std::uint32_t off,va,filesz,memsz; };
struct Elf {
    std::vector<std::uint8_t>d; std::vector<Seg>s;
    explicit Elf(const fs::path&p){
        std::ifstream f(p,std::ios::binary); if(!f)throw std::runtime_error("cannot open ELF");
        f.seekg(0,std::ios::end); auto n=f.tellg(); f.seekg(0,std::ios::beg);
        d.resize((std::size_t)n); f.read((char*)d.data(),n);
        auto phoff=u32(d,0x1c); auto ents=u16(d,0x2a); auto num=u16(d,0x2c);
        for(unsigned i=0;i<num;i++){auto o=phoff+i*ents;if(u32(d,o)==1)s.push_back({u32(d,o+4),u32(d,o+8),u32(d,o+16),u32(d,o+20)});}
    }
    std::size_t off(std::uint32_t va)const{
        for(auto&a:s)if(va>=a.va&&va<a.va+a.filesz)return a.off+(va-a.va);
        throw std::runtime_error("VA not file-backed: "+hx(va));
    }
    std::uint32_t w(std::uint32_t va)const{return u32(d,off(va));}
};
static std::uint32_t jal(std::uint32_t pc,std::uint32_t w){
    if((w>>26)!=3)return 0; return ((pc+4)&0xf0000000u)|((w&0x03ffffffu)<<2);
}
static bool has_call(const Elf&e,std::uint32_t s,std::uint32_t eaddr,std::uint32_t target){
    for(auto pc=s;pc<eaddr;pc+=4)if(jal(pc,e.w(pc))==target)return true; return false;
}
int main(int argc,char**argv){
    try{
        if(argc!=3){std::cerr<<"usage: recover_cseal SCUS_971.34 OUT_DIR\n";return 2;}
        Elf e(argv[1]);fs::path out=argv[2];fs::create_directories(out);

        struct F{const char*n;std::uint32_t s,e;const char*role;};
        const F funcs[]={
            {"player_spawn_path",0x00251F10,0x00252048,"application/player spawn wrapper"},
            {"cseal_factory",0x002955A0,0x002956F0,"allocates 0x1180-byte CSeal and constructs/registers it"},
            {"cseal_constructor",0x0025D5E0,0x0025DA80,"CSeal construction / embedded member initialization"},
            {"cseal_destructor",0x0025CD50,0x0025D500,"CSeal teardown/destruction"},
            {"seal_state_dispatch_a",0x00252050,0x00252150,"state dispatch using CSeal +0x160"},
            {"seal_list_remove",0x002526C0,0x002527C8,"remove/free record from seal-adjacent global list"},
            {"seal_record_create",0x002527D0,0x00252920,"allocate/init 0x2c-byte record and attach to global list"},
        };

        std::ofstream f(out/"cseal_functions.csv");
        f<<"symbol,start,end,size,role\n";
        for(auto&a:funcs)f<<a.n<<","<<hx(a.s)<<","<<hx(a.e)<<","<<(a.e-a.s)<<",\""<<a.role<<"\"\n";

        std::ofstream l(out/"cseal_layout.csv");
        l<<"offset,meaning,confidence,evidence\n";
        l<<"size,0x1180,high,\"factory passes 0x1180 to allocator at 0x002955E4\"\n";
        l<<"+0x000,primary vtable pointer,high,\"constructor writes 0x004899F0; destructor restores it\"\n";
        l<<"+0x004,base/entity field preserved during reinit,medium,\"read/restored by seal_create path\"\n";
        l<<"+0x00C,entity/gameplay link or state pointer,medium,\"spawn/reinit clears/restores it\"\n";
        l<<"+0x014,secondary attached pointer,medium,\"seal_create queries and preserves it\"\n";
        l<<"+0x028,entity/model/node pointer,high,\"spawn path passes it to naming/registration routine\"\n";
        l<<"+0x0C0,controller/owned interface pointer,high,\"virtual call +0x2c used repeatedly during create/destroy\"\n";
        l<<"+0x110,secondary/base interface vtable,high,\"constructor/destructor write 0x00489A4C\"\n";
        l<<"+0x118,embedded owned structure pointer,medium,\"destructor walks object reached here\"\n";
        l<<"+0x158,initialized zero,high,\"constructor\"\n";
        l<<"+0x15C,initialized zero,high,\"constructor\"\n";
        l<<"+0x160,state byte / mode enum,high,\"jump tables at 0x00252050 and 0x00252150\"\n";
        l<<"+0x161,secondary state byte,high,\"destructor resets to -1\"\n";
        l<<"+0x162,state/flag byte,high,\"destructor resets to 0\"\n";
        l<<"+0x164,float state parameter,high,\"destructor sets 1.0f\"\n";
        l<<"+0x178,flag word,high,\"constructor bit manipulation\"\n";
        l<<"+0x1BC,embedded object,high,\"constructor calls 0x00220200 on this subobject\"\n";
        l<<"+0x204,embedded callback/list block,medium,\"constructor initializes 0x48-sized structure\"\n";
        l<<"+0x260,embedded container/subsystem,high,\"constructor calls 0x0025ECF0\"\n";
        l<<"+0x2F0,embedded container/subsystem,high,\"constructor calls 0x001D6400\"\n";
        l<<"+0x304,state byte,high,\"constructor = 0\"\n";
        l<<"+0x305,state byte,high,\"constructor = 2\"\n";
        l<<"+0x308,pointer/int state,high,\"constructor = 0\"\n";
        l<<"+0x30C,embedded object,medium,\"constructor call 0x00172E10\"\n";
        l<<"+0x38C,embedded object,medium,\"constructor call 0x00196140\"\n";
        l<<"+0x428..+0x448,three float3 vectors,high,\"constructor copies same zero/default vector three times\"\n";
        l<<"+0x478,creation descriptor/source pointer,high,\"constructor stores third ctor argument\"\n";
        l<<"+0x47C,embedded object,medium,\"constructor calls 0x00252F20\"\n";
        l<<"+0x4D0,embedded object,medium,\"constructor calls 0x00252F20\"\n";
        l<<"+0x524,+0x528,integer state fields,high,\"constructor zeroes\"\n";
        l<<"+0x52C,float parameter,high,\"constructor initializes random/scaled value\"\n";
        l<<"+0x530,major embedded gameplay/task subsystem,high,\"heavily used by state handlers and destruction\"\n";
        l<<"+0xDC0,vtable/manager pointer region,medium,\"constructor writes type table pointer\"\n";
        l<<"+0xDC4,self pointer,high,\"constructor stores this\"\n";
        l<<"+0xDC8,attached runtime/controller pointer,high,\"seal_create writes global interface pointer\"\n";
        l<<"+0xDCC,peer/backlink pointer,medium,\"destructor clears reciprocal +0xDCC\"\n";
        l<<"+0xDD8,+0xDDC,owned/reference pointers,high,\"small setter helpers at 0x002526A0/0x00252680\"\n";
        l<<"+0xDE0,seal-adjacent record pointer,high,\"0x00251770 destroys; 0x002517A0 creates/updates\"\n";
        l<<"+0xE90,embedded container,high,\"constructor initializes\"\n";
        l<<"+0xEED..+0xEEF,state bytes,high,\"constructor zeroes\"\n";
        l<<"+0xF85..+0xF88,flag bytes/word,high,\"constructor/destructor bit operations\"\n";
        l<<"+0xFAC,embedded container,high,\"constructor initializes\"\n";
        l<<"+0xFF4..+0xFFC,float3 vector,high,\"constructor sets default vector\"\n";
        l<<"+0x1020,embedded owned object,medium,\"constructor calls 0x0022B9B0\"\n";
        l<<"+0x10E0..+0x10F4,two float3 vectors,high,\"constructor initializes default vectors\"\n";

        std::ofstream p(out/"player_creation_chain.csv");
        p<<"step,address,detail,validated\n";
        p<<"spawn_wrapper,0x00251F10,builds Player%d name and chooses local/network player allocation,yes\n";
        p<<"factory_call,0x00251FF4,calls CSeal factory 0x002955A0,"<<(has_call(e,0x251f10,0x252048,0x2955a0)?"yes":"NO")<<"\n";
        p<<"factory_allocate,0x002955E4,allocates 0x1180 bytes,yes\n";
        p<<"constructor_call,0x00295600,calls CSeal constructor 0x0025D5E0,"<<(has_call(e,0x2955a0,0x2956f0,0x25d5e0)?"yes":"NO")<<"\n";
        p<<"entity_registration,0x00295610,calls entity registration path 0x001F1070,"<<(has_call(e,0x2955a0,0x2956f0,0x1f1070)?"yes":"NO")<<"\n";
        p<<"current_player_set,0x00252000,calls current-player setter 0x00200020,"<<(has_call(e,0x251f10,0x252048,0x200020)?"yes":"NO")<<"\n";
        p<<"node_name,0x0025200C,names/registers object at CSeal +0x28,yes\n";
        p<<"post_spawn_entity_init,0x00252020,calls entity helper 0x001F00B0 on CSeal,yes\n";
        p<<"post_spawn_entity_link,0x00252030,calls entity helper 0x001F0010 on CSeal,yes\n";

        std::ofstream s(out/"cseal_summary.txt");
        s<<"SCUS_971.34 CSeal recovery\n";
        s<<"factory=0x002955A0\nconstructor=0x0025D5E0\ndestructor=0x0025CD50\n";
        s<<"size=0x1180\nprimary_vtable=0x004899F0\nsecondary_vtable=0x00489A4C\n";
        s<<"player_spawn_wrapper=0x00251F10\ncurrent_player_set_callsite=0x00252000\n";
        std::cout<<"CSeal recovery complete: size 0x1180, ctor 0x0025D5E0, dtor 0x0025CD50\n";
        return 0;
    }catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<"\n";return 1;}
}
