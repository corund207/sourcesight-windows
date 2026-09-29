#include "common.hpp"
#include "core/engine/classes/PhysicsText.hpp"
#include "core/engine/classes/VpkReader.hpp"
#include "core/engine/classes/MapExtractor.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>
namespace {
void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
template<typename T>std::string blob(std::initializer_list<T> values) {
    std::ostringstream s;s<<"#[ ";
    for(const T& value:values) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(size_t i=0;i<sizeof(T);++i)s<<std::hex<<std::setw(2)<<std::setfill('0')<<int(bytes[i])<<' ';
    }
    return s.str()+"]";
}
std::string mesh(bool invalid=false) {
    return "m_Mesh =\n{\nm_Vertices = "+blob<float>({10,20,30,11,20,30,10,21,30})+
           "\nm_Triangles =\n"+blob<std::uint32_t>({0,1,invalid?9u:2u})+"\n}\n";
}
std::string hull(bool invalid=false) {
    // An independently specified triangular face at z=3. Vertex indices
    // in m_Vertices are topology hints; positions come from m_VertexPositions.
    return "m_Hull =\n{\nm_Vertices = #[ 00 01 02 ]\nm_VertexPositions =\n"+
        blob<float>({1,1,3,2,1,3,1,2,3})+"\nm_Edges = "+
        blob<unsigned char>({1,0,0,0,2,0,1,0,static_cast<unsigned char>(invalid?2:0),0,2,0})+
        "\nm_Faces = #[ 00 ]\n}\n";
}
void rejected(const std::string& text) {
    bool failed=false;try {std::istringstream input(text);PhysicsText::Read(input);}catch(const std::exception&){failed=true;}
    require(failed,"corrupt physics must be rejected");
}
template<typename T>void append(std::string& s,T value) {s.append(reinterpret_cast<const char*>(&value),sizeof(value));}
void makeVpk(const std::filesystem::path& file,bool chunked) {
    std::string tree;
    for(const char* s:{"vmdl_c","maps/test","world_physics"}) {tree+=s;tree+='\0';}
    append<std::uint32_t>(tree,0);append<std::uint16_t>(tree,3);
    append<std::uint16_t>(tree,chunked?2:0x7fff);append<std::uint32_t>(tree,0);
    append<std::uint32_t>(tree,4);append<std::uint16_t>(tree,0xffff);
    tree+="pre";tree.append(3,'\0');
    std::string archive;append<std::uint32_t>(archive,0x55aa1234);append<std::uint32_t>(archive,2);
    append<std::uint32_t>(archive,static_cast<std::uint32_t>(tree.size()));
    append<std::uint32_t>(archive,chunked?0:4);archive.append(12,'\0');archive+=tree;
    if(!chunked)archive+="body";
    std::ofstream out(file,std::ios::binary);out.write(archive.data(),archive.size());
    if(chunked) {std::ofstream chunk(file.parent_path()/"fixture_002.vpk",std::ios::binary);chunk<<"body";}
}
std::string read(const std::filesystem::path& path) {std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
}
int main() {
    LogHelper::Init();
    try {
        std::istringstream input(hull()+mesh());const auto triangles=PhysicsText::Read(input);
        require(triangles.size()==2,"must include BOTH hull and triangle mesh");
        require(triangles[0].p1.x==1 && triangles[0].p3.y==2 && triangles[0].p3.z==3,"hull stays in Source coordinates");
        require(triangles[1].p1.x==10 && triangles[1].p2.x==11 && triangles[1].p3.y==21,"mesh indices map to exact positions");
        rejected(mesh(true));rejected(hull(true));rejected("m_Mesh =\n{\nm_Vertices = #[ 0Z ]\n}");
        rejected("m_Hull =\n{");rejected("");
        const auto folder=std::filesystem::current_path()/"extraction fixtures";
        std::filesystem::create_directories(folder);
        for(bool chunked:{false,true}) {
            const auto vpk=folder/"fixture_dir.vpk";makeVpk(vpk,chunked);
            require(VpkReader::Extract(vpk,"maps/test/world_physics.vmdl_c",folder/"resource"),"extract exact VPK entry");
            require(read(folder/"resource")=="prebody","preload plus correct inline/chunk payload");
            require(!VpkReader::Extract(vpk,"maps/wrong/world_physics.vmdl_c",folder/"missing"),"no basename fallback across maps");
        }
        {std::ofstream bad(folder/"bad.vpk");bad<<"bad";}
        bool failed=false;try {VpkReader::Extract(folder/"bad.vpk","x",folder/"no");}catch(...){failed=true;}
        require(failed,"reject truncated VPK");
        {std::ofstream physics(folder/"test.vphys");physics<<hull()<<mesh();}
        // Remove only this test's own output to exercise fresh publication each run.
        std::filesystem::remove(folder/"test.tri");
        const auto extracted=MapExtractor::ExtractMap("test",folder.string());
        if(!extracted.success)std::cerr<<extracted.error<<'\n';
        require(extracted.success,"local PHYS converts without game or decoder");
        const auto bytes=read(folder/"test.tri");
        require(bytes.size()==2*sizeof(MapRaytrace::Triangle) &&
                std::memcmp(bytes.data(),triangles.data(),bytes.size())==0,"published bytes match independent expected geometry");
        require(!MapExtractor::ExtractMap("../escape",folder.string()).success,"reject traversal map name");
        {std::ofstream bad(folder/"invalid.vphys");bad<<mesh(true);}
        require(!MapExtractor::ExtractMap("invalid",folder.string()).success &&
                !std::filesystem::exists(folder/"invalid.tri"),"failed conversion never publishes cache");
        std::cout<<"PASS: VPK preload/chunks/exact paths, hulls and mesh coordinates, malformed data, atomic publication\n";
        LogHelper::Destroy();return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';LogHelper::Destroy();return 1;}
}
