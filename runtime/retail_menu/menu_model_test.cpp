#include <cassert>
#include <iostream>
#include "zar_archive.hpp"
#include "compiled_rdr_ast.hpp"
#include "main_menu_model.hpp"
int main(int argc,char**argv){
    if(argc!=2)return 2;
    socom::menu_boot::ZarArchive z(argv[1]);
    auto b=z.ReadLeaf("dlgMenu.rdr");
    socom::retail_menu::CompiledRdr rdr(std::move(b));
    auto model=socom::retail_menu::ExtractMainMenu(rdr.ParseRoot());
    assert(model.movie=="run/movies/common/menuloop.pss");
    assert(model.font=="myriad");
    assert(model.buttons.size()==5);
    for(const auto&x:model.buttons){
        std::cout<<x.name<<","<<x.caption<<","<<x.x<<","<<x.y<<","<<x.crossAnimation<<"\n";
    }
    return 0;
}
