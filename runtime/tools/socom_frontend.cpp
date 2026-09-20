#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct MissionEntry {
    int number{};
    std::wstring title;
    fs::path pack;
};

enum class Page { MainMenu, MissionSelect };

struct FrontendState {
    HWND hwnd{};
    HFONT titleFont{};
    HFONT normalFont{};
    HFONT smallFont{};
    std::vector<MissionEntry> missions;
    Page page{Page::MainMenu};
    int selectedMain{};
    int selectedMission{};
    fs::path runtimeExe;
    fs::path playerPack;
    fs::path manifestDir;
    std::wstring status;
};

FrontendState* gState = nullptr;

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

static std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

static std::wstring Quote(const fs::path& p) {
    std::wstring s = p.wstring();
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'\"') out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

static std::vector<std::wstring> Split(const std::wstring& s, wchar_t delim) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == delim) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

static std::vector<MissionEntry> LoadManifest(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open campaign manifest: " + path.string());

    std::vector<MissionEntry> out;
    std::string line;
    const fs::path base = path.parent_path();
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto fields = Split(Widen(line), L'|');
        if (fields.size() < 3) continue;
        MissionEntry m;
        m.number = std::stoi(fields[0]);
        m.title = fields[1];
        fs::path p(fields[2]);
        if (p.is_relative()) p = base / p;
        m.pack = fs::weakly_canonical(p);
        if (fs::exists(m.pack)) out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const MissionEntry& a, const MissionEntry& b){ return a.number < b.number; });
    if (out.empty()) throw std::runtime_error("campaign manifest contains no usable mission packs");
    return out;
}

static bool LaunchMission(FrontendState& s) {
    if (s.selectedMission < 0 || s.selectedMission >= static_cast<int>(s.missions.size())) return false;
    const MissionEntry& m = s.missions[static_cast<std::size_t>(s.selectedMission)];
    if (!fs::exists(s.runtimeExe)) {
        s.status = L"Runtime executable not found.";
        InvalidateRect(s.hwnd, nullptr, TRUE);
        return false;
    }
    if (!fs::exists(m.pack)) {
        s.status = L"Mission pack is missing.";
        InvalidateRect(s.hwnd, nullptr, TRUE);
        return false;
    }

    std::wstring cmd = Quote(s.runtimeExe) + L" --pack " + Quote(m.pack);
    if (!s.playerPack.empty() && fs::exists(s.playerPack)) cmd += L" --player " + Quote(s.playerPack);

    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    ShowWindow(s.hwnd, SW_HIDE);
    const BOOL ok = CreateProcessW(
        s.runtimeExe.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        s.runtimeExe.parent_path().c_str(),
        &si,
        &pi);

    if (!ok) {
        ShowWindow(s.hwnd, SW_SHOW);
        SetForegroundWindow(s.hwnd);
        s.status = L"Failed to launch mission runtime.";
        InvalidateRect(s.hwnd, nullptr, TRUE);
        return false;
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);

    ShowWindow(s.hwnd, SW_SHOW);
    SetForegroundWindow(s.hwnd);
    s.status = L"Returned from " + m.title + L" (exit " + std::to_wstring(exitCode) + L")";
    InvalidateRect(s.hwnd, nullptr, TRUE);
    return true;
}

static void DrawCentered(HDC dc, RECT r, const std::wstring& text, HFONT font, COLORREF color) {
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}

