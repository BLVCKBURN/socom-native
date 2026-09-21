#pragma once
#include "compiled_rdr_ast.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace socom::retail_menu {

struct Color3 { int r{},g{},b{}; };
struct ButtonStyle {
    float scale{};
    Color3 text{};
};
struct RetailButton {
    std::string name;
    std::string caption;
    int x{},y{},width{},height{};
    ButtonStyle normal,active,pressed,disabled;
    std::string crossAnimation;
};
struct MainMenuModel {
    std::string movie;
    std::string font;
    std::vector<RetailButton> buttons;
};

inline Color3 ReadColor(const RdrNode* n){
    Color3 c{};
    if(!n)return c;
    int vals[3]{},k=0;
    for(const auto&v:n->values){
        if(auto i=AsInt(v)){if(k<3)vals[k++]=*i;}
        else if(auto f=AsFloat(v)){if(k<3)vals[k++]=static_cast<int>(*f);}
    }
    c={vals[0],vals[1],vals[2]};return c;
}
inline ButtonStyle ReadStyle(const RdrNode*spec,const std::string&key){
    ButtonStyle st{};
    auto n=FindPairNode(*spec,key); if(!n)return st;
    if(auto s=FindPairNode(*n,"SCALE"))st.scale=FirstNumber(*s);
    if(auto c=FindPairNode(*n,"TEXT_COLOR"))st.text=ReadColor(c);
    return st;
}
inline std::string FindCrossAnimation(const RdrNode*spec){
    auto anims=FindPairNode(*spec,"ANIMATIONS");if(!anims)return {};
    for(const auto&v:anims->values){
        auto rec=AsNode(v);if(!rec)continue;
        auto b=FindPairNode(*rec,"BUTTON");
        auto a=FindPairNode(*rec,"ANIMATION");
        if(b&&a&&FirstString(*b)=="CROSS")return FirstString(*a);
    }
    return {};
}
inline MainMenuModel ExtractMainMenu(const RdrNode&root){
    MainMenuModel m;
    if(root.values.empty())throw std::runtime_error("empty RDR root");
    const auto screenWrap=AsNode(root.values[0]);
    if(!screenWrap)throw std::runtime_error("missing SCREENS wrapper");
    const auto screens=FindPairNode(*screenWrap,"SCREENS");
    if(!screens)throw std::runtime_error("missing SCREENS");
    if(auto bg=FindPairNode(*screens,"BACKGROUND"))
        if(auto f=FindPairNode(*bg,"FILENAME"))m.movie=FirstString(*f);
    if(auto font=FindPairNode(*screens,"FONT"))m.font=FirstString(*font);

    auto controls=FindPairNode(*screens,"CONTROLS");
    if(!controls)throw std::runtime_error("missing CONTROLS");
    for(const auto&v:controls->values){
        auto c=AsNode(v);if(!c)continue;
        auto type=FindPairNode(*c,"TYPE");
        if(!type || FirstString(*type)!="BUTTON")continue;
        RetailButton b;
        if(auto n=FindPairNode(*c,"NAME"))b.name=FirstString(*n);
        if(auto x=FindPairNode(*c,"XPOS"))b.x=FirstInt(*x);
        if(auto y=FindPairNode(*c,"YPOS"))b.y=FirstInt(*y);
        auto spec=FindPairNode(*c,"SPEC");if(!spec)continue;
        if(auto x=FindPairNode(*spec,"XSIZE"))b.width=FirstInt(*x);
        if(auto y=FindPairNode(*spec,"YSIZE"))b.height=FirstInt(*y);
        if(auto cap=FindPairNode(*spec,"CAPTION"))b.caption=FirstString(*cap);
        b.normal=ReadStyle(spec,"NORMAL");
        b.active=ReadStyle(spec,"ACTIVE");
        b.pressed=ReadStyle(spec,"PRESSED");
        b.disabled=ReadStyle(spec,"DISABLED");
        b.crossAnimation=FindCrossAnimation(spec);
        m.buttons.push_back(std::move(b));
    }
    return m;
}

inline int MenuSpotForButton(const std::string&name){
    if(name=="new_game_button")return 1;
    if(name=="multiplayer_button")return 2;
    if(name=="options_button")return 3;
    if(name=="tutorial_button")return 4;
    if(name=="load_game_button")return 5;
    return 0;
}
inline std::string TransitionForAnimation(const std::string&a){
    if(a=="CallOptions")return "dlgOptions_Menu.rdr";
    if(a=="CallDocumentary")return "dlg_documentary.rdr";
    if(a=="UiprepMission1")return "dlgControllerPresetsNewGame.rdr";
    return {};
}

} // namespace
