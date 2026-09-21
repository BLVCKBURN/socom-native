#pragma once
#include "compiled_rdr_ast.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace socom::retail_ui {

using namespace socom::retail_menu;

inline std::string ScalarString(const RdrNode* n) {
    if(!n) return {};
    for(const auto& v:n->values) {
        if(auto s=AsString(v)) return *s;
        if(auto sub=AsNode(v)) {
            auto r=ScalarString(sub);
            if(!r.empty()) return r;
        }
    }
    return {};
}
inline int ScalarInt(const RdrNode* n,int def=0) {
    if(!n) return def;
    for(const auto&v:n->values) {
        if(auto i=AsInt(v)) return *i;
        if(auto f=AsFloat(v)) return (int)*f;
        if(auto sub=AsNode(v)) return ScalarInt(sub,def);
    }
    return def;
}
inline float ScalarFloat(const RdrNode* n,float def=0) {
    if(!n) return def;
    for(const auto&v:n->values) {
        if(auto f=AsFloat(v)) return *f;
        if(auto i=AsInt(v)) return (float)*i;
        if(auto sub=AsNode(v)) return ScalarFloat(sub,def);
    }
    return def;
}

struct Color3{int r=128,g=128,b=128;};
struct Style { float scale=0.5f; Color3 text{}; std::string image; };
struct Action {
    std::string animation;
    std::string argument;
};
struct Control {
    std::string name,type,caption,parent;
    float x{},y{},opacity=1.0f;
    float motionStartX{},motionStartY{},motionTargetX{},motionTargetY{};
    float motionElapsed=0.0f,motionDuration=0.0f;
    bool motionActive=false;
    float opacityStart=1.0f,opacityTarget=1.0f;
    float opacityElapsed=0.0f,opacityDuration=0.0f;
    bool opacityActive=false;
    int w{},h{};
    bool activeState=true;
    Style normal,active,pressed,disabled;
    Action cross;
    std::map<std::string,Action> buttonActions;
    bool enabled=true;
};
struct Object {
    std::string name,type,caption,filename,parent;
    float x{},y{},opacity=1.0f;
    float motionStartX{},motionStartY{},motionTargetX{},motionTargetY{};
    float motionElapsed=0.0f,motionDuration=0.0f;
    bool motionActive=false;
    float opacityStart=1.0f,opacityTarget=1.0f;
    float opacityElapsed=0.0f,opacityDuration=0.0f;
    bool opacityActive=false;
    int w{},h{};
    bool activeState=true;
    bool hCentered=false;
    Color3 color{210,210,210};
    float scale=0.5f;
};
struct Screen {
    std::string leaf;
    std::string backgroundType;
    std::string backgroundFile;
    std::string font;
    std::vector<Object> objects;
    std::vector<Control> controls;
    std::map<std::string,std::vector<const RdrNode*>> animations;
    std::map<std::string,std::vector<std::string>> events;
    RdrNode ast;
};

