#include "Memory.hpp"
#include <tlhelp32.h>
namespace {
struct WindowSearch { DWORD pid; HWND result{}; };
BOOL CALLBACK FindMainWindow(HWND window,LPARAM parameter) {
    auto& search=*reinterpret_cast<WindowSearch*>(parameter);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==search.pid && IsWindowVisible(window) && !GetWindow(window,GW_OWNER)) {
        search.result=window;return FALSE;
    }
    return TRUE;
}
}
bool pProcess::AttachProcess(const char* name) {
    Close();
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32 entry{};entry.dwSize=sizeof(entry);
    if(Process32First(snapshot,&entry)) do {
        if(_stricmp(entry.szExeFile,name)==0) { pid_=entry.th32ProcessID;break; }
    } while(Process32Next(snapshot,&entry));
    CloseHandle(snapshot);
    if(!pid_)return false;
    handle_=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid_);
    if(!handle_)return false;
    UpdateHWND();return true;
}
bool pProcess::UpdateHWND() {
    if(!handle_ || WaitForSingleObject(handle_,0)==WAIT_OBJECT_0)return false;
    WindowSearch search{pid_};EnumWindows(FindMainWindow,reinterpret_cast<LPARAM>(&search));
    hwnd_=search.result;return hwnd_!=nullptr;
}
ProcessModule pProcess::GetModule(const char* name) {
    if(!pid_ || !handle_)return {};
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid_);
    if(snapshot==INVALID_HANDLE_VALUE)return {};
    MODULEENTRY32 entry{};entry.dwSize=sizeof(entry);ProcessModule result{};
    if(Module32First(snapshot,&entry)) do {
        if(_stricmp(entry.szModule,name)==0) {
            result={reinterpret_cast<uintptr_t>(entry.modBaseAddr),entry.modBaseSize};break;
        }
    } while(Module32Next(snapshot,&entry));
    CloseHandle(snapshot);return result;
}
void pProcess::Close() {
    if(handle_)CloseHandle(handle_);
    handle_=nullptr;pid_=0;hwnd_=nullptr;base_client_={};
}
