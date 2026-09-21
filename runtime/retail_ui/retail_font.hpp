#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>

namespace socom::retail_ui {

struct RetailGlyph {
    std::uint32_t code;
    int x1,y1,x2,y2,baseline;
};

static constexpr RetailGlyph kMyriadGlyphs[] = {
    {65,0,0,23,25,0},
    {66,23,0,43,26,1},
    {67,43,0,64,26,1},
    {68,64,0,87,26,1},
    {69,87,0,104,25,0},
    {70,104,0,121,25,0},
    {71,121,0,144,26,1},
    {72,144,0,166,25,0},
    {73,166,0,173,25,0},
    {74,174,0,188,26,1},
    {75,188,0,209,25,0},
    {76,209,0,226,25,0},
    {77,226,0,255,25,0},
    {78,255,0,277,25,0},
    {79,277,0,302,25,0},
    {80,302,0,321,25,0},
    {81,321,0,346,29,4},
    {82,346,0,366,25,0},
    {83,366,0,384,26,1},
    {84,384,0,404,25,0},
    {85,404,0,426,26,1},
    {86,426,0,450,25,0},
    {87,450,0,483,25,0},
    {88,483,0,506,25,0},
    {89,0,26,22,52,0},
    {90,22,26,42,52,0},
    {97,43,34,59,54,1},
    {98,59,26,79,54,1},
    {99,79,34,95,54,1},
    {100,95,26,115,54,1},
    {101,115,34,133,54,1},
    {102,133,26,147,53,0},
    {103,147,26,167,53,8},
    {104,167,26,185,53,0},
    {105,185,26,193,53,0},
    {106,193,26,205,61,8},
    {107,205,26,224,53,0},
    {108,224,26,231,53,0},
    {109,231,34,259,53,0},
    {110,259,34,277,53,0},
    {111,277,34,297,54,1},
    {112,297,33,317,59,7},
    {113,317,34,337,60,7},
    {114,337,34,350,53,0},
    {115,350,34,365,54,1},
    {116,365,30,379,54,1},
    {117,379,34,397,54,1},
    {118,397,34,417,53,0},
    {119,417,34,446,53,0},
    {120,446,34,466,53,0},
    {121,466,34,486,61,8},
    {122,486,34,503,53,0},
    {49,0,54,11,79,0},
    {50,11,54,30,79,0},
    {51,30,54,48,80,0},
    {52,48,54,68,79,0},
    {53,68,54,86,80,0},
    {54,86,54,105,80,0},
    {55,105,54,124,79,0},
    {56,124,54,143,80,0},
    {57,143,54,162,80,0},
    {48,162,53,181,79,0},
    {32,183,62,200,87,0},
    {169,416,58,447,89,0},
    {33,184,93,192,120,0},
    {64,192,93,219,120,0},
    {35,219,94,238,119,0},
    {36,238,91,255,122,0},
    {37,255,94,286,120,0},
    {94,286,94,306,113,-7},
    {38,306,93,331,120,0},
    {42,331,93,347,109,-10},
    {40,347,93,356,124,1},
    {41,357,93,367,124,1},
    {95,367,120,386,124,0},
    {45,411,106,422,112,-9},
    {43,386,99,406,119,-2},
    {61,422,103,442,115,-6},
    {92,442,93,455,121,0},
    {123,17,98,27,127,0},
    {125,27,98,38,127,0},
    {91,0,98,8,127,0},
    {93,8,98,17,127,0},
    {58,48,104,57,124,-2},
    {59,38,104,48,128,-1},
    {39,57,108,63,119,-16},
    {60,70,103,89,123,-2},
    {62,89,103,108,123,-2},
    {44,122,115,131,127,3},
    {46,132,115,141,124,0},
    {63,108,97,122,124,0},
    {47,141,97,154,126,2},
    {126,154,104,174,113,-16},
    {96,174,92,184,100,-18},
    {124,406,91,411,127,2},
    {34,57,97,70,108,-16}
};

inline const RetailGlyph* FindMyriadGlyph(std::uint32_t code) {
    for(const auto&g:kMyriadGlyphs) if(g.code==code) return &g;
    return nullptr;
}

class RetailBitmapFont {
public:
    static constexpr float kNominalCapHeight = 25.0f;
    static constexpr float kTracking = 1.7f;

