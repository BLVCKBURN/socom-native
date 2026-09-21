#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <propidl.h>
#include <ole2.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "zar_archive.hpp"
#include "compiled_rdr_ast.hpp"
#include "screen_model.hpp"
#include "ui_vm.hpp"
#include "visual_backend.hpp"
#include "retail_font.hpp"

namespace fs=std::filesystem;
using socom::menu_boot::ZarArchive;
using namespace socom::retail_menu;
using namespace socom::retail_ui;

static std::unique_ptr<ZarArchive> g_ui;
static std::unique_ptr<AssetLocator> g_locator;
static std::unique_ptr<ImageCache> g_images;
static FfmpegVideoSurface g_video;
static ExternalAudioPlayer g_music;
static ExternalAudioPlayer g_movieAudio;
static Screen g_screen;
static UiVm g_vm;
static int g_selected=0;
static std::wstring g_status;
static ULONG_PTR g_gdiplusToken{};
static fs::path g_gameRoot;
static std::wstring g_bootError;
static bool g_reportedVideoFailure=false;
static ULONGLONG g_lastTick=0;

static COLORREF Col(Color3 c,COLORREF f){
    if(c.r==128&&c.g==128&&c.b==128)return f;
    return RGB(std::clamp(c.r,0,255),std::clamp(c.g,0,255),std::clamp(c.b,0,255));
}

static void SyncSelectionFromMenuSpot(){
    int spot=g_vm.GetValve("MenuSpot");
    if(spot<=0)return;
    static const char* names[]={"","new_game_button","multiplayer_button","options_button","tutorial_button","load_game_button"};
    if(spot>=1&&spot<=5){
        for(std::size_t i=0;i<g_screen.controls.size();++i)
            if(g_screen.controls[i].name==names[spot]){g_selected=(int)i;return;}
    }
}

static void StartBackgroundForScreen(){
    g_video.Stop();
    g_movieAudio.Stop();
    g_music.Stop();
    g_bootError.clear();
    g_reportedVideoFailure=false;

    if(!g_locator){
        g_bootError=L"Asset locator is not initialized.";
        return;
    }

    const bool looping=(g_screen.backgroundType=="MPEG_LOOPING");
    const bool cinematic=(g_screen.backgroundType=="MPEG");
    bool loopingMovieAudioStarted=false;

    if(looping||cinematic){
        auto movie=g_locator->Resolve(g_screen.backgroundFile);

        if(movie.empty()){
            g_bootError=L"Could not resolve retail movie: "+Wide(g_screen.backgroundFile);
        }else if(!g_video.Start(movie,looping)){
            g_bootError=L"Could not start movie decoder for:\n"+movie.wstring()+
                L"\n\n"+g_video.LastError();
        }else if(cinematic){
            // Intro/cinematic audio comes directly from the PSS.
            g_movieAudio.Start(movie,false);
        }else if(looping){
            // SOCOM's retail main-menu loop carries its soundtrack in the
            // MENULOOP PSS.  Play the movie's audio while our video decoder
            // supplies the frames.
            {
                const auto decodedMenuAudio=
                    fs::temp_directory_path()/
                    L"socom-native-audio-cache"/
                    L"MENULOOP.wav";

                if(fs::exists(decodedMenuAudio))
                    loopingMovieAudioStarted=
                        g_movieAudio.Start(decodedMenuAudio,true);
                else
                    loopingMovieAudioStarted=
                        g_movieAudio.Start(movie,true);
            }
        }
    }

    // Fallback retained for retail screens whose looping PSS has no audio.
    if(!cinematic&&!loopingMovieAudioStarted){
        auto theme=g_locator->ResolveVagStoreSound("SMUS021B");
        if(!theme.empty())
            g_music.Start(theme,true);
    }
}

static void Load(const std::string&leaf,bool push=true){
    auto bytes=g_ui->ReadLeaf(leaf);
    CompiledRdr rdr(std::move(bytes));
    (void)push;
    g_screen=ParseScreen(leaf,rdr.ParseRoot());
    g_selected=0;
    if(leaf=="dlgMenu.rdr"&&g_vm.GetValve("MenuSpot")==0)g_vm.valves["MenuSpot"]=1;

    // Retail screen lifecycle: execute every ONSTART animation declared by
    // the screen before the first rendered frame.  dlgMenu.rdr relies on this
    // to position/activate the carousel and initialize MenuSpot.
    if(auto it=g_screen.events.find("ONSTART");it!=g_screen.events.end()){
        for(const auto&animation:it->second)g_vm.Execute(animation,g_screen);
    }

    SyncSelectionFromMenuSpot();
    StartBackgroundForScreen();
    g_status=Wide(g_screen.leaf)+L" | "+Wide(g_screen.backgroundFile);
}


