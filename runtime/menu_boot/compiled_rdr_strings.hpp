#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace socom::menu_boot {

class CompiledRdrStrings {
public:
    explicit CompiledRdrStrings(std::vector<std::uint8_t> bytes)
        : bytes_(std::move(bytes)) {
        if (bytes_.size() < 8)
            throw std::runtime_error("compiled RDR too small");
        std::memcpy(&poolSize_, bytes_.data(), 4);
        std::memcpy(&dataOffset_, bytes_.data()+4, 4);
        if (8ull + poolSize_ > bytes_.size())
            throw std::runtime_error("bad RDR string pool size");
        if (dataOffset_ > bytes_.size())
            throw std::runtime_error("bad RDR data offset");

        const std::size_t begin = 8;
        const std::size_t end = begin + poolSize_;
        std::size_t p = begin;
        while (p < end) {
            std::size_t e = p;
            while (e < end && bytes_[e]) ++e;
            if (e > p) {
                strings_.emplace_back(
                    reinterpret_cast<const char*>(bytes_.data()+p), e-p);
            }
            p = e + 1;
        }
    }

    bool Has(const std::string& s) const {
        for (const auto& x : strings_)
            if (x == s) return true;
        return false;
    }

    std::string ValueAfter(const std::string& key) const {
        for (std::size_t i=0;i+1<strings_.size();++i)
            if (strings_[i] == key) return strings_[i+1];
        return {};
    }

    const std::vector<std::string>& Strings() const { return strings_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t poolSize_{};
    std::uint32_t dataOffset_{};
    std::vector<std::string> strings_;
};

struct RetailMenuInfo {
    std::string backgroundMovie;
    std::string font;
    std::vector<std::string> mainEntries;
};

inline RetailMenuInfo RecoverRetailMainMenu(const CompiledRdrStrings& rdr) {
    RetailMenuInfo out;

    // The exact retail dlgMenu.rdr string pool includes these values.
    // We still validate them instead of silently synthesizing a menu.
    if (rdr.Has("run/movies/common/menuloop.pss"))
        out.backgroundMovie = "run/movies/common/menuloop.pss";

    if (rdr.Has("myriad"))
        out.font = "myriad";

    for (const char* label : {"NEW GAME","LOAD GAME","ONLINE","OPTIONS"}) {
        if (rdr.Has(label))
            out.mainEntries.emplace_back(label);
    }

    if (out.mainEntries.size() != 4)
        throw std::runtime_error(
            "dlgMenu.rdr did not contain the expected retail main-menu controls");

    return out;
}

} // namespace socom::menu_boot