static void PaintMainMenu(FrontendState& s, HDC dc, const RECT& client) {
    RECT title{0, 54, client.right, 118};
    DrawCentered(dc, title, L"SOCOM NATIVE", s.titleFont, RGB(236, 241, 244));
    RECT subtitle{0, 108, client.right, 144};
    DrawCentered(dc, subtitle, L"U.S. NAVY SEALS — SINGLE PLAYER", s.smallFont, RGB(133, 164, 184));

    static const wchar_t* rows[] = {L"NEW GAME", L"LOAD GAME", L"ONLINE", L"OPTIONS"};
    const int rowH = 54;
    const int top = 210;
    const int left = client.right / 2 - 190;
    const int right = client.right / 2 + 190;
    for (int i=0;i<4;++i) {
        RECT row{left, top+i*rowH, right, top+(i+1)*rowH-5};
        if (i==s.selectedMain) {
            HBRUSH sel=CreateSolidBrush(RGB(34,62,78)); FillRect(dc,&row,sel); DeleteObject(sel);
            FrameRect(dc,&row,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        }
        DrawCentered(dc,row,rows[i],s.normalFont,i==s.selectedMain?RGB(255,204,51):RGB(190,199,205));
    }
    RECT footer{20,client.bottom-74,client.right-20,client.bottom-42};
    DrawCentered(dc,footer,L"UP / DOWN   Select      ENTER   Confirm      ESC   Quit",s.smallFont,RGB(170,181,188));
}

static void PaintMissionSelect(FrontendState& s, HDC dc, const RECT& client) {
    RECT title{0, 24, client.right, 82};
    DrawCentered(dc,title,L"AREA OF OPERATION",s.titleFont,RGB(236,241,244));
    RECT subtitle{0,74,client.right,112};
    DrawCentered(dc,subtitle,L"MISSION SELECTION",s.smallFont,RGB(133,164,184));

    const int listTop=126, rowH=36;
    const LONG listLeft = std::max<LONG>(40L, client.right / 2 - 350L);
    const LONG listRight = std::min<LONG>(client.right - 40L, client.right / 2 + 350L);
    const int left = static_cast<int>(listLeft);
    const int right = static_cast<int>(listRight);
    for (std::size_t i=0;i<s.missions.size();++i) {
        RECT row{left,listTop+static_cast<int>(i)*rowH,right,listTop+static_cast<int>(i+1)*rowH-2};
        if (static_cast<int>(i)==s.selectedMission) {
            HBRUSH sel=CreateSolidBrush(RGB(34,62,78)); FillRect(dc,&row,sel); DeleteObject(sel);
            FrameRect(dc,&row,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        }
        std::wstringstream label;
        label<<L"MISSION "; if(s.missions[i].number<10)label<<L'0';
        label<<s.missions[i].number<<L"   "<<s.missions[i].title;
        RECT tr=row; tr.left+=18;
        HFONT old=static_cast<HFONT>(SelectObject(dc,s.normalFont)); SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,static_cast<int>(i)==s.selectedMission?RGB(255,204,51):RGB(186,198,207));
        DrawTextW(dc,label.str().c_str(),-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE); SelectObject(dc,old);
    }
    RECT footer{20,client.bottom-74,client.right-20,client.bottom-42};
    DrawCentered(dc,footer,L"UP / DOWN   Select Mission      ENTER   Deploy      ESC   Back",s.smallFont,RGB(170,181,188));
}

static void Paint(FrontendState& s) {
    PAINTSTRUCT ps{}; HDC dc=BeginPaint(s.hwnd,&ps); RECT client{}; GetClientRect(s.hwnd,&client);
    HBRUSH bg=CreateSolidBrush(RGB(8,13,18)); FillRect(dc,&client,bg); DeleteObject(bg);
    if (s.page==Page::MainMenu) PaintMainMenu(s,dc,client); else PaintMissionSelect(s,dc,client);
    if (!s.status.empty()) {
        RECT status{20,client.bottom-40,client.right-20,client.bottom-12};
        DrawCentered(dc,status,s.status,s.smallFont,RGB(117,175,126));
    }
    EndPaint(s.hwnd,&ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    FrontendState* s=gState;
    switch(msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: if(s)Paint(*s); return 0;
    case WM_KEYDOWN:
        if(!s)break;
        s->status.clear();
        if(s->page==Page::MainMenu) {
            if(wp==VK_UP){s->selectedMain=std::max(0,s->selectedMain-1);InvalidateRect(hwnd,nullptr,TRUE);return 0;}
            if(wp==VK_DOWN){s->selectedMain=std::min(3,s->selectedMain+1);InvalidateRect(hwnd,nullptr,TRUE);return 0;}
            if(wp==VK_RETURN){
                if(s->selectedMain==0){s->page=Page::MissionSelect;InvalidateRect(hwnd,nullptr,TRUE);return 0;}
                if(s->selectedMain==1)s->status=L"LOAD GAME: save/profile runtime is not wired yet.";
                else if(s->selectedMain==2)s->status=L"ONLINE: multiplayer remains deferred until single player is complete.";
                else s->status=L"OPTIONS: original dlgOptions_* data has been identified; runtime settings UI is next.";
                InvalidateRect(hwnd,nullptr,TRUE);return 0;
            }
            if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;}
        } else {
            if(wp==VK_UP){s->selectedMission=std::max(0,s->selectedMission-1);InvalidateRect(hwnd,nullptr,TRUE);return 0;}
            if(wp==VK_DOWN){s->selectedMission=std::min(static_cast<int>(s->missions.size())-1,s->selectedMission+1);InvalidateRect(hwnd,nullptr,TRUE);return 0;}
            if(wp==VK_RETURN){LaunchMission(*s);return 0;}
            if(wp==VK_ESCAPE){s->page=Page::MainMenu;InvalidateRect(hwnd,nullptr,TRUE);return 0;}
        }
        break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

struct Args {
    fs::path manifest;
    fs::path runtime;
    fs::path player;
};

static Args ParseArgs() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) throw std::runtime_error("CommandLineToArgvW failed");
    Args out;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--manifest" && i + 1 < argc) out.manifest = argv[++i];
        else if (a == L"--runtime" && i + 1 < argc) out.runtime = argv[++i];
        else if (a == L"--player" && i + 1 < argc) out.player = argv[++i];
    }
    LocalFree(argv);
    if (out.manifest.empty()) throw std::runtime_error("missing --manifest <campaign_manifest.txt>");
    if (out.runtime.empty()) throw std::runtime_error("missing --runtime <socom-native.exe>");
    return out;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        SetProcessDPIAware();
        Args args = ParseArgs();
        FrontendState state;
        gState = &state;
        state.runtimeExe = fs::weakly_canonical(args.runtime);
        if (!args.player.empty()) state.playerPack = fs::weakly_canonical(args.player);
        state.manifestDir = fs::weakly_canonical(args.manifest).parent_path();
        state.missions = LoadManifest(args.manifest);

        state.titleFont = CreateFontW(-42, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Arial");
        state.normalFont = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
        state.smallFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");

        const wchar_t* cls = L"SocomNativeFrontend";
        WNDCLASSW wc{};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = cls;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("RegisterClassW failed");

        RECT r{0,0,900,690};
        AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        state.hwnd = CreateWindowExW(0, cls, L"SOCOM Native - Single Player", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, r.right-r.left, r.bottom-r.top, nullptr, nullptr, instance, nullptr);
        if (!state.hwnd) throw std::runtime_error("CreateWindowExW failed");

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (state.titleFont) DeleteObject(state.titleFont);
        if (state.normalFont) DeleteObject(state.normalFont);
        if (state.smallFont) DeleteObject(state.smallFont);
        gState = nullptr;
        return 0;
    } catch (const std::exception& e) {
        const std::wstring w = Widen(e.what());
        MessageBoxW(nullptr, w.c_str(), L"SOCOM Native Frontend Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
