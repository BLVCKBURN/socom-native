// SOCOM 1 GameZ rigid models.zar -> glTF 2.0 exporter
// C++17, no third-party dependencies.
//
// Designed for weapon/prop rigid VIF models recovered from SOCOM 1.
// Child names such as N000_000 / N000_001 are grouped by the N000 prefix;
// N000 is generally the highest-detail representation, N001 a lower LOD.
//
// Build macOS:
//   clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic socom_rigid_gltf.cpp -o socom_rigid_gltf
// Windows:
//   cl /std:c++17 /EHsc /O2 socom_rigid_gltf.cpp
//
// Usage:
//   socom_rigid_gltf <models.zar> <decoded_png_dir> <model> <out_dir> [group]
// Example:
//   ./socom_rigid_gltf weapons_models.zar weapon_png m4Acarbine out N000

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;

#pragma pack(push,1)
struct ZarHeader{uint32_t u00,nodeCount,stringSize,base,alignment;uint8_t r[0x40];uint32_t payloadSize,u58,u5c,version;};
struct ZarNodeDisk{uint32_t nameAddress,dataOffset,dataSize,childCount;};
#pragma pack(pop)
static_assert(sizeof(ZarHeader)==0x64);static_assert(sizeof(ZarNodeDisk)==0x10);
template<class T>T rd(const uint8_t*p){T v{};std::memcpy(&v,p,sizeof(v));return v;}
size_t au(size_t v,size_t a){if(!a)throw std::runtime_error("zero alignment");auto r=v%a;return r?v+a-r:v;}
struct Node{std::string name;uint32_t off{},size{};std::vector<Node> children;};
class Zar{public:explicit Zar(const fs::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("open failed: "+p.string());f.seekg(0,std::ios::end);auto n=f.tellg();f.seekg(0);bytes.resize((size_t)n);f.read((char*)bytes.data(),(std::streamsize)bytes.size());if(bytes.size()<0x64)throw std::runtime_error("small ZAR");std::memcpy(&h,bytes.data(),sizeof(h));if(h.version!=0x20002)throw std::runtime_error("bad ZAR version");so=0x64;no=so+h.stringSize;size_t nb=(size_t)h.nodeCount*16;po=au(no+nb,h.alignment);if(po+h.payloadSize!=bytes.size())throw std::runtime_error("bad ZAR layout");disk.resize(h.nodeCount);std::memcpy(disk.data(),bytes.data()+no,nb);size_t c=0;root=parse(c);if(c!=disk.size())throw std::runtime_error("tree mismatch");}
const uint8_t*payload(uint32_t o,size_t n=1)const{if((uint64_t)o+n>h.payloadSize)throw std::runtime_error("payload OOB");return bytes.data()+po+o;}Node root;
private:std::string name(uint32_t a)const{if(!a)return"<root>";if(a<h.base)throw std::runtime_error("bad name ptr");size_t rel=a-h.base,s=so+rel,e=s,lim=so+h.stringSize;while(e<lim&&bytes[e])++e;if(e==lim)throw std::runtime_error("unterminated name");return std::string((char*)bytes.data()+s,e-s);}Node parse(size_t&c){auto d=disk.at(c++);Node n;n.name=name(d.nameAddress);n.off=d.dataOffset;n.size=d.dataSize;for(uint32_t i=0;i<d.childCount;i++)n.children.push_back(parse(c));return n;}std::vector<uint8_t>bytes;ZarHeader h{};std::vector<ZarNodeDisk>disk;size_t so{},no{},po{};};
const Node* top(const Node&n,const std::string&s){for(auto&c:n.children)if(c.name==s)return&c;return nullptr;}
std::string cstr(const uint8_t*b,size_t n,uint32_t o){if(o>=n)throw std::runtime_error("string OOB");size_t e=o;while(e<n&&b[e])++e;if(e==n)throw std::runtime_error("unterminated string");return std::string((char*)b+o,e-o);}
std::string pngname(std::string s){auto p=s.find_last_of('.');if(p!=std::string::npos)s.resize(p);return s+".png";}
std::string esc(const std::string&s){std::ostringstream o;for(unsigned char c:s){if(c=='\\')o<<"\\\\";else if(c=='\"')o<<"\\\"";else o<<char(c);}return o.str();}