static bool IsSonyIntro(){
    return g_screen.leaf=="dlgIntroCinematic.rdr";
}
static bool IsMainIntro(){
    return g_screen.leaf=="dlgSecondCinematic.rdr";
}
static bool IsCinematic(){
    return IsSonyIntro()||IsMainIntro();
}
static void EnterRetailMenu(){
    g_movieAudio.Stop();
    Load("dlgMenu.rdr",false);
}
static void AdvanceRetailBoot(){
    if(IsSonyIntro()){
        g_movieAudio.Stop();
        Load("dlgSecondCinematic.rdr",false);
    }else if(IsMainIntro()){
        EnterRetailMenu();
    }
}

static void DrainRetailCommands(HWND h){
    RetailMenuCommandRecord rec{};

    // Retail update loop at 0x0039C470 processes the current top record and
    // repeats while pendingCount (+0x820) is nonzero.
    while(g_vm.commandStack.Pop(rec)){
        const auto type=static_cast<RetailMenuCommandType>(rec.type);
        const std::string argument(rec.text);

        switch(type){
        case RetailMenuCommandType::ForwardSwitch:{
            const std::string current=g_screen.leaf;
            g_vm.navigation.OnConsumeForward(current);
            if(!argument.empty())Load(argument,false);
            break;
        }
        case RetailMenuCommandType::BackSwitch:
            if(!argument.empty())Load(argument,false);
            break;

        case RetailMenuCommandType::ActivateButton:
            if(auto*c=FindControl(g_screen,argument))c->activeState=true;
            break;

        case RetailMenuCommandType::EnableButton:
            if(auto*c=FindControl(g_screen,argument))c->enabled=true;
            break;

        case RetailMenuCommandType::DisableButton:
            if(auto*c=FindControl(g_screen,argument))c->enabled=false;
            break;

        default:
            break;
        }
    }

    SyncSelectionFromMenuSpot();
    InvalidateRect(h,nullptr,TRUE);
}

static void ApplyResult(const VmResult&r,HWND h){
    DrainRetailCommands(h);
}

static void RunAction(const std::string&button,HWND h){
    if(g_screen.controls.empty())return;
    auto &c=g_screen.controls[(std::size_t)g_selected];
    auto it=c.buttonActions.find(button);
    if(it==c.buttonActions.end())return;
    auto res=g_vm.Execute(it->second.animation,g_screen);
    ApplyResult(res,h);
    SyncSelectionFromMenuSpot();
    InvalidateRect(h,nullptr,TRUE);
}

static void Back(HWND h){
    if(g_vm.navigation.Empty()){
        PostQuitMessage(0);
        return;
    }

    g_vm.navigation.QueueSwitch(
        g_vm.commandStack,
        RetailNavigationState::kPreviousScreen,
        g_screen.leaf);
    DrainRetailCommands(h);
}

static Gdiplus::RectF LogicalRect(const RECT&r,float x,float y,float w,float h){
    const float sx=(float)(r.right-r.left)/640.0f;
    const float sy=(float)(r.bottom-r.top)/448.0f;
    return Gdiplus::RectF(x*sx,y*sy,w*sx,h*sy);
}

static std::pair<float,float> ResolveObjectPosition(const Object&o,int depth=0){
    float x=o.x,y=o.y;
    if(depth>12||o.parent.empty())return {x,y};
    if(auto*p=FindObject(g_screen,o.parent)){
        auto parent=ResolveObjectPosition(*p,depth+1);
        x+=parent.first;
        y+=parent.second;
    }
    return {x,y};
}

static std::pair<float,float> ResolveControlPosition(const Control&c,int depth=0){
    float x=c.x,y=c.y;

    if(depth>12||c.parent.empty())
        return {x,y};

    if(auto*o=FindObject(g_screen,c.parent)){
        auto parent=ResolveObjectPosition(*o,depth+1);
        x+=parent.first;
        y+=parent.second;
        return {x,y};
    }

    if(auto*p=FindControl(g_screen,c.parent)){
        if(p!=&c){
            auto parent=ResolveControlPosition(*p,depth+1);
            x+=parent.first;
            y+=parent.second;
        }
    }

    return {x,y};
}

