#pragma once
#include "screen_model.hpp"
#include "retail_command_queue.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace socom::retail_ui {

struct VmResult {
    std::string switchMenu;
    bool flushHistory=false;
};

class UiVm {
public:
    std::map<std::string,std::int32_t> valves;
    RetailCommandStack commandStack;
    RetailNavigationState navigation;

    std::int32_t GetValve(const std::string& n) const {
        auto it=valves.find(n); return it==valves.end()?0:it->second;
    }

    VmResult Execute(const std::string& name, Screen& screen) {
        VmResult r;
        auto it=screen.animations.find(name);
        if(it==screen.animations.end()) return r;
        for(auto seq : it->second) {
            ExecuteSeq(*seq,screen,r,0);
            if(!r.switchMenu.empty()) break;
        }
        return r;
    }

private:
    static std::string S(const RdrNode* n){ return ScalarString(n); }

    static std::vector<float> Numbers(const RdrNode* n) {
        std::vector<float> out;
        if(!n) return out;
        for(const auto&v:n->values) {
            if(auto f=AsFloat(v)) out.push_back(*f);
            else if(auto i=AsInt(v)) out.push_back((float)*i);
        }
        return out;
    }

    bool EvalValve(const RdrNode& n) {
        if(n.values.size()<3) return false;
        auto name=AsString(n.values[0]);
        auto op=AsString(n.values[1]);
        int rhs=0;
        if(auto i=AsInt(n.values[2])) rhs=*i;
        else if(auto f=AsFloat(n.values[2])) rhs=(int)*f;
        if(!name||!op)return false;
        int lhs=GetValve(*name);
        if(*op=="==")return lhs==rhs;
        if(*op=="!=")return lhs!=rhs;
        if(*op==">")return lhs>rhs;
        if(*op=="<")return lhs<rhs;
        if(*op==">=")return lhs>=rhs;
        if(*op=="<=")return lhs<=rhs;
        return false;
    }

    void SetValve(const RdrNode& n) {
        if(n.values.size()<3)return;
        auto name=AsString(n.values[0]);
        auto op=AsString(n.values[1]);
        if(!name||!op||*op!="=")return;
        int v=0;
        if(auto i=AsInt(n.values[2]))v=*i;
        else if(auto f=AsFloat(n.values[2]))v=(int)*f;
        valves[*name]=v;
    }

    static std::string ObjectName(const RdrNode& arg) {
        auto n=FindPairNode(arg,"NAME");
        if(!n) return {};
        std::string out;
        for(const auto&v:n->values) if(auto s=AsString(v)) out=*s;
        return out;
    }

    void SetControl(Screen& s,const std::string& name,const std::string& cmd) {
        if(auto*c=FindControl(s,name)) {
            if(cmd=="DisableButton") c->enabled=false;
            else if(cmd=="EnableButton") c->enabled=true;
            else if(cmd=="ActivateButton") c->activeState=true;
        }
    }