struct V{std::array<float,3>p,n;std::array<float,2>uv;std::array<uint8_t,4>rgba;};
struct T{uint16_t a,b,c;};
struct Packet{std::string tex;std::vector<V>v;std::vector<T>t;};
struct State{std::string tex;uint32_t colorOff{};uint32_t colorCount{};};

Packet parsePacket(const uint8_t*blob,size_t blobSize,uint32_t off,const State&st){if(off+0x50>blobSize)throw std::runtime_error("rigid packet short");auto cmd0=rd<uint32_t>(blob+off),cmd1=rd<uint32_t>(blob+off+4);if(((cmd0>>24)&0x7f)!=1||((cmd1>>24)&0x7f)!=0x6c||((cmd1>>16)&0xff)!=4)throw std::runtime_error("not rigid packet");uint32_t ctrl[16];for(int i=0;i<16;i++)ctrl[i]=rd<uint32_t>(blob+off+8+i*4);uint32_t vc=ctrl[10],tc=ctrl[11],tb=ctrl[8];float scale=rd<float>(blob+off+0x44);if(tb!=vc*3+4)throw std::runtime_error("rigid triangle base invariant");size_t p=off+0x48;auto stv=rd<uint32_t>(blob+p);auto unv=rd<uint32_t>(blob+p+4);p+=8;if(((stv>>24)&0x7f)!=1||(stv&0xffff)!=0x0203||((unv>>24)&0x7f)!=0x6d||((unv>>16)&0xff)!=vc*2)throw std::runtime_error("vertex VIF mismatch");if(p+(size_t)vc*16>blobSize)throw std::runtime_error("vertex data OOB");Packet out;out.tex=st.tex;out.v.resize(vc);constexpr float ns=1.0f/32767.0f,us=1.0f/4096.0f;for(uint32_t i=0;i<vc;i++){auto*q=blob+p+i*16;int16_t px=rd<int16_t>(q),py=rd<int16_t>(q+2),pz=rd<int16_t>(q+4),nx=rd<int16_t>(q+6),tu=rd<int16_t>(q+8),tv=rd<int16_t>(q+10),ny=rd<int16_t>(q+12),nz=rd<int16_t>(q+14);auto&v=out.v[i];v.p={px*scale*ns*0.1f,py*scale*ns*0.1f,pz*scale*ns*0.1f};v.n={nx*ns,ny*ns,nz*ns};v.uv={tu*us,tv*us};v.rgba={128,128,128,128};}p+=(size_t)vc*16;auto sti=rd<uint32_t>(blob+p),uni=rd<uint32_t>(blob+p+4);p+=8;if(((sti>>24)&0x7f)!=1||(sti&0xffff)!=0x0102||((uni>>24)&0x7f)!=0x6e||((uni>>16)&0xff)!=tc||(uni&0x3ff)!=tb)throw std::runtime_error("index VIF mismatch");out.t.resize(tc);for(uint32_t i=0;i<tc;i++){const uint8_t*q=blob+p+i*4;if((q[0]%3)||(q[1]%3)||(q[2]%3)||q[3]!=3)throw std::runtime_error("bad rigid triangle");out.t[i]={(uint16_t)(q[0]/3),(uint16_t)(q[1]/3),(uint16_t)(q[2]/3)};}p+=(size_t)tc*4;auto stn=rd<uint32_t>(blob+p),unn=rd<uint32_t>(blob+p+4);p+=8;if(((stn>>24)&0x7f)!=1||(stn&0xffff)!=0x0102||((unn>>24)&0x7f)!=0x69||((unn>>16)&0xff)!=tc)throw std::runtime_error("face-normal VIF mismatch");p+=au((size_t)tc*6,4);if(((rd<uint32_t>(blob+p)>>24)&0x7f)!=0x17)throw std::runtime_error("missing MSCNT");if(st.colorCount==vc&&(uint64_t)st.colorOff+(uint64_t)vc*4<=blobSize){for(uint32_t i=0;i<vc;i++){auto*q=blob+st.colorOff+i*4;out.v[i].rgba={q[0],q[1],q[2],q[3]};}}return out;}

