#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace socom::retail_ui {

// SCUS_971.34 storage at 0x004B3450:
//   0x000..0x81F = 40 * 0x34-byte records
//   0x820         = pending command count
//   0x824         = current/top record index
//   0x828         = active menu-history vector
//
// Retail handlers increment topIndex before writing a command.
// The consumer at 0x0039C470 always executes records[topIndex] first.
// This is therefore a LIFO command stack, not FIFO.
struct RetailMenuCommandRecord {
    std::uint32_t type=0;
    char text[48]{};
};
static_assert(sizeof(RetailMenuCommandRecord)==0x34);

enum class RetailMenuCommandType : std::uint32_t {
    ForwardSwitch  = 2,
    BackSwitch     = 5,
    ActivateButton = 6,
    EnableButton   = 7,
    DisableButton  = 8
};

class RetailCommandStack {
public:
    static constexpr int kCapacity=40;

    bool Push(RetailMenuCommandType type,const std::string&text){
        if(pending_>=kCapacity || topIndex_>=kCapacity-1)return false;
        ++topIndex_;
        auto&r=records_[static_cast<std::size_t>(topIndex_)];
        r={};
        r.type=static_cast<std::uint32_t>(type);
        std::strncpy(r.text,text.c_str(),sizeof(r.text)-1);
        ++pending_;
        return true;
    }

    bool Pop(RetailMenuCommandRecord&out){
        if(pending_<=0 || topIndex_<0)return false;
        out=records_[static_cast<std::size_t>(topIndex_)];
        records_[static_cast<std::size_t>(topIndex_)]={};
        --topIndex_;
        --pending_;
        return true;
    }

    void Clear(){
        records_={};
        topIndex_=-1;
        pending_=0;
    }

    int Pending()const{return pending_;}
    int TopIndex()const{return topIndex_;}

private:
    std::array<RetailMenuCommandRecord,kCapacity> records_{};
    int topIndex_=-1;
    int pending_=0;
};

// Native equivalent of the active history vector at retail +0x828.
// The retail build uses pooled char buffers and a free-list. We preserve its
// observable screen-history behavior with owned std::strings.
class RetailNavigationState {
public:
    static constexpr const char* kPreviousScreen="PREVIOUS_SCREEN";

    bool QueueSwitch(RetailCommandStack&commands,
                     const std::string&requested,
                     const std::string&currentScreen)
    {
        if(requested.empty())return false;

        // Exact PREVIOUS_SCREEN behavior: target the active-history back entry
        // and remove it before the command is consumed.
        if(requested==kPreviousScreen){
            if(history_.empty())return false;
            const std::string target=history_.back();
            history_.pop_back();
            return commands.Push(RetailMenuCommandType::BackSwitch,target);
        }

        // Retail SwitchMenu also recognizes an explicit request for the
        // immediate previous screen and treats it as a back-switch, avoiding a
        // duplicate current/previous pair.
        if(!history_.empty() && requested==history_.back()){
            const std::string target=history_.back();
            history_.pop_back();
            return commands.Push(RetailMenuCommandType::BackSwitch,target);
        }

        // Normal transition.  The current screen is pushed when command type 2
        // is consumed, matching 0x0039C55C -> 0x001D6DA0.
        return commands.Push(RetailMenuCommandType::ForwardSwitch,requested);
    }

    void OnConsumeForward(const std::string&currentScreen){
        if(!currentScreen.empty())history_.push_back(currentScreen);
    }

    void Flush(){history_.clear();}
    bool Empty()const{return history_.empty();}
    std::size_t Size()const{return history_.size();}
    const std::string& Back()const{return history_.back();}

private:
    std::vector<std::string> history_;
};

} // namespace socom::retail_ui
