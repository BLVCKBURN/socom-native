#include <cassert>
#include <iostream>
#include "zar_archive.hpp"
#include "compiled_rdr_screen.hpp"

int main(int argc,char**argv){
    if(argc!=2){std::cerr<<"usage: retail_boot_data_test UI_readerc.zar\n";return 2;}
    socom::menu_boot::ZarArchive ui(argv[1]);

    auto load=[&](const char*leaf){
        auto b=ui.ReadLeaf(leaf);
        socom::retail_boot::CompiledRdrStrings r(std::move(b));
        return socom::retail_boot::ParseScreenStrings(leaf,r);
    };

    auto s0=load("dlgIntroScreen.rdr");
    auto s1=load("dlgIntroCinematic.rdr");
    auto s2=load("dlgSecondCinematic.rdr");
    auto sm=load("dlgMenu.rdr");

    assert(s0.onStartSwitchTarget=="dlgIntroCinematic.rdr");
    assert(s1.backgroundFile=="run/movies/common/sony448.pss");
    assert(s1.onMpegEndTarget=="dlgSecondCinematic.rdr");
    assert(s2.backgroundFile=="run/movies/Intro_2.pss");
    assert(s2.onMpegEndTarget=="dlgMenu.rdr");
    assert(sm.backgroundFile=="run/movies/common/menuloop.pss");
    assert(sm.menuEntries.size()==4);

    std::cout<<"Retail boot-chain data test passed.\n";
    std::cout<<"dlgIntroScreen -> "<<s0.onStartSwitchTarget<<"\n";
    std::cout<<s1.name<<" movie="<<s1.backgroundFile<<" -> "<<s1.onMpegEndTarget<<"\n";
    std::cout<<s2.name<<" movie="<<s2.backgroundFile<<" -> "<<s2.onMpegEndTarget<<"\n";
    std::cout<<sm.name<<" loop="<<sm.backgroundFile<<"\n";
}
