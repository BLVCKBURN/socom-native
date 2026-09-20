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
    case 30: // END_WHILE
    case 32: // BREAK
        return ss.str();

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
