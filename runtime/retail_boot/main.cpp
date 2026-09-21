#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>
#include <sstream>

#include "zar_archive.hpp"
#include "compiled_rdr_screen.hpp"

namespace fs=std::filesystem;
using socom::menu_boot::ZarArchive;
using socom::retail_boot::CompiledRdrStrings;
using socom::retail_boot::ParseScreenStrings;
using socom::retail_boot::ScreenDef;

static ScreenDef g_menu;
static int g_selected=0;
static HFONT g_titleFont{};
static HFONT g_menuFont{};
static std::wstring g_status;
static fs::path g_gameRoot;
static fs::path g_uiReader;

static std::wstring W(const std::string&s){
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
    std::wstring o(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),o.data(),n);
    return o;
}

static fs::path FindInsensitiveChild(const fs::path& base,const std::vector<std::string>& parts){
    fs::path cur=base;
    for(const auto& want:parts){
        if(!fs::exists(cur)||!fs::is_directory(cur)) return {};
        bool found=false;
        for(const auto& e:fs::directory_iterator(cur)){
            if(_stricmp(e.path().filename().string().c_str(),want.c_str())==0){
                cur=e.path(); found=true; break;
            }
        }
        if(!found)return {};
    }
    return cur;
}

static fs::path FindRecursiveFile(const fs::path& root,const std::string& filename,const std::string& parentHint=""){
    if(!fs::exists(root)) return {};
    for(const auto& e:fs::recursive_directory_iterator(root,fs::directory_options::skip_permission_denied)){
        if(!e.is_regular_file())continue;
        if(_stricmp(e.path().filename().string().c_str(),filename.c_str())!=0)continue;
        if(!parentHint.empty()){
            std::string p=e.path().parent_path().string();
            std::string lo=p, hint=parentHint;
            for(char&c:lo)c=(char)tolower((unsigned char)c);
            for(char&c:hint)c=(char)tolower((unsigned char)c);
            if(lo.find(hint)==std::string::npos)continue;
        }
        return e.path();
    }
    return {};
}

static fs::path ResolveRetailPath(const fs::path& gameRoot,const std::string& retail){
    std::string norm=retail;
    for(char&c:norm) if(c=='\\')c='/';

    std::vector<std::string> parts;
    std::stringstream ss(norm);
    std::string x;
    while(std::getline(ss,x,'/')) if(!x.empty())parts.push_back(x);

    // Retail paths usually start with "run".  GameRoot may itself be RUN.
    if(!parts.empty() && _stricmp(parts[0].c_str(),"run")==0){
        auto p=FindInsensitiveChild(gameRoot,parts);
        if(!p.empty())return p;

        std::vector<std::string> withoutRun(parts.begin()+1,parts.end());
        p=FindInsensitiveChild(gameRoot,withoutRun);
        if(!p.empty())return p;
    }

    auto p=FindInsensitiveChild(gameRoot,parts);
    if(!p.empty())return p;

    if(!parts.empty())
        return FindRecursiveFile(gameRoot,parts.back());

    return {};
}

static fs::path FindExeOnPath(const std::wstring& name){
    wchar_t buf[MAX_PATH]{};
    DWORD n=SearchPathW(nullptr,name.c_str(),nullptr,MAX_PATH,buf,nullptr);
    if(n>0 && n<MAX_PATH)return fs::path(buf);
    return {};
}

