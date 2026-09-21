#include <cassert>
#include <cmath>
#include <iostream>
#include "zar_archive.hpp"
#include "compiled_rdr_ast.hpp"
#include "screen_model.hpp"
#include "ui_vm.hpp"

int main(int argc,char**argv){
    if(argc!=2)return 2;
    socom::menu_boot::ZarArchive z(argv[1]);

    auto make=[&](){
        auto b=z.ReadLeaf("dlgMenu.rdr");
        socom::retail_menu::CompiledRdr r(std::move(b));
        return socom::retail_ui::ParseScreen("dlgMenu.rdr",r.ParseRoot());
    };

    socom::retail_ui::UiVm vm;

    auto s=make();
    assert(s.animations["NewSetDown"].size()>=6);

    auto o=vm.Execute("CallOptions",s);
    assert(o.switchMenu=="dlgOptions_Menu.rdr");

    auto n=make();
    vm.valves["ChosePresetOnce"]=0;
    auto ng=vm.Execute("UiprepMission1",n);
    assert(ng.switchMenu=="dlgControllerPresetsNewGame.rdr");

    auto n2=make();
    vm.valves["ChosePresetOnce"]=1;
    auto ng2=vm.Execute("UiprepMission1",n2);
    assert(ng2.switchMenu=="dlgAlaskaCinematic.rdr");

    auto m=make();
    vm.valves["MenuSpot"]=1;
    vm.Execute("OnMenuBtnDown",m);

    auto* newGame=socom::retail_ui::FindControl(m,"new_game_button");
    auto* load=socom::retail_ui::FindControl(m,"load_game_button");
    auto* online=socom::retail_ui::FindControl(m,"multiplayer_button");

    assert(vm.GetValve("MenuSpot")==2);
    assert(newGame && std::fabs(newGame->y-275.0f)<0.01f);
    assert(load && std::fabs(load->y-250.0f)<0.01f);
    assert(online && std::fabs(online->y-300.0f)<0.01f);

    std::cout<<"Retail UI Runtime v6 test passed.\n";
    std::cout<<"CallOptions -> "<<o.switchMenu<<"\n";
    std::cout<<"NEW GAME first-run -> "<<ng.switchMenu<<"\n";
    std::cout<<"NEW GAME configured -> "<<ng2.switchMenu<<"\n";
    std::cout<<"OnMenuBtnDown: MenuSpot="<<vm.GetValve("MenuSpot")
             <<" new_game_y="<<newGame->y
             <<" load_game_y="<<load->y
             <<" online_y="<<online->y<<"\n";
}