static void DrawImageObject(Gdiplus::Graphics&g,const RECT&r,const Object&o){
    if(!o.activeState||o.filename.empty()||!g_images)return;
    auto img=g_images->Get(o.filename);
    if(!img)return;

    float w=o.w>0?(float)o.w:(float)img->GetWidth();
    float h=o.h>0?(float)o.h:(float)img->GetHeight();
    // Runtime 11.11:
    // Retail XSIZE/YSIZE already describe the final 640x448 logical extent.
    // Do not multiply IMAGE objects by SPEC/SCALE a second time.
    // This restores the original ~470x216 SplashLogo and full-size arrows.
    auto pos=ResolveObjectPosition(o);
    auto q=LogicalRect(r,pos.first,pos.second,w,h);

    Gdiplus::ImageAttributes attrs;
    Gdiplus::ColorMatrix cm={
        1,0,0,0,0,
        0,1,0,0,0,
        0,0,1,0,0,
        0,0,0,std::clamp(o.opacity,0.0f,1.0f),0,
        0,0,0,0,1
    };
    attrs.SetColorMatrix(&cm);
    g.DrawImage(img,q,0,0,(Gdiplus::REAL)img->GetWidth(),(Gdiplus::REAL)img->GetHeight(),
                Gdiplus::UnitPixel,&attrs);
}

static void DrawRetailText(
    Gdiplus::Graphics&g,const RECT&r,float x,float y,const std::string&txt,
    float scale,Color3 col,float opacity,bool centered=true)
{
    auto w=Wide(txt);

    // Retail front end uses bitmap face "myriad" from fonts.rdr.
    if(g_images){
        if(auto atlas=g_images->Get("myriad_font.tif")){
            if(RetailBitmapFont::Draw(g,atlas,r,x,y,w,
                std::max(scale,0.01f),
                col.r,col.g,col.b,opacity,centered))
                return;
        }
    }

    // Only a fallback for missing retail font assets.
    const float sx=(float)(r.right-r.left)/640.0f;
    const float sy=(float)(r.bottom-r.top)/448.0f;
    float px=x*sx,py=y*sy;
    float fontPx=std::max(12.0f,28.0f*std::max(scale,0.35f)*sy);
    Gdiplus::FontFamily family(L"Arial");
    Gdiplus::Font font(&family,fontPx,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        (BYTE)(255*std::clamp(opacity,0.0f,1.0f)),
        (BYTE)std::clamp(col.r,0,255),
        (BYTE)std::clamp(col.g,0,255),
        (BYTE)std::clamp(col.b,0,255)));
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(centered?Gdiplus::StringAlignmentCenter:Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF box(centered?px-220*sx:px,py-fontPx,centered?440*sx:350*sx,fontPx*2.0f);
    g.DrawString(w.c_str(),-1,&font,box,&fmt,&brush);
}

static void DrawScreen(HWND h,HDC dc,const RECT&r){
    const bool videoDrawn=g_video.Draw(dc,r);
    if(!videoDrawn){
        HBRUSH bg=CreateSolidBrush(RGB(0,0,0));
        FillRect(dc,&r,bg);DeleteObject(bg);
    }

    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    if(IsCinematic()&&!videoDrawn){
        Gdiplus::FontFamily fam(L"Segoe UI");
        Gdiplus::Font font(&fam,18.0f,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush(Gdiplus::Color(220,210,215,220));
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);

        std::wstring status=L"Loading retail intro...";
        if(!g_bootError.empty())status=g_bootError;
        else if(g_video.Started())
            status=L"Preparing / decoding "+g_video.MoviePath().filename().wstring()+L"...";

        Gdiplus::RectF box(40.0f,(Gdiplus::REAL)(r.bottom/2-30),
                           (Gdiplus::REAL)(r.right-80),60.0f);
        g.DrawString(status.c_str(),-1,&font,box,&fmt,&brush);
    }

    // Original retail image objects.
    bool logoRendered=false;
    for(const auto&o:g_screen.objects){
        if(o.type!="IMAGE")continue;
        if(_stricmp(o.name.c_str(),"SplashLogo")==0){
            auto img=g_images?g_images->Get(o.filename):nullptr;
            logoRendered=(img!=nullptr);
        }
        DrawImageObject(g,r,o);
    }

    // No fabricated logo fallback: retail assets only.

    // Original retail text objects.
    for(const auto&o:g_screen.objects){
        if(!o.activeState||o.type!="TEXT"||o.caption.empty())continue;
        auto pos=ResolveObjectPosition(o);
        DrawRetailText(g,r,pos.first,pos.second,o.caption,o.scale,o.color,o.opacity,o.hCentered);
    }

    // RDR-driven buttons.
    for(std::size_t i=0;i<g_screen.controls.size();++i){
        const auto&c=g_screen.controls[i];
        if(!c.activeState)continue;
        bool selected=(int)i==g_selected;
        const auto&st=selected?c.active:(c.enabled?c.normal:c.disabled);

        Color3 color=st.text;

        // The retail capture is a muted yellow/gold, not the saturated
        // orange contained in our current active-style interpretation.
        if(selected){
            color=Color3{205,175,75};
        }else if(color.r==128&&color.g==128&&color.b==128){
            color=Color3{52,56,78};
        }

        auto pos=ResolveControlPosition(c);

        // Runtime 11.14 main-menu parent translation:
        // dlgMenu BUTTON positions are relative to the retail menu group.
        // The remaining authored group offset is +50 logical X pixels.
        if(_stricmp(g_screen.leaf.c_str(),"dlgMenu.rdr")==0){
            pos.first+=50.0f;

            // Runtime 11.15:
            // Center the three-button stack vertically between the retail
            // arrow textures.  13 logical pixels is ~22 pixels at the
            // current 1280x768 presentation.
            pos.second+=13.0f;
        }

        DrawRetailText(
            g,r,
            pos.first,pos.second,
            c.caption,st.scale,color,c.opacity,true);
    }
}

