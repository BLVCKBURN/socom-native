#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <ole2.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

#include "zar_archive.hpp"

namespace socom::retail_ui {
namespace fs=std::filesystem;

inline std::wstring Wide(const std::string&s){
    if(s.empty())return{};

    // Prefer real UTF-8, but retail SOCOM RDR strings can contain legacy
    // Windows-1252 bytes (notably the copyright symbol).
    int n=MultiByteToWideChar(
        CP_UTF8,MB_ERR_INVALID_CHARS,
        s.c_str(),(int)s.size(),nullptr,0);

    UINT cp=CP_UTF8;
    DWORD flags=MB_ERR_INVALID_CHARS;

    if(n<=0){
        cp=1252;
        flags=0;
        n=MultiByteToWideChar(
            cp,flags,
            s.c_str(),(int)s.size(),nullptr,0);
    }

    if(n<=0)return{};

    std::wstring w((std::size_t)n,L'\0');
    MultiByteToWideChar(
        cp,flags,
        s.c_str(),(int)s.size(),w.data(),n);

    return w;
}

class AssetLocator {
public:
    explicit AssetLocator(fs::path root):root_(std::move(root)){
        cacheDir_=fs::temp_directory_path()/L"socom-native-retail-cache";
        std::error_code ec;
        fs::create_directories(cacheDir_,ec);
    }

    fs::path Resolve(const std::string& retail){
        auto it=cache_.find(retail);
        if(it!=cache_.end())return it->second;

        {
            std::error_code ec;
            auto uiCache=fs::temp_directory_path()/L"socom-native-ui-assets";
            fs::path decodedName=fs::path(retail).filename();
            decodedName.replace_extension(L".png");
            auto decoded=uiCache/decodedName;

            if(fs::exists(decoded,ec)&&fs::is_regular_file(decoded,ec)){
                cache_[retail]=decoded;
                return decoded;
            }
        }

        std::string norm=retail;
        for(char&c:norm)if(c=='\\')c='/';

        std::vector<std::string> parts;
        std::string x;
        for(char ch:norm){
            if(ch=='/'){if(!x.empty()){parts.push_back(x);x.clear();}}
            else x.push_back(ch);
        }
        if(!x.empty())parts.push_back(x);

        auto tryParts=[&](const fs::path&base,const std::vector<std::string>&p)->fs::path{
            fs::path cur=base;
            for(const auto&w:p){
                if(!fs::exists(cur)||!fs::is_directory(cur))return{};
                bool found=false;
                for(const auto&e:fs::directory_iterator(cur)){
                    if(_stricmp(e.path().filename().string().c_str(),w.c_str())==0){
                        cur=e.path();found=true;break;
                    }
                }
                if(!found)return{};
            }
            return cur;
        };

        fs::path found;
        if(!parts.empty()&&_stricmp(parts[0].c_str(),"run")==0){
            found=tryParts(root_,parts);
            if(found.empty()){
                std::vector<std::string> tail(parts.begin()+1,parts.end());
                found=tryParts(root_,tail);
            }
        }else{
            found=tryParts(root_,parts);
        }

        const std::string wanted=parts.empty()?retail:parts.back();

        if(found.empty()&&!wanted.empty()){
            for(const auto&e:fs::recursive_directory_iterator(
                    root_,fs::directory_options::skip_permission_denied)){
                if(!e.is_regular_file())continue;
                if(_stricmp(e.path().filename().string().c_str(),wanted.c_str())==0){
                    found=e.path();break;
                }
            }
        }

        if(found.empty()&&!wanted.empty())
            found=ExtractFromAnyZar(wanted);

        cache_[retail]=found;
        return found;
    }

    fs::path ResolveStem(const std::string& stem){
        auto it=stemCache_.find(stem);
        if(it!=stemCache_.end())return it->second;

        fs::path found;
        for(const auto&e:fs::recursive_directory_iterator(
                root_,fs::directory_options::skip_permission_denied)){
            if(!e.is_regular_file())continue;
            if(_stricmp(e.path().stem().string().c_str(),stem.c_str())==0){
                found=e.path();break;
            }
        }

        if(found.empty())
            found=ExtractStemFromAnyZar(stem);

        stemCache_[stem]=found;
        return found;
    }

