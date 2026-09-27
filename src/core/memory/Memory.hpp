#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

struct ProcessModule { uintptr_t base{}, size{}; };
// User-mode, read-only process access. No writes, allocations or injection.
class pProcess {
public:
    DWORD pid_{};
    HANDLE handle_{};
    HWND hwnd_{};
    ProcessModule base_client_{};
    pProcess()=default;
    ~pProcess() { Close(); }
    pProcess(const pProcess&)=delete;
    pProcess& operator=(const pProcess&)=delete;
    bool AttachProcess(const char* name);
    bool UpdateHWND();
    void Close();
    ProcessModule GetModule(const char* name);
    bool read_raw(uintptr_t address,void* buffer,size_t size) const {
        SIZE_T count=0;
        return handle_ && buffer && address &&
            ReadProcessMemory(handle_,reinterpret_cast<const void*>(address),buffer,size,&count) && count==size;
    }
    template<class T> T read(uintptr_t address) const {
        T result{};
        if(!read_raw(address,&result,sizeof(result)))return T{};
        return result;
    }
    uintptr_t read_multi_address(uintptr_t pointer,const std::vector<uintptr_t>& offsets) const {
        for(auto offset:offsets) { pointer=read<uintptr_t>(pointer+offset);if(!pointer)break; }
        return pointer;
    }
    template<class T> T read_multi(uintptr_t pointer,const std::vector<uintptr_t>& offsets) const {
        if(offsets.empty())return T{};
        for(size_t i=0;i+1<offsets.size();++i) { pointer=read<uintptr_t>(pointer+offsets[i]);if(!pointer)return T{}; }
        return read<T>(pointer+offsets.back());
    }
};
