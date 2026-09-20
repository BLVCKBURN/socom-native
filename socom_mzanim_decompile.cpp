// socom_mzanim_decompile.cpp
// First-stage decompiler for SOCOM 1 GameZ mzanim.zar mission choreography.
//
// This tool parses the ZAR tree, resolves each animation's local Name_Index_Table,
// walks Seq_Data blocks, names each sequence block, splits command records, and
// prints a readable pseudo-script. A growing set of compact commands has typed
// operand decoding; unknown/large commands are preserved as raw bytes.
//
// Build:
//   macOS/Linux:
//     clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic socom_mzanim_decompile.cpp -o socom_mzanim_decompile
//
//   Windows Developer Command Prompt:
//     cl /std:c++17 /EHsc /O2 socom_mzanim_decompile.cpp
//
// Usage:
//   socom_mzanim_decompile mzanim.zar
//   socom_mzanim_decompile mzanim.zar output.txt
//   socom_mzanim_decompile mzanim.zar output.txt objectives
//
// The third argument optionally limits output to one Animation_List entry.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma pack(push,1)
struct ZarHeader {
    std::uint32_t unknown00;
    std::uint32_t nodeCount;
    std::uint32_t stringBlockSize;
    std::uint32_t preferredBase;
    std::uint32_t alignment;
    std::uint8_t  unknown14[0x40];
    std::uint32_t payloadSize;
    std::uint32_t unknown58;
    std::uint32_t unknown5C;
    std::uint32_t version;
};
struct ZarNodeDisk {
    std::uint32_t nameAddress;
    std::uint32_t dataOffset;
    std::uint32_t dataSize;
    std::uint32_t childCount;
};
#pragma pack(pop)

static_assert(sizeof(ZarHeader) == 0x64);
static_assert(sizeof(ZarNodeDisk) == 0x10);

template<class T>
static T rd(const std::uint8_t* p) {
    T v{};
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static std::size_t alignUp(std::size_t v, std::size_t a) {
    if (!a) throw std::runtime_error("zero alignment");
    const auto r = v % a;
    return r ? v + (a-r) : v;
}

struct Node {
    std::string name;
    std::uint32_t dataOffset{};
    std::uint32_t dataSize{};
    std::vector<Node> children;
};

class Zar {
public:
    explicit Zar(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + path);
        f.seekg(0, std::ios::end);
        const auto end = f.tellg();
        f.seekg(0, std::ios::beg);
        if (end < static_cast<std::streamoff>(sizeof(ZarHeader)))
            throw std::runtime_error("file too small");
        bytes_.resize(static_cast<std::size_t>(end));
        f.read(reinterpret_cast<char*>(bytes_.data()),
               static_cast<std::streamsize>(bytes_.size()));
        if (!f) throw std::runtime_error("read failed");

        std::memcpy(&h_, bytes_.data(), sizeof(h_));
        if (h_.version != 0x00020002)
            throw std::runtime_error("unexpected ZAR version");

        stringOff_ = sizeof(ZarHeader);
        nodeOff_ = stringOff_ + h_.stringBlockSize;
        const std::uint64_t nodeBytes =
            static_cast<std::uint64_t>(h_.nodeCount) * sizeof(ZarNodeDisk);
        if (nodeOff_ + nodeBytes > bytes_.size())
            throw std::runtime_error("node table outside file");

        payloadOff_ = alignUp(
            static_cast<std::size_t>(nodeOff_ + nodeBytes), h_.alignment);
        if (payloadOff_ + h_.payloadSize != bytes_.size())
            throw std::runtime_error("payload does not end at EOF");

        disk_.resize(h_.nodeCount);
        std::memcpy(disk_.data(), bytes_.data()+nodeOff_,
                    static_cast<std::size_t>(nodeBytes));

        std::size_t cursor = 0;
        root_ = build(cursor);
        if (cursor != disk_.size())
            throw std::runtime_error("node tree did not consume all nodes");
    }

    const Node& root() const { return root_; }

    std::vector<std::uint8_t> payload(const Node& n) const {
        const std::uint64_t end =
            static_cast<std::uint64_t>(n.dataOffset) + n.dataSize;
        if (end > h_.payloadSize)
            throw std::runtime_error("node payload outside archive");
        return std::vector<std::uint8_t>(
            bytes_.begin()+payloadOff_+n.dataOffset,
            bytes_.begin()+payloadOff_+n.dataOffset+n.dataSize);
    }

private:
    std::string nodeName(std::uint32_t addr) const {
        if (!addr) return "<root>";
        if (addr < h_.preferredBase)
            throw std::runtime_error("invalid node name address");
        const std::uint64_t rel =
            static_cast<std::uint64_t>(addr) - h_.preferredBase;
        if (rel >= h_.stringBlockSize)
            throw std::runtime_error("node name outside string block");
        std::size_t p = stringOff_ + static_cast<std::size_t>(rel);
        const std::size_t lim = stringOff_ + h_.stringBlockSize;
        std::size_t e = p;
        while (e < lim && bytes_[e]) ++e;
        if (e == lim) throw std::runtime_error("unterminated node name");
        return std::string(
            reinterpret_cast<const char*>(bytes_.data()+p), e-p);
    }

    Node build(std::size_t& cursor) {
        if (cursor >= disk_.size())
            throw std::runtime_error("node tree overrun");
        const auto d = disk_[cursor++];
        Node n;
        n.name = nodeName(d.nameAddress);
        n.dataOffset = d.dataOffset;
        n.dataSize = d.dataSize;
        n.children.reserve(d.childCount);
        for (std::uint32_t i=0; i<d.childCount; ++i)
            n.children.push_back(build(cursor));
        return n;
    }

    std::vector<std::uint8_t> bytes_;
    ZarHeader h_{};
    std::vector<ZarNodeDisk> disk_;
    Node root_;
    std::size_t stringOff_{}, nodeOff_{}, payloadOff_{};
};

