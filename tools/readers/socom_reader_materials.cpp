// socom_reader_materials.cpp
// Extract SOCOM 1 compiled materials.rdr from a GameZ reader*.zar archive.
//
// Build:
//   macOS/Linux:
//     clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic socom_reader_materials.cpp -o socom_reader_materials
//
//   Windows Developer Command Prompt:
//     cl /std:c++17 /EHsc /O2 socom_reader_materials.cpp
//
// Usage:
//   socom_reader_materials readerc.zar
//   socom_reader_materials readerc.zar materials.csv
//
// The supplied retail SCUS_971.34 core reader archive contains a leaf named
// materials.rdr. Its payload is a compiled zReader/LISP-style typed tree.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
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

static std::size_t align_up(std::size_t v, std::size_t a) {
    if (!a) throw std::runtime_error("zero ZAR alignment");
    const auto r = v % a;
    return r ? v + (a-r) : v;
}

struct ZarLeaf {
    std::string name;
    std::uint32_t dataOffset{};
    std::uint32_t dataSize{};
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

        strOff_ = sizeof(ZarHeader);
        nodeOff_ = strOff_ + h_.stringBlockSize;
        const std::uint64_t nodeBytes =
            static_cast<std::uint64_t>(h_.nodeCount) * sizeof(ZarNodeDisk);
        if (nodeOff_ + nodeBytes > bytes_.size())
            throw std::runtime_error("node table outside file");

        payloadOff_ = align_up(
            static_cast<std::size_t>(nodeOff_ + nodeBytes), h_.alignment);
        if (payloadOff_ + h_.payloadSize != bytes_.size())
            throw std::runtime_error("payload does not end at EOF");

        nodes_.resize(h_.nodeCount);
        std::memcpy(nodes_.data(), bytes_.data()+nodeOff_,
                    static_cast<std::size_t>(nodeBytes));
    }

    ZarLeaf findLeaf(const std::string& wanted) const {
        for (const auto& n : nodes_) {
            if (resolveName(n.nameAddress) == wanted)
                return {wanted, n.dataOffset, n.dataSize};
        }
        throw std::runtime_error("leaf not found: " + wanted);
    }

    std::vector<std::uint8_t> readLeaf(const ZarLeaf& leaf) const {
        const auto end =
            static_cast<std::uint64_t>(leaf.dataOffset) + leaf.dataSize;
        if (end > h_.payloadSize)
            throw std::runtime_error("leaf payload outside archive");
        return std::vector<std::uint8_t>(
            bytes_.begin()+payloadOff_+leaf.dataOffset,
            bytes_.begin()+payloadOff_+leaf.dataOffset+leaf.dataSize);
    }

private:
    std::string resolveName(std::uint32_t addr) const {
        if (!addr) return "<root>";
        if (addr < h_.preferredBase) return "<bad>";
        const std::uint64_t rel =
            static_cast<std::uint64_t>(addr)-h_.preferredBase;
        if (rel >= h_.stringBlockSize) return "<bad>";
        std::size_t p = strOff_ + static_cast<std::size_t>(rel);
        std::size_t e = p;
        const std::size_t lim = strOff_ + h_.stringBlockSize;
        while (e < lim && bytes_[e]) ++e;
        return std::string(
            reinterpret_cast<const char*>(bytes_.data()+p), e-p);
    }

    std::vector<std::uint8_t> bytes_;
    ZarHeader h_{};
    std::vector<ZarNodeDisk> nodes_;
    std::size_t strOff_{}, nodeOff_{}, payloadOff_{};
};

using Value = std::variant<std::monostate, bool, std::int32_t, float, std::string>;

struct Material {
    std::map<std::string, Value> fields;
};

class CompiledRdr {
public:
    explicit CompiledRdr(std::vector<std::uint8_t> bytes)
        : bytes_(std::move(bytes)) {
        if (bytes_.size() < 8)
            throw std::runtime_error("RDR payload too small");
        poolSize_ = rd<std::uint32_t>(bytes_.data()+0);
        dataOff_  = rd<std::uint32_t>(bytes_.data()+4);
        if (8ull + poolSize_ > bytes_.size() || dataOff_ > bytes_.size())
            throw std::runtime_error("invalid compiled RDR header");
    }

    std::vector<Material> parseSoils() const {
        const auto soilsRel = findSoilsList();
        if (dataOff_ + soilsRel + 8 > bytes_.size())
            throw std::runtime_error("SOILS list outside RDR");

        std::size_t pos = soilsRel + 8;
        std::size_t minTarget = std::numeric_limits<std::size_t>::max();
        std::vector<std::uint32_t> recordPtrs;

        // GameZ list nodes begin with a small [type,count] header, followed by
        // tagged values. For the SOILS list these are relative pointers.
        while (dataOff_ + pos + 8 <= bytes_.size() && pos < minTarget) {
            const auto type = u32(pos);
            const auto value = u32(pos+4);
            if (type != 4) break;
            recordPtrs.push_back(value);
            minTarget = std::min<std::size_t>(minTarget, value);
            pos += 8;
        }

        std::vector<Material> out;
        for (const auto ptr : recordPtrs)
            out.push_back(parseRecord(ptr));
        return out;
    }

private:
    std::uint32_t u32(std::size_t rel) const {
        if (dataOff_ + rel + 4 > bytes_.size())
            throw std::runtime_error("RDR data read outside payload");
        return rd<std::uint32_t>(bytes_.data()+dataOff_+rel);
    }

