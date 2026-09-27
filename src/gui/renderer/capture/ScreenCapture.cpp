#include "gui/renderer/capture/ScreenCapture.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

std::mutex& RecorderMutex()
{
    static std::mutex m;
    return m;
}

std::atomic<bool>& RecordingFlag()
{
    static std::atomic<bool> flag{ false };
    return flag;
}

HANDLE& RecorderProcess()
{
    static HANDLE h = nullptr;
    return h;
}
HANDLE& RecorderStdin()
{
    static HANDLE h = nullptr;
    return h;
}
std::string& RecorderPathWin()
{
    static std::string path;
    return path;
}

std::string Timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    return std::format("{:04}{:02}{:02}_{:02}{:02}{:02}", tm.tm_year + 1900, tm.tm_mon + 1,
        tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void EnsureParentDir(const std::string& path)
{
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
}

std::string OutputDir()
{
    std::string dir = cfg::capture::output_dir;
    if (dir.empty())
        dir = "captures";
    // Drop trailing slashes so path joining stays clean.
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    return dir;
}

bool HasLowerSuffix(const std::string& path, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    if (path.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(path[path.size() - n + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b)
            return false;
    }
    return true;
}


} // namespace

std::string ScreenCapture::DefaultScreenshotPath()
{
    const char* ext = ".bmp";
    return OutputDir() + std::format("/shot_{}{}", Timestamp(), ext);
}

std::string ScreenCapture::DefaultRecordingPath()
{
    return OutputDir() + std::format("/rec_{}.mp4", Timestamp());
}

bool ScreenCapture::CaptureScreenshot(const std::string& path)
{
    std::string out = path.empty() ? DefaultScreenshotPath() : path;
    EnsureParentDir(out);

    const int w = GetSystemMetrics(SM_CXSCREEN);
    const int h = GetSystemMetrics(SM_CYSCREEN);
    if (w <= 0 || h <= 0)
        return false;

    HDC screen_dc = GetDC(nullptr);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bmp = CreateCompatibleBitmap(screen_dc, w, h);
    HGDIOBJ old = SelectObject(mem_dc, bmp);
    // SRCCOPY from the desktop DC captures the composited output, i.e. game
    // below plus our layered overlay on top.
    const BOOL ok = BitBlt(mem_dc, 0, 0, w, h, screen_dc, 0, 0, SRCCOPY | CAPTUREBLT);
    SelectObject(mem_dc, old);

    bool saved = false;
    if (ok) {
        BITMAPINFOHEADER bi{};
        bi.biSize = sizeof(bi);
        bi.biWidth = w;
        bi.biHeight = -h; // top-down
        bi.biPlanes = 1;
        bi.biBitCount = 24;
        bi.biCompression = BI_RGB;
        const size_t stride = (static_cast<size_t>(w) * 3 + 3) & ~size_t{ 3 };
        std::vector<unsigned char> pixels(stride * static_cast<size_t>(h));
        if (GetDIBits(mem_dc, bmp, 0, static_cast<UINT>(h), pixels.data(),
                reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS)) {
            BITMAPFILEHEADER bf{};
            bf.bfType = 0x4D42;
            bf.bfOffBits = sizeof(bf) + sizeof(bi);
            bf.bfSize = bf.bfOffBits + static_cast<DWORD>(pixels.size());
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            if (f.good()) {
                f.write(reinterpret_cast<const char*>(&bf), sizeof(bf));
                f.write(reinterpret_cast<const char*>(&bi), sizeof(bi));
                f.write(reinterpret_cast<const char*>(pixels.data()),
                    static_cast<std::streamsize>(pixels.size()));
                saved = static_cast<bool>(f);
            }
        }
    }
    DeleteObject(bmp);
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
    if (saved)
        LOGF(INFO, "[capture] screenshot (overlay+game) saved to '{}'", out);
    else
        LOGF(WARNING, "[capture] screenshot failed");
    return saved;
}

bool ScreenCapture::StartRecording(const std::string& path,int fps) {
    std::lock_guard lock(RecorderMutex());
    if(RecorderProcess()) {
        if(WaitForSingleObject(RecorderProcess(),0)!=WAIT_OBJECT_0)return false;
        CloseHandle(RecorderProcess());RecorderProcess()=nullptr;
        if(RecorderStdin()) { CloseHandle(RecorderStdin());RecorderStdin()=nullptr; }
        RecordingFlag().store(false);
    }
    const std::string out=path.empty()?DefaultRecordingPath():path;
    EnsureParentDir(out);
    if(std::filesystem::exists(out))return false;
    wchar_t executable[32768]{};
    if(!SearchPathW(nullptr,L"ffmpeg.exe",nullptr,std::size(executable),executable,nullptr)) {
        LOGF(WARNING,"Install ffmpeg and add it to PATH to record video");return false;
    }
    const auto quote=[](const std::wstring& arg) {
        std::wstring result=L"\"";size_t slashes=0;
        for(wchar_t c:arg) {
            if(c==L'\\') { ++slashes;continue; }
            result.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;
        }
        result.append(slashes*2,L'\\');return result+L"\"";
    };
    std::wstring cmd=quote(executable)+L" -n -f gdigrab -framerate "+std::to_wstring(std::clamp(fps,1,240))+
        L" -i desktop -vf pad=ceil(iw/2)*2:ceil(ih/2)*2 -c:v libx264 -preset veryfast -pix_fmt yuv420p "+
        quote(std::filesystem::absolute(out).wstring());
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    HANDLE input=nullptr,write=nullptr;
    if(!CreatePipe(&input,&write,&security,0))return false;
    if(!SetHandleInformation(write,HANDLE_FLAG_INHERIT,0)) { CloseHandle(input);CloseHandle(write);return false; }
    HANDLE output=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    if(output==INVALID_HANDLE_VALUE) { CloseHandle(input);CloseHandle(write);return false; }
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input;startup.hStdOutput=output;startup.hStdError=output;
    PROCESS_INFORMATION process{};
    const bool started=CreateProcessW(executable,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,
                                     nullptr,nullptr,&startup,&process)!=FALSE;
    CloseHandle(input);CloseHandle(output);
    if(!started) { CloseHandle(write);return false; }
    CloseHandle(process.hThread);
    if(WaitForSingleObject(process.hProcess,200)==WAIT_OBJECT_0) {
        CloseHandle(process.hProcess);CloseHandle(write);
        LOGF(WARNING,"ffmpeg exited before recording could start");return false;
    }
    RecorderProcess()=process.hProcess;RecorderStdin()=write;
    RecorderPathWin()=out;RecordingFlag().store(true);return true;
}

bool ScreenCapture::RecordEntireScreen(const std::string& path, int fps)
{
    return StartRecording(path, fps);
}

void ScreenCapture::StopRecording() {
    std::lock_guard lock(RecorderMutex());
    HANDLE process=RecorderProcess();if(!process)return;
    if(RecorderStdin()) {
        DWORD written=0;WriteFile(RecorderStdin(),"q\n",2,&written,nullptr);
        CloseHandle(RecorderStdin());RecorderStdin()=nullptr;
    }
    if(WaitForSingleObject(process,5000)!=WAIT_OBJECT_0) {
        LOGF(WARNING,"ffmpeg did not stop cleanly; the video may be incomplete");
        TerminateProcess(process,1);WaitForSingleObject(process,2000);
    }
    CloseHandle(process);RecorderProcess()=nullptr;RecordingFlag().store(false);
    RecorderPathWin().clear();
}

bool ScreenCapture::IsRecording() {
    std::lock_guard lock(RecorderMutex());
    if(!RecorderProcess())return false;
    if(WaitForSingleObject(RecorderProcess(),0)==WAIT_OBJECT_0) {
        CloseHandle(RecorderProcess());RecorderProcess()=nullptr;
        if(RecorderStdin()) { CloseHandle(RecorderStdin());RecorderStdin()=nullptr; }
        RecordingFlag().store(false);RecorderPathWin().clear();return false;
    }
    return true;
}

std::string ScreenCapture::ActiveRecordingPath()
{
    std::lock_guard lock(RecorderMutex());
    return RecorderPathWin();
}