    static float Measure(const std::wstring& text,float scale) {
        const float drawScale=std::max(scale,0.01f);
        float width=0.0f;
        bool first=true;
        for(wchar_t ch:text) {
            auto*g=FindMyriadGlyph((std::uint32_t)ch);
            if(!g) continue;
            if(!first) width += kTracking*drawScale;
            width += (float)(g->x2-g->x1)*drawScale;
            first=false;
        }
        return width;
    }

    static bool Draw(Gdiplus::Graphics& graphics,Gdiplus::Image* atlas,
                     const RECT& client,float logicalX,float logicalY,
                     const std::wstring& text,float scale,
                     int r,int g,int b,float opacity,bool centered)
    {
        if(!atlas || atlas->GetLastStatus()!=Gdiplus::Ok) return false;

        const float sx=(float)(client.right-client.left)/640.0f;
        const float sy=(float)(client.bottom-client.top)/448.0f;
        const float drawScale=std::max(scale,0.01f);
        const float logicalWidth=Measure(text,drawScale);
        float cursor=logicalX-(centered?logicalWidth*0.5f:0.0f);

        // The retail atlas stores glyphs with variable source heights.  The
        // final integer in kMyriadGlyphs is the amount extending below the
        // common baseline, not a per-glyph Y offset.  Treating it as an
        // offset made descenders and even several capitals visibly wobble.
        // RDR YPOS behaves like the visual centre of a 25-pixel cap-height
        // cell, so derive one shared baseline from that centre.
        const float baselineY=
            logicalY + (kNominalCapHeight*0.5f*drawScale);

        Gdiplus::ImageAttributes attrs;
        const float rf=std::clamp(r/255.0f,0.0f,1.0f);
        const float gf=std::clamp(g/255.0f,0.0f,1.0f);
        const float bf=std::clamp(b/255.0f,0.0f,1.0f);
        const float af=std::clamp(opacity,0.0f,1.0f);
        Gdiplus::ColorMatrix cm={
            rf,0,0,0,0,
            0,gf,0,0,0,
            0,0,bf,0,0,
            0,0,0,af,0,
            0,0,0,0,1
        };
        attrs.SetColorMatrix(&cm);

        const auto state=graphics.Save();
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

        bool first=true;
        for(wchar_t ch:text) {
            const auto* glyph=FindMyriadGlyph((std::uint32_t)ch);
            if(!glyph) continue;

            if(!first) cursor += kTracking*drawScale;

            const int sw=glyph->x2-glyph->x1;
            const int sh=glyph->y2-glyph->y1;

            // Align every glyph to the same baseline.  Descenders extend
            // below it by glyph->baseline source pixels.
            const float topLogical =
                baselineY - (float)(sh-glyph->baseline)*drawScale;

            const float dx=std::round(cursor*sx);
            const float dy=std::round(topLogical*sy);
            const float dw=std::max(1.0f,std::round(sw*drawScale*sx));
            const float dh=std::max(1.0f,std::round(sh*drawScale*sy));

            Gdiplus::RectF dst(dx,dy,dw,dh);
            graphics.DrawImage(atlas,dst,
                (Gdiplus::REAL)glyph->x1,(Gdiplus::REAL)glyph->y1,
                (Gdiplus::REAL)sw,(Gdiplus::REAL)sh,
                Gdiplus::UnitPixel,&attrs);

            cursor += sw*drawScale;
            first=false;
        }

        graphics.Restore(state);
        return true;
    }
};
} // namespace
