#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include "zar_archive.hpp"
#include "compiled_rdr_ast.hpp"
#include "main_menu_model.hpp"

namespace fs=std::filesystem;
using namespace socom::retail_menu;
using socom::menu_boot::ZarArchive;

static MainMenuModel g_menu;
static int g_spot=1;
static HFONT g_font{};
static std::wstring g_reader;
static bool g_pressed=false;

static std::wstring W(const std::string&s){
    if(s.empty())return{};
    int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
    std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),w.data(),n);return w;
}
static COLORREF C(Color3 c,COLORREF fallback){
    if(c.r==0&&c.g==0&&c.b==0)return fallback;
    return RGB(std::clamp(c.r,0,255),std::clamp(c.g,0,255),std::clamp(c.b,0,255));
}
static const RetailButton* Active(){
    for(const auto&b:g_menu.buttons)if(MenuSpotForButton(b.name)==g_spot)return &b;
    return nullptr;
}
static void Activate(HWND hwnd){
    const auto*b=Active();if(!b)return;
    const std::string target=TransitionForAnimation(b->crossAnimation);
    std::wstring msg=L"Retail animation: "+W(b->crossAnimation);
    if(!target.empty())msg+=L"\nRetail target: "+W(target);
    else if(b->crossAnimation=="MainMenuOnLoad")
        msg+=L"\nLoad-game memory-card path is the next PC save shim.";
    else if(b->crossAnimation=="do_multi_or_medius")
        msg+=L"\nOnline path requires the network/IOP replacement.";
    MessageBoxW(hwnd,msg.c_str(),W(b->caption).c_str(),MB_OK);
}
static void DrawTextAt(HDC dc,const RECT&r,int logicalX,int logicalY,const std::wstring&s,float scale,COLORREF color){
    const float sx=(float)(r.right-r.left)/512.0f;
    const float sy=(float)(r.bottom-r.top)/448.0f;
    int px=(int)(logicalX*sx);
    int py=(int)(logicalY*sy);
    int h=std::max(12,(int)(28.0f*std::max(scale,0.4f)*sy));
    HFONT f=CreateFontW(h,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Arial");
    auto old=(HFONT)SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    RECT t{px-220,py-h,px+220,py+h};
    DrawTextW(dc,s.c_str(),-1,&t,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    SelectObject(dc,old);DeleteObject(f);
}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_KEYDOWN:
        if(w==VK_DOWN||w==VK_RIGHT){g_spot=g_spot==5?1:g_spot+1;InvalidateRect(h,nullptr,FALSE);}
        else if(w==VK_UP||w==VK_LEFT){g_spot=g_spot==1?5:g_spot-1;InvalidateRect(h,nullptr,FALSE);}
        else if(w==VK_RETURN||w==VK_SPACE){g_pressed=true;InvalidateRect(h,nullptr,FALSE);Activate(h);g_pressed=false;InvalidateRect(h,nullptr,FALSE);}
        else if(w==VK_ESCAPE)PostQuitMessage(0);
        return 0;
    case WM_PAINT:{
        PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);
        HBRUSH bg=CreateSolidBrush(RGB(5,8,12));FillRect(dc,&r,bg);DeleteObject(bg);

        // Retail MenuSpot carousel destination positions from OnMenuBtnUp/Down.
        static const int slotY[6]={0,300,300,300,300,300};
        static const int order[5]={1,2,3,4,5};
        // Active item stays at logical 300; surrounding items follow the retail
        // 250/275/325/350 carousel slots.
        for(const auto&b:g_menu.buttons){
            int spot=MenuSpotForButton(b.name);if(!spot)continue;
            int delta=(spot-g_spot+5)%5;
            int y=300;
            if(delta==1)y=325;
            else if(delta==2)y=350;
            else if(delta==3)y=250;
            else if(delta==4)y=275;
            bool active=spot==g_spot;
            const auto&st=active?(g_pressed?b.pressed:b.active):b.normal;
            COLORREF col=active?C(st.text,RGB(255,104,28)):C(st.text,RGB(30,30,50));
            DrawTextAt(dc,r,256,y,W(b.caption),st.scale>0?st.scale:(active?0.75f:0.6f),col);
        }

        std::wstring footer=L"Retail dlgMenu.rdr | MenuSpot="+std::to_wstring(g_spot)+L" | "+W(g_menu.movie);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(90,105,110));
        RECT f{10,r.bottom-36,r.right-10,r.bottom-8};DrawTextW(dc,footer.c_str(),-1,&f,DT_CENTER|DT_SINGLELINE);
        EndPaint(h,&ps);return 0;
    }
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR,int){
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc<2){MessageBoxW(nullptr,L"Pass UI\\readerc.zar as the first argument.",L"SOCOM retail menu",MB_OK);return 1;}
    try{
        g_reader=argv[1];
        ZarArchive z(fs::path(argv[1]).string());
        CompiledRdr rdr(z.ReadLeaf("dlgMenu.rdr"));
        g_menu=ExtractMainMenu(rdr.ParseRoot());
        if(g_menu.buttons.size()!=5)throw std::runtime_error("expected five retail menu buttons");

        WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=hi;wc.lpszClassName=L"SocomRetailMenuV3";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        RegisterClassW(&wc);
        HWND h=CreateWindowExW(0,wc.lpszClassName,L"SOCOM Native - Retail Menu v3",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,1024,896,nullptr,nullptr,hi,nullptr);
        if(!h)throw std::runtime_error("CreateWindowEx failed");
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
        LocalFree(argv);return(int)msg.wParam;
    }catch(const std::exception&e){MessageBoxA(nullptr,e.what(),"SOCOM retail menu error",MB_OK|MB_ICONERROR);return 1;}
}