std::vector<Packet> parseGroup(const Zar&z,const Node&m,const std::string&group){const uint8_t*blob=z.payload(m.off,m.size);std::vector<Packet>out;for(auto&ch:m.children){std::string prefix=ch.name;auto u=prefix.find('_');if(u!=std::string::npos)prefix.resize(u);if(prefix!=group)continue;if(ch.size!=4)throw std::runtime_error("child render pointer not 4 bytes");uint32_t lo=rd<uint32_t>(z.payload(ch.off,4));if(lo+16>m.size)throw std::runtime_error("render list OOB");uint32_t n=rd<uint32_t>(blob+lo);if((uint64_t)lo+16ull+16ull*n>m.size)throw std::runtime_error("render records OOB");State st;for(uint32_t i=0;i<n;i++){auto*r=blob+lo+16+i*16;uint32_t w0=rd<uint32_t>(r),w1=rd<uint32_t>(r+4),w3=rd<uint32_t>(r+12),typ=w0&0xffff0000;if(w0==0x10060000){st.tex=cstr(blob,m.size,w1);}else if(typ==0x30040000){st.colorOff=w1;st.colorCount=(w3>>16)&0xff;}else if(typ==0x30020000){out.push_back(parsePacket(blob,m.size,w1,st));}}}if(out.empty())throw std::runtime_error("no packets for group "+group);return out;}

struct BV{size_t off,len;int target;};struct AC{int view,ctype;size_t count;std::string type;std::vector<double>mn,mx;};
struct BB{std::vector<uint8_t>d;std::vector<BV>v;std::vector<AC>a;template<class T>int add(const std::vector<T>&x,int ct,size_t count,const std::string&type,int target,std::vector<double>mn={},std::vector<double>mx={}){while(d.size()%4)d.push_back(0);size_t o=d.size(),n=x.size()*sizeof(T);auto*p=(const uint8_t*)x.data();d.insert(d.end(),p,p+n);int vi=(int)v.size();v.push_back({o,n,target});int ai=(int)a.size();a.push_back({vi,ct,count,type,std::move(mn),std::move(mx)});return ai;}};
struct PJ{int p,n,uv,col,idx,mat;};