static const Node* child(const Node& n, const std::string& name) {
    for (const auto& c : n.children)
        if (c.name == name) return &c;
    return nullptr;
}

static const char* commandName(std::uint8_t id) {
    // Registration order recovered from SCUS_971.34.
    static const std::array<const char*,102> names = {{
        "QUAD_ALIGN","IF","ELSEIF","ELSE","ENDIF","NODE_ACTIVE","NODE_RENDERED",
        "RANGE_TEST","RANDOM_WEIGHT","FAIL","ANIM_HEALTH","ANIM_LOD","LOOP","WAIT",
        "OBJECT_BLENDMODE","OBJECT_ACTIVE_STATE","OBJECT_TRANSLATE_STATE",
        "OBJECT_ROTATE_STATE","OBJECT_MOTION","OBJECT_MOTION_FROM_TO",
        "OBJECT_OPACITY_FROM_TO","OBJECT_OPACITY_FROM_TO","OBJECT_MOTION_SI_SCRIPT",
        "PARTICLE_SOURCE","CAMERA","DESTRUCTION_SOURCE","SOUND","LIGHT","WHILE",
        "END_WHILE","EXPRESSION","BREAK","CALL_ANIMATION","STOP_ANIMATION",
        "PAUSE_ANIMATION","RESUME_ANIMATION","INVALIDATE_ANIMATION","CALL_SEQUENCE",
        "STOP_SEQUENCE","DEBUG","OBJECT_ADD_CHILD","OBJECT_DELETE_CHILD",
        "OBJECT_ADD_CHILD","OBJECT_DELETE_CHILD","MESSAGE","TIMER","FIRE_WEAPON",
        "REMOVE_SATCHELS","ui::UI_COMMAND","ui::UI_APP_COMMAND",
        "DYNAMICS_RELEASE_CAMERA","DYNAMICS_ACQUIRE_CAMERA","CAMERA_INDOORS",
        "SET_CAMERA_REGION_TEST","GET_CAMERA_REGION_TEST","CAMERA_PARAMS",
        "START_IMAGE_RECORDER","STOP_IMAGE_RECORDER","HUD_ON","HUD_OFF",
        "HUD_LETTERBOX_ON","HUD_LETTERBOX_OFF","HUD_TRIGGER_MISSION_CAM",
        "HUD_START_DEFUSE_TIMER","HUD_STOP_DEFUSE_TIMER","HUD_TRIGGER_POPUP",
        "PLAYER_INDOORS","PLAYER_CONTROLS","IS_PLAYER_NODE","ai::ACTION","ai::COMM",
        "ai::DEBUG","ai::DOOR","ai::EVENT","ai::FIREMODE","ai::FIREWEAPON","ai::GOTO",
        "ai::HOLD","ai::INSTATE","ai::INVIEW","ai::LOOKAT","ai::MACRO","ai::MAP",
        "ai::MOVE","ai::PAINT","ai::PARAM","ai::PURSUE","ai::ANIMATE","ai::INRANGE",
        "ai::INREGION","ai::SELECT","ai::SETMODE","ai::SETANIMSET","ai::SIGNAL",
        "ai::SOUND","ai::STANCE","ai::STATE","ai::STOPALL","ai::SPECIAL","ai::TEAM",
        "ai::VALVE","ai::WAIT"
    }};
    if (id == 0 || id > names.size()) return "UNKNOWN";
    return names[id-1];
}