    fs::path ResolveVagStoreSound(const std::string& stem){
        const std::string cacheKey="vagstore:"+stem;
        auto it=soundCache_.find(cacheKey);
        if(it!=soundCache_.end())return it->second;

        // The retail executable names:
        // RUN\SOUNDS\VAGSTORE.ZAR
        // %s.VPK / %s.VAG (plus lower-case variants).
        fs::path store;
        for(const auto&e:fs::recursive_directory_iterator(
                root_,fs::directory_options::skip_permission_denied)){
            if(!e.is_regular_file())continue;
            if(_stricmp(e.path().filename().string().c_str(),"VAGSTORE.ZAR")==0){
                store=e.path();break;
            }
        }

        fs::path found;
        if(!store.empty()){
            try{
                socom::menu_boot::ZarArchive z(store.string());
                const std::vector<std::string> candidates={
                    stem+".VAG",stem+".vag",stem+".VPK",stem+".vpk"
                };
                for(const auto&name:candidates){
                    std::vector<std::uint8_t> bytes;
                    if(!z.TryReadLeaf(name,bytes))continue;
                    fs::path dst=cacheDir_/fs::path(name).filename();
                    std::ofstream f(dst,std::ios::binary);
                    f.write(reinterpret_cast<const char*>(bytes.data()),
                            static_cast<std::streamsize>(bytes.size()));
                    if(f){found=dst;break;}
                }
            }catch(...){}
        }

        if(found.empty())
            found=ResolveStem(stem);

        soundCache_[cacheKey]=found;
        return found;
    }

private:
    fs::path ExtractFromAnyZar(const std::string&wanted){
        for(const auto&e:fs::recursive_directory_iterator(
                root_,fs::directory_options::skip_permission_denied)){
            if(!e.is_regular_file())continue;
            if(_stricmp(e.path().extension().string().c_str(),".zar")!=0)continue;
            try{
                socom::menu_boot::ZarArchive z(e.path().string());
                std::vector<std::uint8_t> bytes;
                if(!z.TryReadLeaf(wanted,bytes))continue;
                fs::path dst=cacheDir_/fs::path(wanted).filename();
                std::ofstream f(dst,std::ios::binary);
                f.write(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                if(f)return dst;
            }catch(...){}
        }
        return{};
    }

    fs::path ExtractStemFromAnyZar(const std::string&stem){
        for(const auto&e:fs::recursive_directory_iterator(
                root_,fs::directory_options::skip_permission_denied)){
            if(!e.is_regular_file())continue;
            if(_stricmp(e.path().extension().string().c_str(),".zar")!=0)continue;
            try{
                socom::menu_boot::ZarArchive z(e.path().string());
                for(const auto&name:z.LeafNames()){
                    if(_stricmp(fs::path(name).stem().string().c_str(),stem.c_str())!=0)
                        continue;
                    std::vector<std::uint8_t> bytes;
                    if(!z.TryReadLeaf(name,bytes))continue;
                    fs::path dst=cacheDir_/fs::path(name).filename();
                    std::ofstream f(dst,std::ios::binary);
                    f.write(reinterpret_cast<const char*>(bytes.data()),
                            static_cast<std::streamsize>(bytes.size()));
                    if(f)return dst;
                }
            }catch(...){}
        }
        return{};
    }

    fs::path root_,cacheDir_;
    std::map<std::string,fs::path> cache_,stemCache_,soundCache_;
};

class ImageCache {
public:
    explicit ImageCache(AssetLocator&locator):locator_(locator){}
    Gdiplus::Image* Get(const std::string&filename){
        if(filename.empty())return nullptr;
        auto it=images_.find(filename);
        if(it!=images_.end())return it->second.get();

        auto p=locator_.Resolve(filename);
        if(p.empty())return nullptr;

        auto img=std::make_unique<Gdiplus::Bitmap>(p.wstring().c_str(),FALSE);
        if(img->GetLastStatus()!=Gdiplus::Ok)return nullptr;

        auto raw=img.get();
        images_[filename]=std::move(img);
        return raw;
    }
private:
    AssetLocator&locator_;
    std::map<std::string,std::unique_ptr<Gdiplus::Bitmap>> images_;
};

class ExternalAudioPlayer {
public:
    ~ExternalAudioPlayer(){Stop();}