void exportModel(const fs::path&zarPath,const fs::path&pngDir,const std::string&modelName,const fs::path&outRoot,std::optional<std::string>groupOpt){Zar z(zarPath);const Node*m=top(z.root,modelName);if(!m)throw std::runtime_error("model not found");std::set<std::string>groups;for(auto&c:m->children){auto s=c.name;auto p=s.find('_');if(p!=std::string::npos)s.resize(p);groups.insert(s);}if(groups.empty())throw std::runtime_error("model has no child groups");std::string group=groupOpt?*groupOpt:*groups.begin();if(!groups.count(group))throw std::runtime_error("group not found: "+group);auto packets=parseGroup(z,*m,group);fs::path out=outRoot/modelName;fs::create_directories(out/"textures");std::map<std::string,int>mat;std::vector<std::string>textures;for(auto&p:packets)if(!mat.count(p.tex)){std::string png=pngname(p.tex);fs::path src=pngDir/png;if(fs::exists(src)) fs::copy_file(src,out/"textures"/png,fs::copy_options::overwrite_existing); else std::cerr<<"warning: missing decoded texture "<<src<<"; using placeholder in glTF\n";mat[p.tex]=(int)textures.size();textures.push_back(p.tex);}BB bb;std::vector<PJ>prims;size_t totalv=0,totalt=0;for(auto&pk:packets){totalv+=pk.v.size();totalt+=pk.t.size();std::vector<float>P,N,U;std::vector<uint8_t>C;std::vector<uint16_t>I;std::array<double,3>mn{1e9,1e9,1e9},mx{-1e9,-1e9,-1e9};for(auto&x:pk.v){for(int k=0;k<3;k++){P.push_back(x.p[k]);N.push_back(x.n[k]);mn[k]=std::min(mn[k],(double)x.p[k]);mx[k]=std::max(mx[k],(double)x.p[k]);}U.push_back(x.uv[0]);U.push_back(x.uv[1]);for(int k=0;k<4;k++)C.push_back(x.rgba[k]);}for(auto&t:pk.t){I.push_back(t.a);I.push_back(t.b);I.push_back(t.c);}PJ q;q.p=bb.add(P,5126,pk.v.size(),"VEC3",34962,{mn[0],mn[1],mn[2]},{mx[0],mx[1],mx[2]});q.n=bb.add(N,5126,pk.v.size(),"VEC3",34962);q.uv=bb.add(U,5126,pk.v.size(),"VEC2",34962);q.col=bb.add(C,5121,pk.v.size(),"VEC4",34962);q.idx=bb.add(I,5123,I.size(),"SCALAR",34963);q.mat=mat.at(pk.tex);prims.push_back(q);}std::ofstream bf(out/(modelName+".bin"),std::ios::binary);bf.write((char*)bb.d.data(),(std::streamsize)bb.d.size());std::ofstream o(out/(modelName+".gltf"));o<<std::setprecision(9);o<<"{\n\"asset\":{\"version\":\"2.0\",\"generator\":\"SOCOM rigid VIF exporter\"},\n\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\n\"nodes\":[{\"name\":\""<<esc(modelName)<<"\",\"mesh\":0}],\n\"meshes\":[{\"name\":\""<<esc(modelName+"_"+group)<<"\",\"primitives\":[";for(size_t i=0;i<prims.size();i++){if(i)o<<",";auto&p=prims[i];o<<"{\"attributes\":{\"POSITION\":"<<p.p<<",\"NORMAL\":"<<p.n<<",\"TEXCOORD_0\":"<<p.uv<<",\"COLOR_0\":"<<p.col<<"},\"indices\":"<<p.idx<<",\"material\":"<<p.mat<<",\"mode\":4}";}o<<"]}],\n\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}],\n\"images\":[";for(size_t i=0;i<textures.size();i++){if(i)o<<",";auto png=pngname(textures[i]);if(fs::exists(pngDir/png))o<<"{\"uri\":\"textures/"<<esc(png)<<"\"}";else o<<"{\"name\":\"MISSING: "<<esc(textures[i])<<"\",\"uri\":\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Z2GAAAAAASUVORK5CYII=\"}";}o<<"],\n\"textures\":[";for(size_t i=0;i<textures.size();i++){if(i)o<<",";o<<"{\"sampler\":0,\"source\":"<<i<<"}";}o<<"],\n\"materials\":[";for(size_t i=0;i<textures.size();i++){if(i)o<<",";o<<"{\"name\":\""<<esc(textures[i])<<"\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":"<<i<<"},\"metallicFactor\":0,\"roughnessFactor\":1}}";}o<<"],\n\"buffers\":[{\"byteLength\":"<<bb.d.size()<<",\"uri\":\""<<esc(modelName+".bin")<<"\"}],\n\"bufferViews\":[";for(size_t i=0;i<bb.v.size();i++){if(i)o<<",";auto&v=bb.v[i];o<<"{\"buffer\":0,\"byteOffset\":"<<v.off<<",\"byteLength\":"<<v.len<<",\"target\":"<<v.target<<"}";}o<<"],\n\"accessors\":[";for(size_t i=0;i<bb.a.size();i++){if(i)o<<",";auto&a=bb.a[i];o<<"{\"bufferView\":"<<a.view<<",\"byteOffset\":0,\"componentType\":"<<a.ctype<<",\"count\":"<<a.count<<",\"type\":\""<<a.type<<"\"";if(a.ctype==5121&&a.type=="VEC4")o<<",\"normalized\":true";if(!a.mn.empty()){o<<",\"min\":[";for(size_t k=0;k<a.mn.size();k++){if(k)o<<",";o<<a.mn[k];}o<<"]";}if(!a.mx.empty()){o<<",\"max\":[";for(size_t k=0;k<a.mx.size();k++){if(k)o<<",";o<<a.mx[k];}o<<"]";}o<<"}";}o<<"]\n}\n";std::cout<<"Exported "<<modelName<<" group "<<group<<" -> "<<(out/(modelName+".gltf"))<<"\n  packets: "<<packets.size()<<" vertices: "<<totalv<<" triangles: "<<totalt<<" materials: "<<textures.size()<<"\n";}

int main(int argc,char**argv){try{if(argc!=5&&argc!=6){std::cerr<<"Usage: socom_rigid_gltf <models.zar> <png_dir> <model> <out_dir> [group]\n";return 1;}std::optional<std::string>g;if(argc==6)g=argv[5];exportModel(argv[1],argv[2],argv[3],argv[4],g);return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<"\n";return 2;}}