    std::string str(std::uint32_t off) const {
        if (off >= poolSize_)
            throw std::runtime_error("RDR string offset outside pool");
        const std::size_t start = 8 + off;
        const std::size_t limit = 8 + poolSize_;
        std::size_t end = start;
        while (end < limit && bytes_[end]) ++end;
        if (end == limit)
            throw std::runtime_error("unterminated RDR string");
        return std::string(
            reinterpret_cast<const char*>(bytes_.data()+start),
            end-start);
    }

    std::uint32_t findSoilsList() const {
        const std::size_t maxScan =
            std::min<std::size_t>(0x100, bytes_.size()-dataOff_);
        for (std::size_t pos=0; pos+16<=maxScan; pos+=8) {
            const auto t = u32(pos);
            const auto v = u32(pos+4);
            if (t == 3 && str(v) == "SOILS") {
                if (u32(pos+8) != 4)
                    throw std::runtime_error("SOILS not followed by list pointer");
                return u32(pos+12);
            }
        }
        throw std::runtime_error("SOILS symbol not found");
    }

    Value decodeValue(std::uint32_t rel) const {
        const auto listType = u32(rel);
        const auto listCount = u32(rel+4);
        if (listType != 1)
            throw std::runtime_error("unexpected RDR value node");

        // GameZ uses a single-item/empty list form for enabled switches.
        if (listCount == 1)
            return true;

        if (listCount < 2)
            return std::monostate{};

        const auto atomType = u32(rel+8);
        const auto atom = u32(rel+12);
        switch (atomType) {
        case 1:
            return static_cast<std::int32_t>(atom);
        case 2: {
            float f{};
            std::memcpy(&f, &atom, sizeof(f));
            return f;
        }
        case 3:
            return str(atom);
        default:
            return std::monostate{};
        }
    }

    Material parseRecord(std::uint32_t rel) const {
        if (u32(rel) != 1)
            throw std::runtime_error("unexpected material record");

        Material m;
        std::size_t pos = rel + 8;
        std::size_t minValue =
            std::numeric_limits<std::size_t>::max();

        // The field stream contains string-symbol tags. Most are followed by a
        // pointer to a value node. Some switch keywords (LIQUID, UNDERWATER,
        // PICKUP, etc.) occur as bare symbols and therefore mean true.
        while (dataOff_ + pos + 8 <= bytes_.size() && pos < minValue) {
            const auto tag = u32(pos);
            const auto keyOff = u32(pos+4);
            if (tag != 3) break;

            const std::string key = str(keyOff);
            if (dataOff_ + pos + 16 <= bytes_.size() &&
                u32(pos+8) == 4) {
                const auto valuePtr = u32(pos+12);
                minValue = std::min<std::size_t>(minValue, valuePtr);
                m.fields[key] = decodeValue(valuePtr);
                pos += 16;
            } else {
                m.fields[key] = true;
                pos += 8;
            }
        }
        return m;
    }

    std::vector<std::uint8_t> bytes_;
    std::uint32_t poolSize_{};
    std::uint32_t dataOff_{};
};

static std::string valueString(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return "";
    if (auto p=std::get_if<bool>(&v)) return *p ? "true" : "false";
    if (auto p=std::get_if<std::int32_t>(&v)) return std::to_string(*p);
    if (auto p=std::get_if<float>(&v)) {
        std::ostringstream ss;
        ss << std::setprecision(9) << *p;
        return ss.str();
    }
    return std::get<std::string>(v);
}

static std::string csvQuote(const std::string& s) {
    bool q = s.find_first_of(",\"\r\n") != std::string::npos;
    if (!q) return s;
    std::string o = "\"";
    for (char c : s) {
        if (c=='"') o += "\"\"";
        else o += c;
    }
    o += '"';
    return o;
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) {
            std::cerr << "Usage: socom_reader_materials readerc.zar [materials.csv]\n";
            return 1;
        }

        Zar zar(argv[1]);
        const auto leaf = zar.findLeaf("materials.rdr");
        CompiledRdr rdr(zar.readLeaf(leaf));
        const auto materials = rdr.parseSoils();

        std::vector<std::string> columns = {
            "NAME","OPACITY","PENETRATION","RICOCHET","IMPACT_RADIUS_MOD",
            "ELASTICITY_COEFF","STEALTH_FACTOR","FOOT_STEP_OFFSET",
            "STEPSOUND","STEALTH_STEPSOUND","CRAWLSOUND","LANDSOUND",
            "VOLUMETRIC","LIQUID","UNDERWATER","PICKUP",
            "WEAPONANIM","DECAL"
        };

        std::ostream* osp = &std::cout;
        std::ofstream file;
        if (argc == 3) {
            file.open(argv[2], std::ios::binary);
            if (!file) throw std::runtime_error("cannot create output CSV");
            osp = &file;
        }
        auto& os=*osp;

        os << "material_id";
        for (const auto& c:columns) os << ',' << c;
        os << '\n';

        for (std::size_t i=0;i<materials.size();++i) {
            os << i;
            for (const auto& c:columns) {
                os << ',';
                auto it=materials[i].fields.find(c);
                if (it!=materials[i].fields.end())
                    os << csvQuote(valueString(it->second));
            }
            os << '\n';
        }

        std::cerr << "Recovered " << materials.size()
                  << " materials from materials.rdr\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}