    bool Start(const fs::path&asset,bool loop){
        Stop();
        lastError_.clear();

        if(asset.empty() || !fs::exists(asset)){
            lastError_=L"Audio asset was not found.";
            return false;
        }

        wchar_t ffplay[MAX_PATH]{};
        if(!SearchPathW(nullptr,L"ffplay.exe",nullptr,MAX_PATH,ffplay,nullptr)){
            lastError_=L"ffplay.exe is not on PATH.";
            return false;
        }

        fs::path playable=asset;

        const bool isPss=
            _wcsicmp(asset.extension().c_str(),L".pss")==0;

        const bool isPs2Audio=
            _wcsicmp(asset.extension().c_str(),L".vag")==0 ||
            _wcsicmp(asset.extension().c_str(),L".vpk")==0;

        if(isPss || isPs2Audio){
            wchar_t ffmpeg[MAX_PATH]{};

            if(SearchPathW(nullptr,L"ffmpeg.exe",nullptr,MAX_PATH,ffmpeg,nullptr)){
                auto dir=fs::temp_directory_path()/L"socom-native-audio-cache";
                std::error_code ec;
                fs::create_directories(dir,ec);

                // MENULOOP gets a stable retail-menu cache filename.
                fs::path wav=
                    isPss
                    ? dir/L"MENULOOP.wav"
                    : dir/(asset.stem().wstring()+L".wav");

                bool usable=false;

                if(fs::exists(wav,ec)){
                    auto size=fs::file_size(wav,ec);
                    usable=!ec && size>44;
                }

                if(!usable){
                    std::wstring convert=
                        L"\""+std::wstring(ffmpeg)+
                        L"\" -y -hide_banner -loglevel error "
                        L"-probesize 100M -analyzeduration 100M "
                        L"-i \""+asset.wstring()+
                        L"\" -map 0:a:0? -vn -sn -dn "
                        L"-ac 2 -ar 48000 -c:a pcm_s16le \""+
                        wav.wstring()+L"\"";

                    STARTUPINFOW csi{};
                    csi.cb=sizeof(csi);

                    PROCESS_INFORMATION cpi{};

                    std::vector<wchar_t> mutableConvert(
                        convert.begin(),convert.end());
                    mutableConvert.push_back(L'\0');

                    if(CreateProcessW(
                        nullptr,
                        mutableConvert.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        asset.parent_path().wstring().c_str(),
                        &csi,
                        &cpi)){

                        WaitForSingleObject(cpi.hProcess,INFINITE);

                        DWORD code=1;
                        GetExitCodeProcess(cpi.hProcess,&code);

                        CloseHandle(cpi.hThread);
                        CloseHandle(cpi.hProcess);

                        if(code==0 && fs::exists(wav,ec)){
                            auto size=fs::file_size(wav,ec);
                            usable=!ec && size>44;
                        }
                    }
                }

                if(usable)
                    playable=wav;
            }
        }

        std::wstring cmd=
            L"\""+std::wstring(ffplay)+
            L"\" -hide_banner -loglevel error "
            L"-nodisp -vn -autoexit ";

        if(loop)
            cmd+=L"-loop 0 ";

        cmd+=L"\""+playable.wstring()+L"\"";

        STARTUPINFOW si{};
        si.cb=sizeof(si);

        PROCESS_INFORMATION pi{};

        std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end());
        mutableCmd.push_back(L'\0');

        if(!CreateProcessW(
            nullptr,
            mutableCmd.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            playable.parent_path().wstring().c_str(),
            &si,
            &pi)){

            lastError_=L"Could not launch ffplay.";
            return false;
        }

        process_=pi.hProcess;
        thread_=pi.hThread;

        // Catch immediate decoder/input failures.
        DWORD wait=WaitForSingleObject(process_,250);

        if(wait==WAIT_OBJECT_0){
            DWORD code=1;
            GetExitCodeProcess(process_,&code);

            CloseHandle(process_);
            CloseHandle(thread_);

            process_=nullptr;
            thread_=nullptr;

            lastError_=
                L"ffplay exited immediately with code "+
                std::to_wstring(code)+L".";

            return false;
        }

        return true;
    }