static bool PlayPssWithFfplay(const fs::path& p,bool skippable){
    const fs::path ffplay=FindExeOnPath(L"ffplay.exe");
    if(ffplay.empty())return false;

    std::wstring cmd=L"\""+ffplay.wstring()+L"\" -loglevel error -autoexit -noborder ";
    if(skippable) cmd+=L"-exitonkeydown ";
    cmd+=L"\""+p.wstring()+L"\"";

    STARTUPINFOW si{}; si.cb=sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end());
    mutableCmd.push_back(L'\0');

    if(!CreateProcessW(nullptr,mutableCmd.data(),nullptr,nullptr,FALSE,0,nullptr,
                       p.parent_path().wstring().c_str(),&si,&pi))
        throw std::runtime_error("failed launching ffplay.exe");

    WaitForSingleObject(pi.hProcess,INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static ScreenDef LoadScreen(ZarArchive& ui,const std::string& leaf){
    auto bytes=ui.ReadLeaf(leaf);
    CompiledRdrStrings rdr(std::move(bytes));
    return ParseScreenStrings(leaf,rdr);
}

static void DrawCentered(HDC dc,const RECT&r,int y,const std::wstring&text,HFONT font,COLORREF color){
    auto old=(HFONT)SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,color);
    RECT t{r.left,y,r.right,y+54};
    DrawTextW(dc,text.c_str(),-1,&t,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    SelectObject(dc,old);
}

static void SelectMenu(HWND hwnd){
    const auto& item=g_menu.menuEntries.at((std::size_t)g_selected);
    std::wstring title=W(item);
    std::wstring body;

    if(item=="NEW GAME")
        body=L"Retail NEW GAME reached.\n\nNext: bind UiprepMission1 and transition into dlgMISSIONSELECTION.rdr.";
    else if(item=="LOAD GAME")
        body=L"Retail LOAD GAME reached.\n\nPC save/memory-card shim is not bound yet.";
    else if(item=="ONLINE")
        body=L"Retail ONLINE reached.\n\nNetwork/IOP services remain stubbed for the menu milestone.";
    else
        body=L"Retail OPTIONS reached.\n\nNext: load dlgOptions_Menu.rdr through the same screen state machine.";

    MessageBoxW(hwnd,body.c_str(),title.c_str(),MB_OK|MB_ICONINFORMATION);
}

static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_KEYDOWN:
        if(wp==VK_UP){g_selected=(g_selected+3)%4;InvalidateRect(hwnd,nullptr,FALSE);}
        else if(wp==VK_DOWN){g_selected=(g_selected+1)%4;InvalidateRect(hwnd,nullptr,FALSE);}
        else if(wp==VK_RETURN||wp==VK_SPACE)SelectMenu(hwnd);
        else if(wp==VK_ESCAPE)PostQuitMessage(0);
        return 0;

    case WM_PAINT:{
        PAINTSTRUCT ps{};
        HDC dc=BeginPaint(hwnd,&ps);
        RECT r{};GetClientRect(hwnd,&r);
        HBRUSH bg=CreateSolidBrush(RGB(5,8,10));
        FillRect(dc,&r,bg);DeleteObject(bg);

        DrawCentered(dc,r,45,L"SOCOM: U.S. NAVY SEALs",g_titleFont,RGB(210,220,225));
        DrawCentered(dc,r,105,L"SCUS_971.34 retail UI state",g_menuFont,RGB(100,120,130));

        int y=220;
        for(int i=0;i<(int)g_menu.menuEntries.size();++i){
            bool sel=i==g_selected;
            std::wstring s=W(g_menu.menuEntries[(std::size_t)i]);
            if(sel)s=L">  "+s+L"  <";
            DrawCentered(dc,r,y+i*60,s,g_menuFont,
                         sel?RGB(255,255,255):RGB(125,140,145));
        }
        DrawCentered(dc,r,r.bottom-90,g_status,g_menuFont,RGB(80,100,105));
        DrawCentered(dc,r,r.bottom-52,L"Arrow keys: navigate   Enter: select   Esc: quit",
                     g_menuFont,RGB(80,100,105));
        EndPaint(hwnd,&ps);
        return 0;
    }

    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int){
    int argc=0;
    wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    try{
        if(argc<2)
            throw std::runtime_error(
                "Usage: socom_retail_boot.exe <game root> [UI readerc.zar]\n\n"
                "Example:\n"
                "  socom_retail_boot.exe D:\\SOCOM");

        g_gameRoot=fs::path(argv[1]);
        if(!fs::exists(g_gameRoot))
            throw std::runtime_error("game root does not exist");

        if(argc>=3 && fs::exists(fs::path(argv[2])))
            g_uiReader=fs::path(argv[2]);
        else{
            g_uiReader=FindRecursiveFile(g_gameRoot,"readerc.zar","ui");
            if(g_uiReader.empty())
                throw std::runtime_error(
                    "UI\\readerc.zar not found under game root. "
                    "Pass it as the second argument.");
        }

        ZarArchive ui(g_uiReader.string());

        // Actual retail UI boot chain recovered from RDR:
        // dlgIntroScreen -> dlgIntroCinematic -> dlgSecondCinematic -> dlgMenu.
        ScreenDef introScreen=LoadScreen(ui,"dlgIntroScreen.rdr");
        std::string next=introScreen.onStartSwitchTarget;
        if(next.empty())next="dlgIntroCinematic.rdr";

        ScreenDef sony=LoadScreen(ui,next);
        fs::path sonyMovie=ResolveRetailPath(g_gameRoot,sony.backgroundFile);
        if(sonyMovie.empty())
            throw std::runtime_error(("movie not found: "+sony.backgroundFile).c_str());

        if(!PlayPssWithFfplay(sonyMovie,true))
            throw std::runtime_error(
                "ffplay.exe was not found on PATH.\n\n"
                "Install an FFmpeg build that includes ffplay, then rerun. "
                "The retail movie was resolved successfully.");

        next=sony.onMpegEndTarget;
        if(next.empty())next="dlgSecondCinematic.rdr";

        ScreenDef intro=LoadScreen(ui,next);
        fs::path introMovie=ResolveRetailPath(g_gameRoot,intro.backgroundFile);
        if(introMovie.empty())
            throw std::runtime_error(("movie not found: "+intro.backgroundFile).c_str());

        if(!PlayPssWithFfplay(introMovie,true))
            throw std::runtime_error("ffplay.exe disappeared from PATH");

        next=intro.onMpegEndTarget;
        if(next.empty())next="dlgMenu.rdr";

        g_menu=LoadScreen(ui,next);
        if(g_menu.menuEntries.size()!=4)
            throw std::runtime_error("retail dlgMenu.rdr main controls were not recovered");

        g_status=L"Loaded "+g_uiReader.wstring()+L"  |  "+W(g_menu.backgroundFile);

        g_titleFont=CreateFontW(40,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
        g_menuFont=CreateFontW(22,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");

        const wchar_t* cls=L"SocomRetailBootV2";
        WNDCLASSW wc{};
        wc.lpfnWndProc=WndProc;wc.hInstance=h;wc.lpszClassName=cls;
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        RegisterClassW(&wc);

        HWND hwnd=CreateWindowExW(0,cls,L"SOCOM Native - Retail Boot",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,1100,720,
            nullptr,nullptr,h,nullptr);
        if(!hwnd)throw std::runtime_error("CreateWindowExW failed");

        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }

        if(g_titleFont)DeleteObject(g_titleFont);
        if(g_menuFont)DeleteObject(g_menuFont);
        LocalFree(argv);
        return (int)msg.wParam;
    }catch(const std::exception&e){
        MessageBoxA(nullptr,e.what(),"SOCOM retail boot error",MB_OK|MB_ICONERROR);
        if(argv)LocalFree(argv);
        return 1;
    }
}