static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:
        g_lastTick=GetTickCount64();
        SetTimer(h,1,15,nullptr);
        return 0;
    case WM_TIMER:{
        const ULONGLONG now=GetTickCount64();
        float dt=g_lastTick?float(now-g_lastTick)/1000.0f:0.016f;
        g_lastTick=now;
        dt=std::clamp(dt,0.0f,0.1f);
        AdvanceAnimations(g_screen,dt);

        if(IsCinematic()){
            if(!g_bootError.empty()){
                if(!g_reportedVideoFailure){
                    g_reportedVideoFailure=true;
                    MessageBoxW(h,g_bootError.c_str(),
                        L"SOCOM intro movie could not start",
                        MB_OK|MB_ICONWARNING);
                    AdvanceRetailBoot();
                    InvalidateRect(h,nullptr,TRUE);
                    return 0;
                }
            }else if(g_video.TimedOutWaitingForFirstFrame(10000)){
                if(!g_reportedVideoFailure){
                    g_reportedVideoFailure=true;
                    std::wstring msg=
                        L"FFmpeg started but produced no video frame within 10 seconds.\n\nOriginal movie:\n"+
                        g_video.MoviePath().wstring()+
                        L"\n\nTemporary PC cache:\n"+
                        g_video.CachedMoviePath().wstring()+
                        L"\n\nThe runtime will continue to the next retail boot stage.";
                    MessageBoxW(h,msg.c_str(),
                        L"SOCOM intro decode timeout",
                        MB_OK|MB_ICONWARNING);
                    AdvanceRetailBoot();
                    InvalidateRect(h,nullptr,TRUE);
                    return 0;
                }
            }else if(g_video.Finished()){
                AdvanceRetailBoot();
                InvalidateRect(h,nullptr,TRUE);
                return 0;
            }
        }

        InvalidateRect(h,nullptr,FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if(IsMainIntro()){
            // Retail dlgSecondCinematic.rdr maps START, face buttons,
            // directions and all shoulder buttons to goto_menu.
            EnterRetailMenu();
            InvalidateRect(h,nullptr,TRUE);
            return 0;
        }

        if(IsSonyIntro()){
            // dlgIntroCinematic.rdr has no retail button-skip mapping.
            return 0;
        }

        if(w==VK_DOWN||w==VK_RIGHT)RunAction(w==VK_DOWN?"DOWN":"RIGHT",h);
        else if(w==VK_UP||w==VK_LEFT)RunAction(w==VK_UP?"UP":"LEFT",h);
        else if(w==VK_RETURN||w==VK_SPACE)RunAction("CROSS",h);
        else if(w==VK_ESCAPE||w=='Q')Back(h);
        return 0;
    case WM_ERASEBKGND:
        // DrawScreen paints the entire client area.  Suppressing the class
        // erase prevents a black flash between the MPEG frame and UI pass.
        return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps{};
        HDC dc=BeginPaint(h,&ps);
        RECT r{};GetClientRect(h,&r);

        // Compose the movie frame and every retail UI object off-screen, then
        // present once.  The old direct-to-window path exposed intermediate
        // frames at the 15 ms timer cadence and made menu selections flicker.
        HDC backDc=CreateCompatibleDC(dc);

        int backWidth=static_cast<int>(r.right-r.left);
        int backHeight=static_cast<int>(r.bottom-r.top);
        if(backWidth<1) backWidth=1;
        if(backHeight<1) backHeight=1;

        HBITMAP backBitmap=CreateCompatibleBitmap(
            dc,backWidth,backHeight);

        if(backDc&&backBitmap){
            HGDIOBJ oldBitmap=SelectObject(backDc,backBitmap);
            DrawScreen(h,backDc,r);
            BitBlt(dc,0,0,r.right-r.left,r.bottom-r.top,
                   backDc,0,0,SRCCOPY);
            SelectObject(backDc,oldBitmap);
        }else{
            DrawScreen(h,dc,r);
        }

        if(backBitmap)DeleteObject(backBitmap);
        if(backDc)DeleteDC(backDc);
        EndPaint(h,&ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h,1);g_video.Stop();g_movieAudio.Stop();g_music.Stop();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

static fs::path FindUiReader(const fs::path&root){
    for(const auto&e:fs::recursive_directory_iterator(root,fs::directory_options::skip_permission_denied)){
        if(!e.is_regular_file())continue;
        if(_stricmp(e.path().filename().string().c_str(),"readerc.zar")!=0)continue;
        std::string p=e.path().parent_path().string();
        for(char&c:p)c=(char)tolower((unsigned char)c);
        if(p.find("ui")!=std::string::npos)return e.path();
    }
    return{};
}

int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR,int){
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc<2){
        MessageBoxW(nullptr,
            L"Usage: socom_retail_ui.exe <game-root> [UI\\readerc.zar] [start-screen.rdr]",
            L"SOCOM Retail UI v11.15",MB_OK);return 1;
    }

    try{
        Gdiplus::GdiplusStartupInput gdiplusInput;
        if(Gdiplus::GdiplusStartup(&g_gdiplusToken,&gdiplusInput,nullptr)!=Gdiplus::Ok)
            throw std::runtime_error("GDI+ startup failed");

        g_gameRoot=fs::path(argv[1]);
        if(!fs::exists(g_gameRoot))throw std::runtime_error("game root does not exist");

        fs::path uiReader;
        if(argc>=3&&fs::exists(fs::path(argv[2])))uiReader=fs::path(argv[2]);
        else uiReader=FindUiReader(g_gameRoot);
        if(uiReader.empty())throw std::runtime_error("UI readerc.zar not found");

        g_locator=std::make_unique<AssetLocator>(g_gameRoot);
        g_images=std::make_unique<ImageCache>(*g_locator);
        g_ui=std::make_unique<ZarArchive>(uiReader.string());

        std::string start=argc>=4?fs::path(argv[3]).string():"dlgMenu.rdr";
        Load(start,false);

        WNDCLASSW wc{};
        wc.lpfnWndProc=Proc;wc.hInstance=hi;wc.lpszClassName=L"SocomRetailUiV10";
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);

        // Runtime 11.12:
        // Keep the original 640x448 logical coordinate system, while matching
        // the aspect seen in the supplied retail reference capture.
        // 1280x768 also removes the excessive vertical stretch from v11.10/11.11.
        RECT desired{0,0,1280,768};
        AdjustWindowRect(&desired,WS_OVERLAPPEDWINDOW,FALSE);
        HWND h=CreateWindowExW(0,wc.lpszClassName,L"SOCOM Native - Retail Front End v11.15",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,
            desired.right-desired.left,desired.bottom-desired.top,
            nullptr,nullptr,hi,nullptr);
        if(!h)throw std::runtime_error("CreateWindowEx failed");

        // Consume commands produced by the initial screen's ONSTART scripts.
        DrainRetailCommands(h);

        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}

        g_video.Stop();g_movieAudio.Stop();g_music.Stop();
        g_images.reset();g_locator.reset();g_ui.reset();
        if(g_gdiplusToken)Gdiplus::GdiplusShutdown(g_gdiplusToken);
        LocalFree(argv);
        return(int)msg.wParam;
    }catch(const std::exception&e){
        MessageBoxA(nullptr,e.what(),"SOCOM Retail UI v11.15 error",MB_OK|MB_ICONERROR);
        if(g_gdiplusToken)Gdiplus::GdiplusShutdown(g_gdiplusToken);
        if(argv)LocalFree(argv);return 1;
    }
}