static std::string hexBytes(const std::uint8_t* p, std::size_t n) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (std::size_t i=0; i<n; ++i) {
        if (i) ss << ' ';
        ss << std::setw(2) << static_cast<unsigned>(p[i]);
    }
    return ss.str();
}

static std::string quoted(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    o += '"';
    return o;
}

static std::string localName(
    const std::vector<std::string>& localNames,
    std::uint16_t index)
{
    if (index < localNames.size())
        return localNames[index];
    return "<name_index_" + std::to_string(index) + ">";
}

static const char* timerComparison(std::uint32_t mode) {
    // Jump table at SCUS_971.34:0x462190, used by tick handler 0x1C0D00.
    static const std::array<const char*,7> ops =
        {{"==", "!=", ">=", "<=", ">", "<", "SET"}};
    return mode < ops.size() ? ops[mode] : nullptr;
}

static std::string soundFlags(std::uint16_t flags) {
    struct Entry { std::uint16_t bit; const char* name; };
    static const std::array<Entry,12> entries = {{
        {0x0001,"ALT_NAME_LOOKUP"}, {0x0002,"AT_NODE"},
        {0x0004,"TRANSLATION"},     {0x0008,"INPUT_TRANSLATION"},
        {0x0010,"VOLUME"},          {0x0020,"PAN"},
        {0x0040,"PITCH"},           {0x0080,"START"},
        {0x0100,"STOP"},            {0x0200,"STOP_ON_EXIT"},
        {0x0400,"MUSIC_LOCAL"},     {0x0800,"MUSIC_EVENT"}
    }};
    std::ostringstream ss;
    bool first = true;
    for (const auto& e : entries) {
        if (!(flags & e.bit)) continue;
        if (!first) ss << '|';
        ss << e.name;
        first = false;
    }
    const auto unknown = static_cast<std::uint16_t>(flags & ~0x0FFFu);
    if (unknown) {
        if (!first) ss << '|';
        ss << "UNKNOWN_0x" << std::hex << unknown << std::dec;
        first = false;
    }
    return first ? "NONE" : ss.str();
}

static std::string cameraModes(std::uint32_t control) {
    static const std::array<const char*,8> properties = {{
        "H_FOV", "V_FOV", "NEAR_CLIP", "MID_CLIP", "FAR_CLIP",
        "FOG_NEAR", "FOG_FAR", "FOG_COLOR"
    }};
    std::ostringstream ss;
    bool first = true;
    for (std::size_t i=0; i<properties.size(); ++i) {
        const auto mode = static_cast<unsigned>((control >> (i*2)) & 3u);
        if (!mode) continue;
        if (!first) ss << ',';
        ss << properties[i] << ':' << mode;
        first = false;
    }
    const auto unknown = control & 0xFFFF0000u;
    if (unknown) {
        if (!first) ss << ',';
        ss << "UNKNOWN:0x" << std::hex << unknown << std::dec;
        first = false;
    }
    return first ? "NONE" : ss.str();
}

static bool closesBefore(std::uint8_t id) {
    return id==3 || id==4 || id==5 || id==30;
}
static bool opensAfter(std::uint8_t id) {
    return id==2 || id==3 || id==4 || id==29;
}

