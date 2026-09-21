#include <cassert>
#include <iostream>
#include "zar_archive.hpp"
#include "compiled_rdr_strings.hpp"

int main(int argc,char**argv) {
    if(argc!=2) {
        std::cerr<<"usage: menu_data_test readerc.zar\n";
        return 2;
    }
    socom::menu_boot::ZarArchive z(argv[1]);
    auto bytes=z.ReadLeaf("dlgMenu.rdr");
    socom::menu_boot::CompiledRdrStrings rdr(std::move(bytes));
    auto m=socom::menu_boot::RecoverRetailMainMenu(rdr);

    assert(m.mainEntries.size()==4);
    assert(m.mainEntries[0]=="NEW GAME");
    assert(m.mainEntries[1]=="LOAD GAME");
    assert(m.mainEntries[2]=="ONLINE");
    assert(m.mainEntries[3]=="OPTIONS");
    assert(m.backgroundMovie=="run/movies/common/menuloop.pss");
    assert(m.font=="myriad");

    std::cout<<"Retail menu data test passed.\n";
    std::cout<<"background="<<m.backgroundMovie<<"\n";
    std::cout<<"font="<<m.font<<"\n";
    for(auto&s:m.mainEntries) std::cout<<"entry="<<s<<"\n";
}
