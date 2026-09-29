#include "PhysicsText.hpp"
#include <cstring>
#include <stdexcept>
#include <cctype>

namespace {
using Bytes=std::vector<unsigned char>;
using Vec3=MapRaytrace::Vec3;
void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
int hex(char c) {
    if(c>='0' && c<='9')return c-'0';
    if(c>='A' && c<='F')return c-'A'+10;
    if(c>='a' && c<='f')return c-'a'+10;
    return -1;
}
Bytes blob(std::istream& input,std::string line) {
    while(line.find_first_not_of(" \t\r")==std::string::npos)
        check(bool(std::getline(input,line)),"missing physics blob");
    const auto start=line.find("#[");check(start!=std::string::npos,"expected binary physics blob");
    Bytes bytes;int high=-1;size_t pos=start+2;
    for(;;) {
        for(;pos<line.size();++pos) {
            const char c=line[pos];
            if(c==']') { check(high<0,"odd physics hex digit count");return bytes; }
            if(std::isspace(static_cast<unsigned char>(c)))continue;
            const int value=hex(c);check(value>=0,"invalid physics hex byte");
            if(high<0)high=value;
            else {bytes.push_back(static_cast<unsigned char>(high*16+value));high=-1;}
            check(bytes.size()<=128*1024*1024,"physics blob exceeds size limit");
        }
        check(bool(std::getline(input,line)),"truncated physics blob");pos=0;
    }
}
template<typename T> std::vector<T> unpack(const Bytes& data) {
    check(data.size()%sizeof(T)==0,"misaligned physics element data");
    std::vector<T> result(data.size()/sizeof(T));
    if(!data.empty())std::memcpy(result.data(),data.data(),data.size());
    return result;
}
struct Shape {
    bool hull=false;
    Bytes vertices,positions,edges,faces,triangles;
    void append(std::vector<MapRaytrace::Triangle>& output) const {
        const auto points=unpack<Vec3>(hull && !positions.empty()?positions:vertices);
        check(!points.empty(),"physics shape has no vertices");
        for(const auto& p:points)
            check(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
                  std::abs(p.x)<1e7f&&std::abs(p.y)<1e7f&&std::abs(p.z)<1e7f,"invalid physics vertex");
        auto emit=[&](size_t a,size_t b,size_t c) {
            check(a<points.size()&&b<points.size()&&c<points.size(),"physics vertex index out of bounds");
            check(output.size()<512*1024*1024/sizeof(MapRaytrace::Triangle),"physics mesh exceeds size limit");
            output.push_back({points[a],points[b],points[c]});
        };
        if(!hull) {
            const auto indices=unpack<std::uint32_t>(triangles);
            check(!indices.empty() && indices.size()%3==0,"invalid triangle index triples");
            for(size_t i=0;i<indices.size();i+=3)emit(indices[i],indices[i+1],indices[i+2]);
        } else {
            // RnHalfEdge_t: next, twin, origin, face (four uint8s).
            check(!faces.empty() && !edges.empty() && edges.size()%4==0,"invalid hull topology");
            for(size_t face=0;face<faces.size();++face) {
                size_t edge=faces[face];std::vector<size_t> ring;
                do {
                    check(edge<edges.size()/4 && ring.size()<edges.size()/4,"invalid hull edge cycle");
                    check(edges[edge*4+3]==face,"hull edge belongs to another face");
                    ring.push_back(edges[edge*4+2]);edge=edges[edge*4];
                } while(edge!=faces[face]);
                check(ring.size()>=3,"hull face has fewer than three vertices");
                for(size_t i=1;i+1<ring.size();++i)emit(ring[0],ring[i],ring[i+1]);
            }
        }
    }
};
}
std::vector<MapRaytrace::Triangle> PhysicsText::Read(std::istream& input) {
    std::vector<MapRaytrace::Triangle> output;Shape shape;
    std::string line;int depth=0;bool pending=false,active=false;
    while(std::getline(input,line)) {
        const auto first=line.find_first_not_of(" \t\r");
        if(first==std::string::npos)continue;
        std::string_view trimmed(line.data()+first,line.size()-first);
        while(!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))trimmed.remove_suffix(1);
        if(!active && (trimmed.starts_with("m_Hull =") || trimmed.starts_with("m_Mesh ="))) {
            shape=Shape{};shape.hull=trimmed.starts_with("m_Hull =");pending=true;continue;
        }
        if(pending) {check(trimmed=="{","missing physics shape opening brace");pending=false;active=true;depth=1;continue;}
        if(!active)continue;
        if(trimmed=="{") {++depth;continue;}
        if(trimmed.starts_with("}")) {
            if(--depth==0) {shape.append(output);active=false;}
            continue;
        }
        if(depth!=1)continue;
        const auto equals=trimmed.find('=');if(equals==std::string::npos)continue;
        auto key=trimmed.substr(0,equals);
        while(!key.empty() && std::isspace(static_cast<unsigned char>(key.back())))key.remove_suffix(1);
        Bytes* dest=nullptr;
        if(key=="m_Vertices")dest=&shape.vertices;
        else if(key=="m_VertexPositions")dest=&shape.positions;
        else if(key=="m_Edges")dest=&shape.edges;
        else if(key=="m_Faces")dest=&shape.faces;
        else if(key=="m_Triangles")dest=&shape.triangles;
        if(dest) {check(dest->empty(),"duplicate physics field");*dest=blob(input,std::string(trimmed.substr(equals+1)));}
    }
    check(!active && !pending && !input.bad(),"truncated physics shape");
    check(!output.empty(),"no hulls or meshes in physics dump");
    return output;
}
