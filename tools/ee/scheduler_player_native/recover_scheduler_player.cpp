#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::uint16_t u16(const std::vector<std::uint8_t>& d, std::size_t o) {
    if (o + 2 > d.size()) throw std::runtime_error("read u16 out of range");
    std::uint16_t v; std::memcpy(&v, d.data()+o, 2); return v;
}
static std::uint32_t u32(const std::vector<std::uint8_t>& d, std::size_t o) {
    if (o + 4 > d.size()) throw std::runtime_error("read u32 out of range");
    std::uint32_t v; std::memcpy(&v, d.data()+o, 4); return v;
}
static float f32bits(std::uint32_t bits) {
    float f; std::memcpy(&f, &bits, 4); return f;
}
static std::string hx(std::uint32_t v, int w=8) {
    std::ostringstream s; s << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(w) << v;
    return s.str();
}

struct Seg { std::uint32_t off{}, va{}, filesz{}, memsz{}, flags{}; };

struct Elf {
    std::vector<std::uint8_t> d;
    std::vector<Seg> segs;
    std::uint32_t gp{};

    explicit Elf(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open ELF: " + p.string());
        in.seekg(0,std::ios::end); auto n=in.tellg(); in.seekg(0,std::ios::beg);
        d.resize(static_cast<std::size_t>(n)); in.read(reinterpret_cast<char*>(d.data()), n);
        if (d.size()<0x34 || std::memcmp(d.data(), "\x7f""ELF",4)!=0) throw std::runtime_error("not ELF");
        if (d[4]!=1 || d[5]!=1) throw std::runtime_error("expected ELF32 little endian");

        auto phoff=u32(d,0x1c); auto phentsz=u16(d,0x2a); auto phnum=u16(d,0x2c);
        for (std::uint16_t i=0;i<phnum;i++) {
            auto o=phoff + std::uint32_t(i)*phentsz;
            if (u32(d,o)==1) segs.push_back({u32(d,o+4),u32(d,o+8),u32(d,o+16),u32(d,o+20),u32(d,o+24)});
        }
        auto shoff=u32(d,0x20); auto shentsz=u16(d,0x2e); auto shnum=u16(d,0x30);
        for (std::uint16_t i=0;i<shnum;i++) {
            auto o=shoff + std::uint32_t(i)*shentsz;
            auto type=u32(d,o+4);
            if (type==0x70000006u) { // SHT_MIPS_REGINFO
                auto off=u32(d,o+16); auto size=u32(d,o+20);
                if (size>=24 && off+24<=d.size()) gp=u32(d,off+20);
            }
        }
    }

    std::size_t off(std::uint32_t va) const {
        for (auto&s:segs) if (va>=s.va && va<s.va+s.filesz) return s.off + (va-s.va);
        throw std::runtime_error("VA not file-backed: "+hx(va));
    }
    std::uint32_t word(std::uint32_t va) const { return u32(d, off(va)); }
    std::string cstr(std::uint32_t va, std::size_t cap=256) const {
        auto o=off(va); std::string s;
        for (std::size_t i=0;i<cap && o+i<d.size();i++) { char c=char(d[o+i]); if (!c) break; s.push_back(c); }
        return s;
    }
};

static std::uint32_t jal_target(std::uint32_t pc, std::uint32_t w) {
    if ((w>>26)!=3) return 0;
    return ((pc+4)&0xF0000000u) | ((w&0x03FFFFFFu)<<2);
}

struct RegRec {
    std::uint32_t callsite;
    const char* name;
    std::uint32_t callback;
    std::uint32_t priority_bits;
    const char* context;
    const char* evidence;
};

