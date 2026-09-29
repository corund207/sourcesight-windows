#include "VpkReader.hpp"
#include <fstream>
#include <vector>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace {
void check(bool ok) {if(!ok)throw std::runtime_error("Invalid or truncated VPK");}
}
bool VpkReader::Extract(const std::filesystem::path& archive,const std::string& entry,
                        const std::filesystem::path& output) {
    std::ifstream in(archive,std::ios::binary);if(!in)return false;
    std::uint32_t header[7]{};
    check(bool(in.read(reinterpret_cast<char*>(header),12)));
    check(header[0]==0x55aa1234 && (header[1]==1 || header[1]==2));
    const size_t header_bytes=header[1]==1?12:28;
    if(header[1]==2)check(bool(in.read(reinterpret_cast<char*>(header+3),16)));
    check(header[2]>0 && header[2]<=64*1024*1024);
    std::vector<unsigned char> tree(header[2]);
    check(bool(in.read(reinterpret_cast<char*>(tree.data()),tree.size())));
    size_t pos=0;
    auto str=[&]() {
        const size_t start=pos;while(pos<tree.size() && tree[pos])++pos;
        check(pos<tree.size());return std::string(reinterpret_cast<char*>(tree.data()+start),pos++-start);
    };
    auto u16=[&]() {check(pos+2<=tree.size());std::uint16_t x;std::memcpy(&x,tree.data()+pos,2);pos+=2;return x;};
    auto u32=[&]() {check(pos+4<=tree.size());std::uint32_t x;std::memcpy(&x,tree.data()+pos,4);pos+=4;return x;};
    for(std::string ext=str();!ext.empty();ext=str())
        for(std::string path=str();!path.empty();path=str())
            for(std::string name=str();!name.empty();name=str()) {
                u32();const auto preload=u16(),index=u16();const auto offset=u32(),length=u32();
                check(u16()==0xffff && pos+preload<=tree.size());
                const auto full=(path==" "?"":path+"/")+name+(ext==" "?"":"."+ext);
                if(full==entry) {
                    check(static_cast<uint64_t>(preload)+length<=256*1024*1024);
                    std::vector<char> bytes(preload+length);
                    std::memcpy(bytes.data(),tree.data()+pos,preload);
                    if(length) {
                        if(index==0x7fff) {
                            if(header[1]==2)check(static_cast<uint64_t>(offset)+length<=header[3]);
                            in.seekg(static_cast<uint64_t>(header_bytes)+tree.size()+offset);
                        }
                        else {
                            auto stem=archive.stem().string();
                            if(stem.ends_with("_dir"))stem.resize(stem.size()-4);
                            std::ostringstream chunk;chunk<<stem<<'_'<<std::setw(3)<<std::setfill('0')<<index<<".vpk";
                            in.close();in.open(archive.parent_path()/chunk.str(),std::ios::binary);in.seekg(offset);
                        }
                        check(bool(in.read(bytes.data()+preload,length)));
                    }
                    std::ofstream out(output,std::ios::binary|std::ios::trunc);
                    out.write(bytes.data(),bytes.size());out.close();check(bool(out));return true;
                }
                pos+=preload;
            }
    return false;
}