    bool Running()const{
        if(!process_)return false;

        DWORD code=0;
        return
            GetExitCodeProcess(process_,&code) &&
            code==STILL_ACTIVE;
    }

    const std::wstring& LastError()const{
        return lastError_;
    }

    void Stop(){
        if(process_){
            DWORD code=0;

            if(GetExitCodeProcess(process_,&code) &&
               code==STILL_ACTIVE){
                TerminateProcess(process_,0);
            }

            CloseHandle(process_);
            process_=nullptr;
        }

        if(thread_){
            CloseHandle(thread_);
            thread_=nullptr;
        }
    }

private:
    HANDLE process_=nullptr;
    HANDLE thread_=nullptr;
    std::wstring lastError_;
};

class FfmpegVideoSurface {
public:
    ~FfmpegVideoSurface(){Stop();}

    bool Start(const fs::path&movie,bool loop){
        Stop();
        lastError_.clear();

        if(movie.empty() || !fs::exists(movie)){
            lastError_=L"Movie file was not found.";
            return false;
        }

        wchar_t ffmpeg[MAX_PATH]{};
        if(!SearchPathW(nullptr,L"ffmpeg.exe",nullptr,MAX_PATH,ffmpeg,nullptr)){
            lastError_=L"ffmpeg.exe is not on PATH.";
            return false;
        }

        moviePath_=movie;
        frameWidth_=640;
        frameHeight_=loop?448:368;
        auto cacheRoot=fs::temp_directory_path()/L"socom-native-video-cache";
        std::error_code ec;
        fs::create_directories(cacheRoot,ec);
        cachedMovie_=cacheRoot/(movie.stem().wstring()+
            (loop?L"_pc_640x448_loop.mpg":L"_pc_640x368_cinematic.mpg"));

        if(!fs::exists(cachedMovie_) || fs::file_size(cachedMovie_)==0){
            std::wstring transcode=
                L"\""+std::wstring(ffmpeg)+
                L"\" -y -hide_banner -loglevel error "
                L"-probesize 100M -analyzeduration 100M "
                L"-i \""+movie.wstring()+
                L"\" -map 0:v:0 -an -sn -dn ";
            transcode += loop ? L"-vf scale=640:448:flags=lanczos "
                              : L"-vf scale=640:368:flags=lanczos ";
            transcode += L"-c:v mpeg2video -q:v 2 -f mpeg \""+
                cachedMovie_.wstring()+L"\"";

            if(!RunAndWait(transcode,movie.parent_path())){
                lastError_=L"FFmpeg could not transcode the retail PSS video:\n"+
                    movie.wstring();
                return false;
            }
        }

        return StartRawDecode(ffmpeg,cachedMovie_,loop);
    }

    void Stop(){
        running_=false;
        if(process_){
            DWORD code=0;
            if(GetExitCodeProcess(process_,&code)&&code==STILL_ACTIVE)
                TerminateProcess(process_,0);
        }
        if(pipe_){CloseHandle(pipe_);pipe_=nullptr;}
        if(worker_.joinable())worker_.join();
        if(threadHandle_){CloseHandle(threadHandle_);threadHandle_=nullptr;}
        if(process_){CloseHandle(process_);process_=nullptr;}
        haveFrame_=false;
        started_=false;
        finished_=false;
        failed_=false;
        startTick_=0;
        frameCount_=0;
    }

    bool Started()const{return started_;}
    bool Finished()const{return started_&&finished_;}
    bool Failed()const{return failed_;}

    bool HasFrame()const{
        std::lock_guard<std::mutex> lock(mu_);
        return haveFrame_;
    }

    bool TimedOutWaitingForFirstFrame(std::uint64_t timeoutMs=10000)const{
        if(!started_ || HasFrame()) return false;
        return GetTickCount64()-startTick_ >= timeoutMs;
    }

    const std::wstring& LastError()const{return lastError_;}
    const fs::path& MoviePath()const{return moviePath_;}
    const fs::path& CachedMoviePath()const{return cachedMovie_;}
    std::uint64_t FrameCount()const{return frameCount_;}