inline Color3 ReadColor(const RdrNode* n) {
    Color3 c{};
    if(!n)return c;
    std::vector<int> vals;
    for(const auto&v:n->values){
        if(auto i=AsInt(v))vals.push_back(*i);
        else if(auto f=AsFloat(v))vals.push_back((int)*f);
    }
    if(vals.size()>=3)c={vals[0],vals[1],vals[2]};
    return c;
}
inline Style ReadStyle(const RdrNode* spec,const std::string& key){
    Style s;
    auto n=FindPairNode(*spec,key); if(!n)return s;
    if(auto q=FindPairNode(*n,"SCALE"))s.scale=ScalarFloat(q,s.scale);
    if(auto q=FindPairNode(*n,"TEXT_COLOR"))s.text=ReadColor(q);
    if(auto q=FindPairNode(*n,"IMAGE"))s.image=ScalarString(q);
    return s;
}
inline std::map<std::string,Action> ReadButtonActions(const RdrNode* spec){
    std::map<std::string,Action> out;
    auto anims=FindPairNode(*spec,"ANIMATIONS");if(!anims)return out;
    for(const auto&v:anims->values){
        auto r=AsNode(v);if(!r)continue;
        auto button=ScalarString(FindPairNode(*r,"BUTTON"));
        if(button.empty())continue;
        Action a;
        a.animation=ScalarString(FindPairNode(*r,"ANIMATION"));
        a.argument=ScalarString(FindPairNode(*r,"ARGUMENT"));
        out[button]=a;
    }
    return out;
}
inline Action ReadCross(const RdrNode* spec){
    auto all=ReadButtonActions(spec);
    auto it=all.find("CROSS");
    return it==all.end()?Action{}:it->second;
}
inline void IndexAnimationsRecursive(const RdrNode& n,std::map<std::string,std::vector<const RdrNode*>>&out){
    auto name=FindPairNode(n,"ANIMATION_NAME");
    if(name){
        auto s=ScalarString(name);
        if(!s.empty()){
            for(std::size_t i=0;i+1<n.values.size();++i){
                auto k=AsString(n.values[i]);
                auto seq=AsNode(n.values[i+1]);
                if(k && *k=="SEQUENCE_DEFINITION" && seq) out[s].push_back(seq);
            }
        }
    }
    for(const auto&v:n.values)if(auto sub=AsNode(v))IndexAnimationsRecursive(*sub,out);
}
inline Screen ParseScreen(const std::string&leaf,RdrNode ast){
    Screen s;s.leaf=leaf;s.ast=std::move(ast);
    if(s.ast.values.empty())throw std::runtime_error("empty RDR");
    auto wrap=AsNode(s.ast.values[0]);if(!wrap)throw std::runtime_error("bad RDR root");
    auto screens=FindPairNode(*wrap,"SCREENS");if(!screens)throw std::runtime_error("SCREENS missing");

    if(auto bg=FindPairNode(*screens,"BACKGROUND")){
        s.backgroundType=ScalarString(FindPairNode(*bg,"TYPE"));
        s.backgroundFile=ScalarString(FindPairNode(*bg,"FILENAME"));
    }
    s.font=ScalarString(FindPairNode(*screens,"FONT"));

    if(auto evs=FindPairNode(*screens,"ANIMATIONS")){
        for(const auto&v:evs->values){
            auto n=AsNode(v);if(!n)continue;
            auto event=ScalarString(FindPairNode(*n,"EVENT"));
            auto anim=ScalarString(FindPairNode(*n,"ANIMATION"));
            if(!event.empty()&&!anim.empty())s.events[event].push_back(anim);
        }
    }

    if(auto objs=FindPairNode(*screens,"OBJECTS")){
        for(const auto&v:objs->values){
            auto n=AsNode(v);if(!n)continue;
            Object o;
            o.name=ScalarString(FindPairNode(*n,"NAME"));
            o.type=ScalarString(FindPairNode(*n,"TYPE"));
            o.x=static_cast<float>(ScalarInt(FindPairNode(*n,"XPOS")));
            o.y=static_cast<float>(ScalarInt(FindPairNode(*n,"YPOS")));
            o.parent=ScalarString(FindPairNode(*n,"CHILDOF"));
            if(auto spec=FindPairNode(*n,"SPEC")){
                o.caption=ScalarString(FindPairNode(*spec,"CAPTION"));
                o.filename=ScalarString(FindPairNode(*spec,"FILENAME"));
                o.w=ScalarInt(FindPairNode(*spec,"XSIZE"));
                o.h=ScalarInt(FindPairNode(*spec,"YSIZE"));
                o.scale=ScalarFloat(FindPairNode(*spec,"SCALE"),o.scale);
                if(auto q=FindPairNode(*spec,"COLOR"))o.color=ReadColor(q);
                o.hCentered=FindPairNode(*spec,"HCENTERED")!=nullptr;
                if(o.parent.empty())o.parent=ScalarString(FindPairNode(*spec,"CHILDOF"));
            }
            if(o.parent==o.name)o.parent.clear();
            s.objects.push_back(std::move(o));
        }
    }

    if(auto ctrls=FindPairNode(*screens,"CONTROLS")){
        for(const auto&v:ctrls->values){
            auto n=AsNode(v);if(!n)continue;
            Control c;
            c.name=ScalarString(FindPairNode(*n,"NAME"));
            c.type=ScalarString(FindPairNode(*n,"TYPE"));
            c.x=static_cast<float>(ScalarInt(FindPairNode(*n,"XPOS")));
            c.y=static_cast<float>(ScalarInt(FindPairNode(*n,"YPOS")));
            c.parent=ScalarString(FindPairNode(*n,"CHILDOF"));
            auto spec=FindPairNode(*n,"SPEC");
            if(spec){
                c.caption=ScalarString(FindPairNode(*spec,"CAPTION"));
                c.w=ScalarInt(FindPairNode(*spec,"XSIZE"));
                c.h=ScalarInt(FindPairNode(*spec,"YSIZE"));
                c.normal=ReadStyle(spec,"NORMAL");
                c.active=ReadStyle(spec,"ACTIVE");
                c.pressed=ReadStyle(spec,"PRESSED");
                c.disabled=ReadStyle(spec,"DISABLED");
                c.buttonActions=ReadButtonActions(spec);
                c.cross=ReadCross(spec);
                if(c.parent.empty())
                    c.parent=ScalarString(FindPairNode(*spec,"CHILDOF"));
            }
            if(c.parent==c.name)c.parent.clear();
            if(c.type=="BUTTON" && !c.caption.empty()){
                s.controls.push_back(std::move(c));
            }else if(c.type=="IMAGE" && spec){
                Object o;
                o.name=c.name;
                o.type="IMAGE";
                o.x=c.x;o.y=c.y;
                o.filename=ScalarString(FindPairNode(*spec,"FILENAME"));
                o.w=ScalarInt(FindPairNode(*spec,"XSIZE"));
                o.h=ScalarInt(FindPairNode(*spec,"YSIZE"));
                o.parent=ScalarString(FindPairNode(*spec,"CHILDOF"));
                if(o.parent==o.name)o.parent.clear();
                s.objects.push_back(std::move(o));
            }
        }
    }

    // Animation definitions are siblings of SCREENS under the RDR root.
    // Index the entire AST, not only the SCREENS subtree.
    IndexAnimationsRecursive(s.ast,s.animations);
    return s;
}