int main(int argc,char**argv) {
    try {
        if (argc!=3) {
            std::cerr<<"usage: recover_scheduler_player <SCUS_971.34> <out_dir>\n";
            return 2;
        }
        Elf e(argv[1]); fs::path out=argv[2]; fs::create_directories(out);
        if (e.gp==0) throw std::runtime_error("MIPS .reginfo GP value not found");

        constexpr std::uint32_t kSchedRegister=0x0030B4F0;
        constexpr std::uint32_t kSchedObject=0x00527130;
        constexpr std::uint32_t kPlayerGet=0x00200010;
        constexpr std::uint32_t kPlayerSet=0x00200020;
        constexpr std::int32_t  kPlayerGpOff=-0x7128;
        const std::uint32_t playerGlobal=std::uint32_t(std::int64_t(e.gp)+kPlayerGpOff);

        // Verified static scheduler registrations in SCUS_971.34.
        const std::vector<RegRec> regs = {
            {0x0017C760,"UnitTick",          0x002E1A20,0x3F4CCCCD,"null","core registration"},
            {0x0017C78C,"ai_pre_tick",       0x002C4910,0x3F666666,"null","core registration"},
            {0x0017C7B4,"entity_pre_tick",   0x001F08F0,0x3F800000,"null","core registration"},
            {0x0017C7DC,"weapon_pre_tick",   0x003490E0,0x40400000,"null","core registration"},
            {0x0017C804,"CAiEventTick",      0x0017E900,0x40900000,"null","core registration"},
            {0x0017C830,"entity_tick",       0x001F07D0,0x40933333,"null","core registration"},
            {0x0017C85C,"ai_direg_tick",     0x002C4690,0x40966666,"null","core registration"},
            {0x0017C884,"entity_post_tick",  0x001F05C0,0x40C00000,"null","core registration"},
            {0x0017C8AC,"weapon_post_tick",  0x003490A0,0x41000000,"null","core registration"},
            {0x0017C8D4,"ai_route_planner",  0x0017FDB0,0x41280000,"null","core registration"},
            {0x0017C900,"ai_post_tick",      0x002C4340,0x4129999A,"null","core registration"},
            {0x0017C928,"sound_post_tick",   0x00302DF0,0x41300000,"null","core registration"},
            {0x001F8338,"MissionFadeToEnd",  0x001F8380,0x3F7AE148,"runtime","mission result/fade"},
            {0x001FA770,"diTick",             0x00231DC0,0x40A00000,"null","mission activation"},
            {0x001FA79C,"Mission",            0x001F94A0,0x3F7AE148,"CMission*","mission activation"},
            {0x001FA7C4,"Update visual effects",0x00228C70,0x3F000000,"null","mission activation"},
            {0x0021C284,"CClutterAnimManager",0x0021B4D0,0x3F733333,"null","clutter init"},
            {0x00237E0C,"ParticleTick",       0x00237CF0,0x3E4CCCCD,"null","particle init"},
        };

        std::ofstream sr(out/"scheduler_registrations.csv");
        sr<<"callsite,name,callback,priority,context,evidence,validated\n";
        for (auto&r:regs) {
            auto actual=jal_target(r.callsite,e.word(r.callsite));
            bool ok=actual==kSchedRegister;
            sr<<hx(r.callsite)<<",\""<<r.name<<"\","<<hx(r.callback)<<","<<std::fixed<<std::setprecision(6)
              <<f32bits(r.priority_bits)<<",\""<<r.context<<"\",\""<<r.evidence<<"\","<<(ok?"yes":"NO")<<"\n";
        }
        sr<<"0x001CFB64,\"dynamic zAnim callback\",0x001C3880,\"runtime\",\"command object\",\"name and priority resolved dynamically from command data\","
          <<(jal_target(0x001CFB64,e.word(0x001CFB64))==kSchedRegister?"yes":"NO")<<"\n";

        std::ofstream layout(out/"scheduler_layout.csv");
        layout<<"object,offset,meaning,confidence\n";
        layout<<"scheduler,"<<hx(kSchedObject)<<",global zsys scheduler object,high\n";
        layout<<"callback_node,+0x00,name/string pointer or owned string storage handle,medium\n";
        layout<<"callback_node,+0x04,enabled/state byte,high\n";
        layout<<"callback_node,+0x08,float priority/order,high\n";
        layout<<"callback_node,+0x0C,callback function pointer,high\n";
        layout<<"callback_node,+0x10,user/context pointer,high\n";
        layout<<"callback_node,size,0x1C bytes,high\n";
        layout<<"callback_signature,n/a,bool callback(void* context,float dt),high\n";

        std::ofstream pr(out/"player_runtime.csv");
        pr<<"symbol,address,value_or_meaning,evidence\n";
        pr<<"mips_gp,"<<hx(e.gp)<<",ELF .reginfo GP value,ELF metadata\n";
        pr<<"current_player_global,"<<hx(playerGlobal)<<",global pointer returned by getter,gp-0x7128\n";
        pr<<"current_player_getter,"<<hx(kPlayerGet)<<",returns *(gp-0x7128),single-instruction getter\n";
        pr<<"current_player_setter,"<<hx(kPlayerSet)<<",stores a0 to *(gp-0x7128),single-instruction setter\n";
        pr<<"player_setter_callsite_1,0x0017BFAC,state/application path,direct JAL\n";
        pr<<"player_setter_callsite_2,0x00252000,seal/player creation path,direct JAL\n";
        pr<<"mission_tick_player_use_1,0x001F8EF8,fetches current player,direct JAL\n";
        pr<<"mission_tick_player_use_2,0x001F91A4,fetches current player,direct JAL\n";
        pr<<"mission_tick_embedded_object,+0x1C,object passed to 0x00250C90/0x00250E30,high\n";

        std::ofstream mf(out/"mission_frame_calls.csv");
        mf<<"callsite,target,note\n";
        for (std::uint32_t pc=0x001F8EB0; pc<0x001F9208; pc+=4) {
            auto t=jal_target(pc,e.word(pc));
            if (t) {
                std::string note;
                if (t==0x00200010) note="current player getter";
                else if (t==0x00250C90) note="seal.cpp-neighborhood per-frame/player-proximity routine";
                else if (t==0x00250E30) note="seal.cpp-neighborhood deferred/global cleanup routine";
                else if (t==0x001F8580) note="mission result transition";
                else if (t==0x001F8900) note="objective maintenance";
                else if (t==0x001F92A0) note="mission runtime cleanup";
                mf<<hx(pc)<<","<<hx(t)<<",\""<<note<<"\"\n";
            }
        }

        std::ofstream sum(out/"recovery_summary.txt");
        sum<<"SCUS_971.34 scheduler/player recovery\n";
        sum<<"GP="<<hx(e.gp)<<"\n";
        sum<<"scheduler_register="<<hx(kSchedRegister)<<"\n";
        sum<<"scheduler_object="<<hx(kSchedObject)<<"\n";
        sum<<"current_player_global="<<hx(playerGlobal)<<"\n";
        sum<<"current_player_getter="<<hx(kPlayerGet)<<"\n";
        sum<<"current_player_setter="<<hx(kPlayerSet)<<"\n";
        sum<<"mission_scheduler_callback=0x001F94A0\n";
        sum<<"mission_scheduler_registration=0x001FA79C\n";

        std::cout<<"Scheduler/player recovery complete\n";
        std::cout<<"  GP: "<<hx(e.gp)<<"\n";
        std::cout<<"  current player global: "<<hx(playerGlobal)<<"\n";
        std::cout<<"  scheduler registrations: "<<regs.size()+1<<"\n";
        return 0;
    } catch (const std::exception&e) {
        std::cerr<<"error: "<<e.what()<<"\n";
        return 1;
    }
}