    bool Draw(HDC dc,const RECT&r){
        std::vector<unsigned char> copy;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if(!haveFrame_)return false;
            copy=frame_;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=frameWidth_;
        bmi.bmiHeader.biHeight=-frameHeight_;
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;

        const int clientW=r.right-r.left;
        const int clientH=r.bottom-r.top;

        int dstY=0;
        int dstH=clientH;
        if(frameHeight_==368){
            dstY=(int)((40.0f/448.0f)*clientH);
            dstH=(int)((368.0f/448.0f)*clientH);
        }

        StretchDIBits(dc,0,dstY,clientW,dstH,
            0,0,frameWidth_,frameHeight_,copy.data(),&bmi,DIB_RGB_COLORS,SRCCOPY);
        return true;
    }

private:
    static bool RunAndWait(const std::wstring&cmd,const fs::path&cwd){
        STARTUPINFOW si{};
        si.cb=sizeof(si);
        PROCESS_INFORMATION pi{};

        std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end());
        mutableCmd.push_back(L'\0');

        if(!CreateProcessW(nullptr,mutableCmd.data(),nullptr,nullptr,FALSE,
            CREATE_NO_WINDOW,nullptr,cwd.wstring().c_str(),&si,&pi))
            return false;

        WaitForSingleObject(pi.hProcess,INFINITE);
        DWORD code=1;
        GetExitCodeProcess(pi.hProcess,&code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return code==0;
    }

    bool StartRawDecode(const wchar_t*ffmpeg,const fs::path&source,bool loop){
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        HANDLE readPipe=nullptr,writePipe=nullptr;
        if(!CreatePipe(&readPipe,&writePipe,&sa,1024*1024)){
            lastError_=L"CreatePipe failed.";
            return false;
        }
        SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);

        std::wstring cmd=L"\""+std::wstring(ffmpeg)+
            L"\" -hide_banner -loglevel error ";
        if(loop)cmd+=L"-stream_loop -1 ";
        cmd+=L"-re -i \""+source.wstring()+
            L"\" -map 0:v:0 -an -sn -dn "
            L"-pix_fmt bgra -f rawvideo pipe:1";

        STARTUPINFOW si{};
        si.cb=sizeof(si);
        si.dwFlags=STARTF_USESTDHANDLES;
        si.hStdOutput=writePipe;
        si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
        si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end());
        mutableCmd.push_back(L'\0');

        if(!CreateProcessW(nullptr,mutableCmd.data(),nullptr,nullptr,TRUE,
            CREATE_NO_WINDOW,nullptr,source.parent_path().wstring().c_str(),&si,&pi)){
            CloseHandle(readPipe);CloseHandle(writePipe);
            lastError_=L"Could not launch FFmpeg raw-video decoder.";
            return false;
        }

        CloseHandle(writePipe);
        pipe_=readPipe;
        process_=pi.hProcess;
        threadHandle_=pi.hThread;
        running_=true;
        finished_=false;
        failed_=false;
        started_=true;
        startTick_=GetTickCount64();
        frame_.resize((std::size_t)frameWidth_*frameHeight_*4);

        worker_=std::thread([this]{
            std::vector<unsigned char> temp((std::size_t)frameWidth_*frameHeight_*4);
            while(running_){
                std::size_t got=0;
                while(got<temp.size()&&running_){
                    DWORD n=0;
                    if(!ReadFile(pipe_,temp.data()+got,
                        (DWORD)(temp.size()-got),&n,nullptr)||n==0){
                        running_=false;
                        break;
                    }
                    got+=n;
                }

                if(got==temp.size()){
                    {
                        std::lock_guard<std::mutex> lock(mu_);
                        frame_=temp;
                        haveFrame_=true;
                    }
                    ++frameCount_;
                }
            }

            if(started_){
                DWORD code=0;
                if(process_&&GetExitCodeProcess(process_,&code)&&
                   code!=0&&code!=STILL_ACTIVE)
                    failed_=true;
                finished_=true;
            }
        });

        return true;
    }

    mutable std::mutex mu_;
    std::vector<unsigned char> frame_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> failed_{false};
    std::atomic<std::uint64_t> frameCount_{0};
    bool started_=false;
    bool haveFrame_=false;
    std::uint64_t startTick_=0;
    HANDLE pipe_=nullptr,process_=nullptr,threadHandle_=nullptr;
    fs::path moviePath_,cachedMovie_;
    int frameWidth_=640,frameHeight_=368;
    std::wstring lastError_;
};} // namespace socom::retail_ui
