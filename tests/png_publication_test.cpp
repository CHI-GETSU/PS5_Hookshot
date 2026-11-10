#define _CRT_SECURE_NO_WARNINGS
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
static void need(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
std::mutex mu;
std::map<std::string,std::vector<uint8_t>> files;
struct Stream{std::string path;};
std::map<FILE*,std::unique_ptr<Stream>> live;
std::vector<std::string> messages;
std::string mode;
unsigned writes=0,closes=0,fail_write=0;
const std::vector<uint8_t> previous={'p','r','e','v','i','o','u','s'};
void logf(const char* format,...){char text[2048];va_list args;va_start(args,format);std::vsnprintf(text,sizeof text,format,args);va_end(args);std::lock_guard lk(mu);messages.emplace_back(text);}
FILE* png_open(const char* name,const char* access){
    std::lock_guard lk(mu);
    if(mode=="open-failure"){errno=EACCES;return nullptr;}
    if(std::strchr(access,'x') && files.count(name)){errno=EEXIST;return nullptr;}
    files[name].clear();auto s=std::make_unique<Stream>();s->path=name;
    FILE* f=reinterpret_cast<FILE*>(s.get());live.emplace(f,std::move(s));return f;
}
size_t png_write(const void* data,size_t size,size_t count,FILE* f){
    std::lock_guard lk(mu);auto& out=files.at(live.at(f)->path);writes++;
    const bool fail=(fail_write && writes==fail_write);
    size_t n=size*count;if(fail && n)n--;
    auto p=static_cast<const uint8_t*>(data);out.insert(out.end(),p,p+n);
    if(fail){errno=ENOSPC;return size?n/size:0;}return count;
}
int png_close(FILE* f){std::lock_guard lk(mu);need(live.erase(f)==1,"exactly one close per owned stream");closes++;if(mode=="close-failure"){errno=ENOSPC;return EOF;}return 0;}
int png_remove(const char* path){std::lock_guard lk(mu);if(mode=="cleanup-failure"){errno=EACCES;return -1;}if(!files.erase(path)){errno=ENOENT;return -1;}return 0;}
int png_rename(const char* from,const char* to){
    std::lock_guard lk(mu);if(mode=="publish-failure"){errno=EACCES;return -1;}
    files[to]=std::move(files.at(from));files.erase(from);return 0;
}
[[maybe_unused]] int png_move(const char* from,const char* to,unsigned){return png_rename(from,to)==0;}
[[maybe_unused]] unsigned png_last_error(){return 5;}
#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 1
#endif
#define fopen png_open
#define fwrite png_write
#define fclose png_close
#define remove png_remove
#define rename png_rename
#define MoveFileExA png_move
#define GetLastError png_last_error
/* PNG */
#undef fopen
#undef fwrite
#undef fclose
#undef remove
#undef rename
#undef MoveFileExA
#undef GetLastError
const uint32_t kShotW=257,kShotH=251;
/* ANNOUNCE */
static std::vector<uint8_t> pixels(unsigned seed){
    std::vector<uint8_t> out(size_t(kShotW)*kShotH*4);
    for(size_t i=0;i<out.size();i+=4){out[i]=uint8_t(seed);out[i+1]=73;out[i+2]=211;out[i+3]=127;}
    return out;
}
static bool saved(const std::string& path,unsigned seed){auto rgba=pixels(seed);return write_png(path.c_str(),rgba.data(),kShotW,kShotH);}
static void emit(const char* directory){
    unsigned n=0;
    for(auto& [name,data]:files)if(name.ends_with(".png") && data!=previous){
        const std::string path=std::string(directory)+"/"+std::to_string(n++)+".png";
        FILE* f=std::fopen(path.c_str(),"wb");need(f!=nullptr,"fixture output open");need(std::fwrite(data.data(),1,data.size(),f)==data.size(),"fixture output write");need(std::fclose(f)==0,"fixture output close");
    }
}
int main(int argc,char**argv){
    need(argc==3,"mode/output directory");mode=argv[1];const std::string target="capture.png";files[target]=previous;
    if(mode.starts_with("write-"))fail_write=unsigned(std::atoi(mode.c_str()+6));
    if(mode=="cleanup-failure" || mode=="announce-failure")fail_write=1;
    if(mode=="bounded-report"){
        mode="open-failure";for(int i=0;i<30;i++)need(!saved(target,17),"every failed capture returns false");
        need(!messages.empty() && messages.size()<=8,"failure diagnostics remain bounded");
        need(files[target]==previous && live.empty(),"repeated failure preserves prior capture and handles");
        mode="healthy";need(saved(target,19),"capture still recovers after report limit");emit(argv[2]);return 0;
    }
    if(mode=="invalid"){
        uint8_t one[4]{};
        need(!write_png(target.c_str(),one,0,1),"zero width rejected");
        need(!write_png(target.c_str(),one,UINT32_MAX,1),"PNG width limit rejected");
        need(!write_png(target.c_str(),one,1000000,1000000),"oversized IDAT rejected before source access");
        need(!write_png(nullptr,one,1,1) && !write_png("",one,1,1) && !write_png(target.c_str(),nullptr,1,1),"missing input/path rejected");
        need(files.size()==1 && files[target]==previous && !writes && !closes,"invalid input leaves destination untouched");return 0;
    }
    if(mode=="parallel" || mode=="same-target"){
        std::atomic<unsigned> ready{0};std::atomic<bool> go{false};std::vector<std::thread> workers;
        for(unsigned i=1;i<=12;i++)workers.emplace_back([&,i]{ready++;while(!go.load())std::this_thread::yield();need(saved(mode=="same-target"?target:std::to_string(i)+".png",i),"concurrent capture succeeds");});
        while(ready.load()!=12)std::this_thread::yield();
        go=true;
        for(auto& t:workers)t.join();
        need(live.empty(),"parallel files all closed");emit(argv[2]);return 0;
    }
    if(mode=="collisions" || mode=="collision-limit"){
        unsigned count=mode=="collisions"?3:128;
        for(unsigned i=0;i<count;i++)files[target+".part."+std::to_string(i)]=previous;
        bool ok=saved(target,17);need(ok==(mode=="collisions"),"bounded exclusive creation");
        for(unsigned i=0;i<count;i++)need(files[target+".part."+std::to_string(i)]==previous,"foreign collision preserved");
        if(!ok){need(files[target]==previous,"collision failure preserves target");mode="healthy";need(saved(target,19),"next capture retries after collisions");}
        emit(argv[2]);return 0;
    }
    if(mode=="announce-failure" || mode=="announce-success"){
        auto px=std::make_shared<std::vector<uint8_t>>(pixels(17));announce(px,target,1);
        bool success=mode=="announce-success";
        unsigned said=0;for(auto& m:messages)said+=m.find("written to")!=std::string::npos;
        need(said==(success?1u:0u),"success announcement matches actual publication");
        need(success || files[target]==previous,"failed announced capture preserves target");emit(argv[2]);return 0;
    }
    const bool healthy=mode=="healthy";
    bool ok=saved(target,17);need(ok==healthy,"publication status reflects write/close/replace result");
    need(live.empty(),"all file handles closed on success and failure");
    if(!healthy){
        need(files[target]==previous,"failed capture preserves previous complete file");
        need(!messages.empty() && messages.back().find("capture.png")!=std::string::npos,"failure identifies capture path");
        need(files.size()==(mode=="cleanup-failure"?2u:1u),"only owned incomplete temporary may remain on cleanup failure");
        if(mode=="cleanup-failure")return 0;
        mode="healthy";fail_write=0;need(saved(target,19),"subsequent capture recovers without disabling output");
    }
    emit(argv[2]);return 0;
}