static std::string decodeCommand(
    std::uint8_t id,
    const std::uint8_t* rec,
    std::size_t size,
    const std::vector<std::string>& localNames)
{
    std::ostringstream ss;
    ss << commandName(id);

    // Compact command layouts independently supported by the executable
    // parser and M8 serialized records.
    switch (id) {
    case 2: // IF
    case 3: // ELSEIF
        if (size >= 8) {
            const auto expressionMode = rd<std::uint32_t>(rec+4);
            ss << " expression_mode=" << expressionMode;
            std::size_t p = 8;
            std::uint32_t termCount=0;
            while (p+4<=size && rd<std::uint32_t>(rec+p)!=0xFFFFFFFFu) {
                const auto nestedOpcode = rd<std::uint16_t>(rec+p);
                const auto nestedMeta   = rd<std::uint16_t>(rec+p+2);
                const auto nestedId = static_cast<std::uint8_t>(nestedOpcode & 0xFF);
                const auto nestedSize =
                    static_cast<std::size_t>((nestedMeta & 0xFFFC) >> 2);
                if (nestedSize < 4 || p+nestedSize > size) {
                    ss << " malformed_term=" << termCount
                       << " trailing=" << hexBytes(rec+p, size-p);
                    return ss.str();
                }
                ss << " {op=" << static_cast<unsigned>(nestedId) << ' '
                   << decodeCommand(
                    nestedId, rec+p, nestedSize, localNames) << '}';
                p += nestedSize;
                ++termCount;
            }
            if (p < size) {
                // Compiler-generated diagnostics use 0xFFFFFFFF followed by
                // an aligned NUL-terminated string.
                if (p+4 <= size && rd<std::uint32_t>(rec+p)==0xFFFFFFFFu) {
                    p += 4;
                    std::size_t end=p;
                    while (end<size && rec[end]) ++end;
                    if (end>p)
                        ss << " message=" << quoted(std::string(
                            reinterpret_cast<const char*>(rec+p), end-p));
                    p = end<size ? end+1 : end;
                }
                while (p<size && rec[p]==0) ++p;
                if (p<size)
                    ss << " trailing=" << hexBytes(rec+p, size-p);
            }
            return ss.str();
        }
        break;

    case 16: // OBJECT_ACTIVE_STATE
        if (size >= 8) {
            const auto state = rd<std::uint16_t>(rec+4);
            const auto name  = rd<std::uint16_t>(rec+6);
            ss << " target=" << quoted(localName(localNames,name))
               << " state=" << (state ? "ON" : "OFF")
               << " (" << state << ")";
            return ss.str();
        }
        break;

    case 9: // RANDOM_WEIGHT
        if (size == 8) {
            ss << " weight=" << rd<float>(rec+4);
            return ss.str();
        }
        break;

    case 8: // RANGE_TEST
        if (size >= 20) {
            ss << " flags=0x" << std::hex << rd<std::uint32_t>(rec+4)
               << std::dec << " radius=" << rd<float>(rec+8)
               << " reference_type=" << rd<std::uint32_t>(rec+12)
               << " reference_payload=0x" << std::hex
               << rd<std::uint32_t>(rec+16) << std::dec;
            if (size>20) ss << " trailing=" << hexBytes(rec+20,size-20);
            return ss.str();
        }
        break;

    case 13: // LOOP
        if (size == 12) {
            ss << " mode=" << rd<std::uint32_t>(rec+4)
               << " count=" << rd<std::int32_t>(rec+8);
            return ss.str();
        }
        if (size >= 17) {
            const auto mode = rd<std::uint32_t>(rec+4);
            const auto arg  = rd<std::uint32_t>(rec+8);
            const auto enabled = rd<std::uint32_t>(rec+12);
            std::size_t end=16;
            while (end<size && rec[end]) ++end;
            ss << " mode=" << mode << " argument=" << arg
               << " enabled=" << enabled;
            if (end>16)
                ss << " sequence=" << quoted(std::string(
                    reinterpret_cast<const char*>(rec+16),end-16));
            std::size_t p=end<size ? end+1 : end;
            while (p<size && rec[p]==0) ++p;
            if (p<size) ss << " trailing=" << hexBytes(rec+p,size-p);
            return ss.str();
        }
        break;

    case 14: // WAIT
        if (size >= 12) {
            const auto mode = rd<std::uint32_t>(rec+4);
            ss << " mode=0x" << std::hex << mode << std::dec;
            if (mode == 0x09)
                ss << " seconds=" << rd<float>(rec+8);
            else if (mode == 0x10)
                ss << " frames=" << rd<std::int32_t>(rec+8);
            else
                ss << " value0=0x" << std::hex
                   << rd<std::uint32_t>(rec+8) << std::dec;
            if (size>12) ss << " trailing=" << hexBytes(rec+12,size-12);
            return ss.str();
        }
        break;

    case 46: // TIMER
        // Serialized M8 layout (16 bytes total):
        //   +0x04 u32 value payload
        //   +0x08 u32 comparison-mode payload
        //   +0x0C u8  value operand tag
        //   +0x0D u8  mode operand tag
        // The runtime command is 12 bytes: mode at +4 and float value at +8.
        // The loader evaluates/converts the serialized operands into that form.
        if (size >= 16) {
            const auto valuePayload = rd<std::uint32_t>(rec+4);
            const auto modePayload  = rd<std::uint32_t>(rec+8);
            const auto valueTag     = rec[12];
            const auto modeTag      = rec[13];
            const auto op           = timerComparison(modePayload);

            if (op) {
                if (modePayload == 6)
                    ss << " value=" << valuePayload;
                else
                    ss << " test " << op << ' ' << valuePayload;
            }
            else {
                ss << " value_payload=" << valuePayload
                   << " mode_payload=" << modePayload;
            }

            // Tags 0x07/0x02 are the normal M8 literal/mode encoding. Keep
            // non-canonical tags visible until the generic operand evaluator
            // is fully recovered; this preserves dynamic/symbolic records.
            if (valueTag != 0x07 || modeTag != 0x02)
                ss << " operand_tags=[0x" << std::hex
                   << static_cast<unsigned>(valueTag) << ",0x"
                   << static_cast<unsigned>(modeTag) << std::dec << ']';
            return ss.str();
        }
        break;

    case 27: // SOUND
        // Parser 0x1B56A0 creates a 0x20-byte base command. Seq_Data may
        // truncate unused trailing defaults or append command-specific data.
        if (size >= 8) {
            const auto flags   = rd<std::uint16_t>(rec+4);
            const auto soundId = rec[6];
            ss << " sound_id=" << static_cast<unsigned>(soundId)
               << " flags=" << soundFlags(flags);

            if ((flags & 0x0002) && size >= 17)
                ss << " node_id=" << static_cast<unsigned>(rec[16]);
            if ((flags & 0x0010) && size >= 12)
                ss << " volume=" << rd<float>(rec+8);
            if ((flags & 0x0020) && size >= 14)
                ss << " pan=" << rd<std::uint16_t>(rec+12);
            if ((flags & 0x0040) && size >= 16)
                ss << " pitch_raw=" << rd<std::int16_t>(rec+14);
            if ((flags & 0x0004) && size >= 32)
                ss << " translation=(" << rd<float>(rec+20) << ','
                   << rd<float>(rec+24) << ',' << rd<float>(rec+28) << ')';

            // Compact M8 records retain two nonzero words even when their
            // corresponding option bits are clear. Their loader semantics are
            // not yet proven, so expose them explicitly instead of guessing.
            if (size >= 16) {
                const auto field08 = rd<std::uint32_t>(rec+8);
                const auto field0C = rd<std::uint32_t>(rec+12);
                if (!(flags & 0x0010) && field08)
                    ss << " field08_raw=0x" << std::hex << field08 << std::dec;
                if (!(flags & (0x0020|0x0040)) && field0C)
                    ss << " field0C_raw=0x" << std::hex << field0C << std::dec;
            }
            if (size > 32)
                ss << " appended_data=" << hexBytes(rec+32, size-32);
            return ss.str();
        }
        break;

    case 25: // CAMERA
        if (size >= 16) {
            const auto control = rd<std::uint32_t>(rec+4);
            if (control == 0) {
                const auto action = rd<std::uint32_t>(rec+8);
                ss << " action_id=" << action;
                if (size > 16) {
                    std::size_t end = 12;
                    while (end < size && rec[end]) ++end;
                    if (end > 12)
                        ss << " name=" << quoted(std::string(
                            reinterpret_cast<const char*>(rec+12), end-12));
                    if (end < size) ++end;
                    if (end < size)
                        ss << " trailing=" << hexBytes(rec+end, size-end);
                }
                return ss.str();
            }

            ss << " control=0x" << std::hex << control << std::dec
               << " modes=[" << cameraModes(control) << ']';
            if (size > 8) {
                ss << " payload=[";
                bool first = true;
                std::size_t p = 8;
                for (; p+4 <= size; p+=4) {
                    if (!first) ss << ',';
                    ss << rd<float>(rec+p);
                    first = false;
                }
                ss << ']';
                if (p < size)
                    ss << " trailing=" << hexBytes(rec+p, size-p);
            }
            return ss.str();
        }
        break;

    case 26: // DESTRUCTION_SOURCE
        if (size >= 17) {
            std::size_t p=16;
            std::size_t end=p;
            while (end<size && rec[end]) ++end;
            const std::string node(
                reinterpret_cast<const char*>(rec+p), end-p);
            p = end<size ? end+1 : end;
            end=p;
            while (end<size && rec[end]) ++end;
            const std::string texture(
                reinterpret_cast<const char*>(rec+p), end-p);

            ss << " node=" << (node.empty() ? "<current>" : quoted(node));
            if (!texture.empty()) ss << " texture=" << quoted(texture);
            ss << " prefix=" << hexBytes(rec+4, 12);
            p = end<size ? end+1 : end;
            while (p<size && rec[p]==0) ++p;
            if (p<size) ss << " trailing=" << hexBytes(rec+p, size-p);
            return ss.str();
        }
        break;

    case 41: // OBJECT_ADD_CHILD (compact runtime form)
        if (size >= 8) {
            const auto flags = rd<std::uint16_t>(rec+4);
            ss << " parent_id=" << static_cast<unsigned>(rec[6])
               << " child_id=" << static_cast<unsigned>(rec[7])
               << " retain_world_location=" << ((flags & 1) ? "true" : "false");
            if (flags & ~1u)
                ss << " unknown_flags=0x" << std::hex
                   << static_cast<unsigned>(flags & ~1u) << std::dec;
            return ss.str();
        }
        break;

    case 33: // CALL_ANIMATION
    case 34: // STOP_ANIMATION
    case 35: // PAUSE_ANIMATION
    case 36: // RESUME_ANIMATION
    case 37: // INVALIDATE_ANIMATION
    case 38: // CALL_SEQUENCE
    case 39: // STOP_SEQUENCE
        if (size >= 6) {
            const auto name = rd<std::uint16_t>(rec+4);
            ss << ' ' << quoted(localName(localNames,name));
            return ss.str();
        }
        break;

    case 4:  // ELSE
    case 5:  // ENDIF
    case 10: // FAIL
    case 32: // BREAK
        return ss.str();

    case 30: // END_WHILE at top level; opcode 30 is also used in expressions
        if (size == 4) return ss.str();
        break;

    default:
        break;
    }

    // Keep every unknown operand byte visible so the decompiler is lossless
    // while more command layouts are recovered.
    if (size > 4)
        ss << "  ; raw_args: " << hexBytes(rec+4, size-4);
    return ss.str();
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 4) {
            std::cerr
                << "Usage: socom_mzanim_decompile mzanim.zar "
                   "[output.txt] [animation_name]\n";
            return 1;
        }

        Zar zar(argv[1]);

        const Node* animSets = child(zar.root(), "Anim_Sets");
        if (!animSets)
            throw std::runtime_error("Anim_Sets not found");

        const Node* mission = child(*animSets, "mission");
        if (!mission) {
            if (animSets->children.empty())
                throw std::runtime_error("no animation set found");
            mission = &animSets->children.front();
        }

        const Node* globalNameTable = child(*mission, "Name_Table");
        const Node* animationList   = child(*mission, "Animation_List");
        if (!globalNameTable || !animationList)
            throw std::runtime_error("Name_Table/Animation_List missing");

        std::vector<std::string> globalNames;
        globalNames.reserve(globalNameTable->children.size());
        for (const auto& n : globalNameTable->children)
            globalNames.push_back(n.name);

        std::ofstream file;
        std::ostream* osp = &std::cout;
        if (argc >= 3 && std::string(argv[2]) != "-") {
            file.open(argv[2], std::ios::binary);
            if (!file) throw std::runtime_error("cannot create output");
            osp = &file;
        }
        auto& os = *osp;

        const std::string only = argc == 4 ? argv[3] : "";

        std::size_t animationCount=0, sequenceCount=0, commandCount=0;

        for (const auto& anim : animationList->children) {
            if (!only.empty() && anim.name != only)
                continue;

            const Node* nit = child(anim, "Name_Index_Table");
            const Node* seq = child(anim, "Seq_Data");
            if (!nit || !seq)
                continue;

            const auto nitBytes = zar.payload(*nit);
            if (nitBytes.size() % 2)
                throw std::runtime_error("odd Name_Index_Table size");

            std::vector<std::string> localNames;
            for (std::size_t p=0; p<nitBytes.size(); p+=2) {
                const auto index = rd<std::uint16_t>(nitBytes.data()+p);
                if (index >= globalNames.size())
                    throw std::runtime_error("Name_Index_Table index out of range");
                localNames.push_back(globalNames[index]);
            }

            const auto data = zar.payload(*seq);

            os << "\nANIMATION " << quoted(anim.name) << " {\n";
            ++animationCount;

            std::size_t off=0;
            std::size_t seqOrdinal=0;
            while (off < data.size()) {
                if (off+28 > data.size())
                    throw std::runtime_error("short Seq_Data block");

                const auto labelIndex = rd<std::uint32_t>(data.data()+off+0x00);
                const auto blockFlags = rd<std::uint32_t>(data.data()+off+0x04);
                const auto blockSize  = rd<std::uint32_t>(data.data()+off+0x0C);

                if (blockSize < 28 || off+blockSize > data.size())
                    throw std::runtime_error("bad Seq_Data block size");

                std::string label;
                if (labelIndex < localNames.size())
                    label = localNames[labelIndex];
                else
                    label = "<sequence_" + std::to_string(seqOrdinal) + ">";

                os << "  SEQUENCE " << quoted(label)
                   << "  ; local_name_index=" << labelIndex
                   << " flags=0x" << std::hex << blockFlags << std::dec
                   << " {\n";
                ++sequenceCount;

                int indent=2;
                std::size_t p=off+28;
                const std::size_t end=off+blockSize;
                while (p < end) {
                    if (p+4 > end)
                        throw std::runtime_error("short command header");

                    const auto rawOpcode = rd<std::uint16_t>(data.data()+p);
                    const auto meta      = rd<std::uint16_t>(data.data()+p+2);
                    const auto id        = static_cast<std::uint8_t>(rawOpcode & 0xFF);
                    const auto recSize   =
                        static_cast<std::size_t>((meta & 0xFFFC) >> 2);

                    if (recSize < 4 || p+recSize > end)
                        throw std::runtime_error("bad command record size");

                    if (closesBefore(id) && indent > 2)
                        --indent;

                    os << std::string(static_cast<std::size_t>(indent)*2, ' ')
                       << decodeCommand(
                              id, data.data()+p, recSize, localNames);

                    const auto highFlags =
                        static_cast<std::uint16_t>(rawOpcode & 0xFF00);
                    if (highFlags)
                        os << "  ; opcode_flags=0x"
                           << std::hex << highFlags << std::dec;
                    os << '\n';

                    if (opensAfter(id))
                        ++indent;

                    p += recSize;
                    ++commandCount;
                }

                os << "  }\n";
                off += blockSize;
                ++seqOrdinal;
            }

            os << "}\n";
        }

        std::cerr
            << "Decompiled " << animationCount << " animation objects, "
            << sequenceCount << " named sequence blocks, "
            << commandCount << " commands.\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}