    void ExecuteSeq(const RdrNode& seq, Screen& screen, VmResult& out, int depth) {
        if(depth>24)return;
        struct IfState{bool parent=true,taken=false,active=true;};
        std::vector<IfState> stack;
        bool active=true;

        for(std::size_t i=0;i+1<seq.values.size();i+=2) {
            auto key=AsString(seq.values[i]);
            auto arg=AsNode(seq.values[i+1]);
            if(!key||!arg)continue;

            if(*key=="IF") {
                bool cond=false;
                if(arg->values.size()>=2) {
                    auto k=AsString(arg->values[0]); auto n=AsNode(arg->values[1]);
                    if(k&&*k=="VALVE"&&n)cond=EvalValve(*n);
                }
                stack.push_back({active,cond,active&&cond});
                active=stack.back().active; continue;
            }
            if(*key=="ELSEIF") {
                if(stack.empty())continue;
                auto &st=stack.back(); bool cond=false;
                if(arg->values.size()>=2) {
                    auto k=AsString(arg->values[0]); auto n=AsNode(arg->values[1]);
                    if(k&&*k=="VALVE"&&n)cond=EvalValve(*n);
                }
                st.active=st.parent && !st.taken && cond;
                if(cond)st.taken=true;
                active=st.active; continue;
            }
            if(*key=="ELSE") {
                if(stack.empty())continue;
                auto &st=stack.back();
                st.active=st.parent && !st.taken;
                st.taken=true;
                active=st.active; continue;
            }
            if(*key=="ENDIF") {
                if(!stack.empty()) {
                    auto st=stack.back();
                    stack.pop_back();
                    active=st.parent;
                }
                continue;
            }
            if(!active)continue;

            if(*key=="WAIT") {
                // Synchronous VM: stop this sequence at long delayed continuations.
                // Nested callers continue their own immediate sequence.
                const float seconds=ScalarFloat(arg,0.0f);
                if(seconds>1.0f) return;
                continue;
            }
            if(*key=="VALVE") { SetValve(*arg); continue; }

            if(*key=="CALL_ANIMATION") {
                auto nm=S(FindPairNode(*arg,"NAME"));
                auto it=screen.animations.find(nm);
                if(it!=screen.animations.end()) {
                    for(auto sub : it->second) {
                        ExecuteSeq(*sub,screen,out,depth+1);
                        if(!out.switchMenu.empty())return;
                    }
                }
                continue;
            }

            if(*key=="OBJECT_ACTIVE_STATE") {
                auto name=ObjectName(*arg);
                auto state=S(FindPairNode(*arg,"STATE"));
                bool on=state!="INACTIVE";
                if(auto*c=FindControl(screen,name)) c->activeState=on;
                if(auto*o=FindObject(screen,name)) o->activeState=on;
                continue;
            }

            if(*key=="OBJECT_MOTION_FROM_TO") {
                auto name=ObjectName(*arg);
                auto from=Numbers(FindPairNode(*arg,"TRANSLATE_FROM"));
                auto to=Numbers(FindPairNode(*arg,"TRANSLATE_TO"));
                const float duration=ScalarFloat(FindPairNode(*arg,"RUN_TIME"),0.0f);
                if(to.size()>=2) {
                    auto start=[&](auto* item){
                        if(!item)return;
                        item->motionStartX=from.size()>=2?from[0]:item->x;
                        item->motionStartY=from.size()>=2?from[1]:item->y;
                        item->x=item->motionStartX;
                        item->y=item->motionStartY;
                        item->motionTargetX=to[0];
                        item->motionTargetY=to[1];
                        item->motionElapsed=0.0f;
                        item->motionDuration=duration;
                        item->motionActive=duration>0.0f;
                        if(duration<=0.0f){item->x=to[0];item->y=to[1];}
                    };
                    start(FindControl(screen,name));
                    start(FindObject(screen,name));
                }
                continue;
            }

            if(*key=="OBJECT_OPACITY_FROM_TO") {
                auto name=ObjectName(*arg);
                auto from=Numbers(FindPairNode(*arg,"OPACITY_FROM"));
                auto to=Numbers(FindPairNode(*arg,"OPACITY_TO"));
                const float duration=ScalarFloat(FindPairNode(*arg,"RUN_TIME"),0.0f);
                if(!to.empty()) {
                    auto start=[&](auto* item){
                        if(!item)return;
                        item->opacityStart=!from.empty()?from[0]:item->opacity;
                        item->opacity=item->opacityStart;
                        item->opacityTarget=to[0];
                        item->opacityElapsed=0.0f;
                        item->opacityDuration=duration;
                        item->opacityActive=duration>0.0f;
                        if(duration<=0.0f)item->opacity=to[0];
                    };
                    start(FindControl(screen,name));
                    start(FindObject(screen,name));
                }
                continue;
            }

            if(*key=="ui::UI_COMMAND") {
                auto type=S(FindPairNode(*arg,"TYPE"));
                auto argument=S(FindPairNode(*arg,"ARGUMENT"));

                if(type=="SWITCHMENU"){
                    navigation.QueueSwitch(commandStack,argument,screen.leaf);
                    return;
                }
                if(type=="FlushMenuHistory"){
                    navigation.Flush();
                    out.flushHistory=true;
                    continue;
                }
                if(type=="DisableButton"){
                    commandStack.Push(RetailMenuCommandType::DisableButton,argument);
                    continue;
                }
                if(type=="EnableButton"){
                    commandStack.Push(RetailMenuCommandType::EnableButton,argument);
                    continue;
                }
                if(type=="ActivateButton"){
                    commandStack.Push(RetailMenuCommandType::ActivateButton,argument);
                    continue;
                }
                continue;
            }
        }
    }
};

} // namespace socom::retail_ui
