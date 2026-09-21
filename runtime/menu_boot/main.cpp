#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "zar_archive.hpp"
#include "compiled_rdr_strings.hpp"

namespace fs = std::filesystem;
using socom::menu_boot::CompiledRdrStrings;
using socom::menu_boot::RecoverRetailMainMenu;
using socom::menu_boot::RetailMenuInfo;
using socom::menu_boot::ZarArchive;

static RetailMenuInfo g_menu;
static std::wstring g_archivePath;
static HFONT g_titleFont{};
static HFONT g_menuFont{};
static int g_selected = 0;
static constexpr UINT_PTR kTimer = 1;

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
    std::wstring w(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),w.data(),n);
    return w;
}

static void DrawCentered(HDC dc, const RECT& r, int y, const std::wstring& text, HFONT font, COLORREF color) {
    HFONT old=(HFONT)SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,color);
    RECT tr{r.left,y,r.right,y+64};
    DrawTextW(dc,text.c_str(),-1,&tr,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    SelectObject(dc,old);
}

static void ActivateSelection(HWND hwnd) {
    const std::wstring item = Widen(g_menu.mainEntries.at((std::size_t)g_selected));
    std::wstring msg;
    if (item == L"NEW GAME") {
        msg =
            L"Retail NEW GAME control reached.\n\n"
            L"Next integration target: execute the original UiprepMission1 / "
            L"mission-selection command path and load dlgMISSIONSELECTION.rdr.";
    } else if (item == L"LOAD GAME") {
        msg =
            L"Retail LOAD GAME control reached.\n\n"
            L"Memory-card access is intentionally stubbed for this first PC menu milestone.";
    } else if (item == L"ONLINE") {
        msg =
            L"Retail ONLINE control reached.\n\n"
            L"Network/IOP services are intentionally stubbed for the initial menu bootstrap.";
    } else {
        msg =
            L"Retail OPTIONS control reached.\n\n"
            L"Next UI step: load dlgOptions_Menu.rdr through the same reader archive path.";
    }
    MessageBoxW(hwnd,msg.c_str(),item.c_str(),MB_OK|MB_ICONINFORMATION);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        SetTimer(hwnd,kTimer,16,nullptr);
        return 0;

    case WM_TIMER:
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;

    case WM_KEYDOWN:
        if (wp==VK_UP) {
            g_selected=(g_selected+3)%4;
            InvalidateRect(hwnd,nullptr,FALSE);
        } else if (wp==VK_DOWN) {
            g_selected=(g_selected+1)%4;
            InvalidateRect(hwnd,nullptr,FALSE);
        } else if (wp==VK_RETURN || wp==VK_SPACE) {
            ActivateSelection(hwnd);
        } else if (wp==VK_ESCAPE) {
            PostQuitMessage(0);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc=BeginPaint(hwnd,&ps);
        RECT r{}; GetClientRect(hwnd,&r);

        HBRUSH bg=CreateSolidBrush(RGB(7,10,13));
        FillRect(dc,&r,bg);
        DeleteObject(bg);

        DrawCentered(dc,r,42,L"SOCOM: U.S. NAVY SEALs",g_titleFont,RGB(210,218,222));
        DrawCentered(dc,r,108,L"SCUS_971.34 Retail Menu Bootstrap",g_menuFont,RGB(110,130,140));

        const int startY=240;
        const int step=62;
        for (int i=0;i<4;++i) {
            const bool selected=i==g_selected;
            COLORREF c=selected?RGB(255,255,255):RGB(130,145,150);
            std::wstring label=Widen(g_menu.mainEntries[(std::size_t)i]);
            if (selected) label=L">  "+label+L"  <";
            DrawCentered(dc,r,startY+i*step,label,g_menuFont,c);
        }

        std::wstring footer =
            L"Retail dlgMenu.rdr  |  background: " +
            Widen(g_menu.backgroundMovie) +
            L"  |  font: " + Widen(g_menu.font);
        DrawCentered(dc,r,r.bottom-70,footer,g_menuFont,RGB(80,95,100));

        EndPaint(hwnd,&ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd,kTimer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

static fs::path FindReaderArchive(int argc, wchar_t** argv) {
    if (argc >= 2) {
        fs::path p=argv[1];
        if (fs::exists(p)) return p;
    }

    const fs::path exeDir=fs::path(argv[0]).parent_path();
    const std::vector<fs::path> candidates = {
        exeDir/"readerc.zar",
        exeDir/"data"/"common"/"readerc.zar",
        exeDir/"run"/"data"/"common"/"readerc.zar",
        fs::current_path()/"readerc.zar",
        fs::current_path()/"run"/"data"/"common"/"readerc.zar"
    };
    for(const auto& p:candidates)
        if(fs::exists(p)) return p;

    throw std::runtime_error(
        "readerc.zar was not found. Pass its full path as the first command-line argument.");
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    int argc=0;
    wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);

    try {
        const fs::path archive=FindReaderArchive(argc,argv);
        g_archivePath=archive.wstring();

        ZarArchive zar(archive.string());
        auto rdrBytes=zar.ReadLeaf("dlgMenu.rdr");
        CompiledRdrStrings rdr(std::move(rdrBytes));
        g_menu=RecoverRetailMainMenu(rdr);

        g_titleFont=CreateFontW(
            40,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        g_menuFont=CreateFontW(
            24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,L"Segoe UI");

        const wchar_t* cls=L"SocomRetailMenuBootstrap";
        WNDCLASSW wc{};
        wc.lpfnWndProc=WndProc;
        wc.hInstance=instance;
        wc.lpszClassName=cls;
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        RegisterClassW(&wc);

        HWND hwnd=CreateWindowExW(
            0,cls,L"SOCOM Native - Retail Main Menu Bootstrap",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,1100,720,
            nullptr,nullptr,instance,nullptr);
        if(!hwnd) throw std::runtime_error("CreateWindowExW failed");

        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if(g_titleFont) DeleteObject(g_titleFont);
        if(g_menuFont) DeleteObject(g_menuFont);
        LocalFree(argv);
        return (int)msg.wParam;
    } catch(const std::exception& e) {
        MessageBoxA(nullptr,e.what(),"SOCOM menu bootstrap error",MB_OK|MB_ICONERROR);
        if(argv) LocalFree(argv);
        return 1;
    }
}
