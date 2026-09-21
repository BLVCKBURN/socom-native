#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "weapon_definition_accessors.hpp"

static void W32(std::uint8_t* p, std::uint32_t v) { std::memcpy(p,&v,4); }
static void WF(std::uint8_t* p, float v) { std::memcpy(p,&v,4); }

int main() {
    std::array<std::uint8_t,0x200> w{};
    using L=socom::recomp::scus97134::weapon_runtime::DefinitionLayout;
    W32(w.data()+L::Field04,0x11111111);
    W32(w.data()+L::Field08,0x22222222);
    W32(w.data()+L::Field24,0x24242424);
    W32(w.data()+L::Field28,0x28282828);
    WF(w.data()+L::Value38,3.8f);
    WF(w.data()+L::Value3C,3.12f);
    WF(w.data()+L::Value40,4.0f);
    WF(w.data()+L::Value44,4.4f);
    WF(w.data()+L::FireWait,0.125f);
    w[L::Type]=2;
    w[0x34]=0xFE;

    using namespace socom::recomp::native_weapon;
    assert(GetField04(w.data())==0x11111111);
    assert(GetField08(w.data())==0x22222222);
    assert(GetField24(w.data())==0x24242424);
    assert(GetField28(w.data())==0x28282828);
    assert(std::fabs(GetFloat38(w.data())-3.8f)<0.0001f);
    assert(std::fabs(GetFloat40(w.data())-4.0f)<0.0001f);
    assert(std::fabs(GetFireWait(w.data())-0.125f)<0.0001f);
    assert(GetType(w.data())==2);
    assert(GetByte34(w.data())==-2);
    std::cout<<"Weapon definition native accessor smoke test passed.\n";
}
