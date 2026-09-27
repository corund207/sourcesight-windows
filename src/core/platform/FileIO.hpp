#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <algorithm>

namespace FileIO {
// Exclusive creation prevents overwriting reports and temporary files.
inline bool WriteNew(const std::filesystem::path& path, const std::string& bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return false;
    DWORD error=ERROR_SUCCESS;
    size_t offset=0;
    while (offset<bytes.size()) {
        DWORD written=0;
        const DWORD count=static_cast<DWORD>(std::min(bytes.size()-offset,size_t(MAXDWORD)));
        if (!WriteFile(file,bytes.data()+offset,count,&written,nullptr) || !written) {
            error=GetLastError();if(!error)error=ERROR_WRITE_FAULT;break;
        }
        offset+=written;
    }
    if (!error && !FlushFileBuffers(file)) error=GetLastError();
    if (!CloseHandle(file) && !error) error=GetLastError();
    if (error) { DeleteFileW(path.c_str());SetLastError(error);return false; }
    return true;
}
inline void Replace(const std::filesystem::path& from, const std::filesystem::path& to,
                    std::error_code& error) {
    error.clear();
    if (!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        error=std::error_code(GetLastError(),std::system_category());
}
}