inline Control* FindControl(Screen&s,const std::string&name){for(auto&c:s.controls)if(c.name==name)return &c;return nullptr;}
inline Object* FindObject(Screen&s,const std::string&name){for(auto&o:s.objects)if(o.name==name)return &o;return nullptr;}

inline float SmoothStep01(float t){
    t=std::clamp(t,0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}
template<class T>
inline void AdvanceVisual(T&v,float dt){
    if(v.motionActive){
        v.motionElapsed+=dt;
        const float t=v.motionDuration<=0.0f?1.0f:
            SmoothStep01(v.motionElapsed/v.motionDuration);
        v.x=v.motionStartX+(v.motionTargetX-v.motionStartX)*t;
        v.y=v.motionStartY+(v.motionTargetY-v.motionStartY)*t;
        if(t>=1.0f)v.motionActive=false;
    }
    if(v.opacityActive){
        v.opacityElapsed+=dt;
        const float t=v.opacityDuration<=0.0f?1.0f:
            SmoothStep01(v.opacityElapsed/v.opacityDuration);
        v.opacity=v.opacityStart+(v.opacityTarget-v.opacityStart)*t;
        if(t>=1.0f)v.opacityActive=false;
    }
}
inline void AdvanceAnimations(Screen&s,float dt){
    for(auto&c:s.controls)AdvanceVisual(c,dt);
    for(auto&o:s.objects)AdvanceVisual(o,dt);
}

} // namespace
