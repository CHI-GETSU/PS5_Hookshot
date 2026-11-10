#define VK_NO_PROTOTYPES
#define _CRT_SECURE_NO_WARNINGS
#include <vulkan/vulkan.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace mkwvk {
struct Allocation {
    std::vector<uint8_t> gpu, cpu;
    bool copied=false, host=false, completed=false, destroyed=false;
};
using VmaAllocation=Allocation*;
struct Texture {
    VkImage image{}; VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT;
    VkFormat format=VK_FORMAT_R32G32B32A32_SFLOAT;
    uint64_t gpu=0; int id=1;
    uint32_t levels=1,width=2,height=1,layers=1,depth=1;
    VkImageType type=VK_IMAGE_TYPE_2D;
};
struct Device {VkPhysicalDevice physical{};} dev;
std::vector<std::unique_ptr<Allocation>> allocations;
std::vector<uint8_t> payload, png;
std::vector<std::string> lines;
bool coherent=false, implicit_host=false;
int invalidations=0;
int waits=0;
std::atomic<bool> par_active{false};
std::atomic<uint64_t> work_epoch{0};
VkCommandBuffer last_barrier_cmd{};
uint64_t last_barrier_epoch=UINT64_MAX;
std::atomic<int64_t> st_barriers_left_out{0}, st_barriers_made{0};
struct GprofSpan { GprofSpan(VkCommandBuffer,const char*){} };
void logf(const char* format,...) {
    char b[2048];va_list a;va_start(a,format);vsnprintf(b,sizeof b,format,a);va_end(a);lines.emplace_back(b);
}
bool create_buffer(uint64_t n,VkBufferUsageFlags,bool,VkBuffer& b,VmaAllocation& a,void** map) {
    auto x=std::make_unique<Allocation>();x->gpu.resize(n);x->cpu.resize(n);
    a=x.get();b=reinterpret_cast<VkBuffer>(a);*map=x->cpu.data();allocations.push_back(std::move(x));return true;
}
void destroy_buffer(VkBuffer,VmaAllocation a){a->destroyed=true;}
bool create_image(const VkImageCreateInfo&,VkImage& i,VmaAllocation& a){i=reinterpret_cast<VkImage>(2);a=nullptr;return true;}
void destroy_image(VkImage,VmaAllocation){}
void invalidate_mapped(VmaAllocation a,uint64_t n) {
    if(!a->completed || n!=a->gpu.size()) {std::fprintf(stderr,"bad invalidate order/range\n");std::exit(9);}
    invalidations++;
    if(a->host) a->cpu=a->gpu;
}
void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice,VkFormat,VkFormatProperties* p){p->optimalTilingFeatures=VK_FORMAT_FEATURE_BLIT_SRC_BIT;}
void vkCmdPipelineBarrier2(VkCommandBuffer,const VkDependencyInfo* d) {
    for(uint32_t i=0;i<d->memoryBarrierCount;i++) {
        const auto& b=d->pMemoryBarriers[i];
        bool transfer=(b.srcStageMask&(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT|VK_PIPELINE_STAGE_2_COPY_BIT))!=0;
        bool writes=(b.srcAccessMask&(VK_ACCESS_2_MEMORY_WRITE_BIT|VK_ACCESS_2_TRANSFER_WRITE_BIT))!=0;
        if(transfer && writes && (b.dstStageMask&VK_PIPELINE_STAGE_2_HOST_BIT) && (b.dstAccessMask&(VK_ACCESS_2_HOST_READ_BIT|VK_ACCESS_2_MEMORY_READ_BIT)))
            for(auto& a:allocations) if(a->copied&&!a->destroyed)a->host=true;
    }
    work_epoch++;
}
void copy_to(VkBuffer b,size_t n) {
    auto a=reinterpret_cast<Allocation*>(b);
    if(n!=a->gpu.size()||payload.size()!=n){std::fprintf(stderr,"bad copy range\n");std::exit(8);}
    a->gpu=payload;a->copied=true;a->host=implicit_host;work_epoch++;
}
void vkCmdCopyImageToBuffer(VkCommandBuffer,VkImage,VkImageLayout,VkBuffer b,uint32_t,const VkBufferImageCopy*){copy_to(b,payload.size());}
void vkCmdCopyBuffer(VkCommandBuffer,VkBuffer,VkBuffer b,uint32_t,const VkBufferCopy* c){copy_to(b,c->size);}
void vkCmdBlitImage(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageBlit*,VkFilter){work_epoch++;}
void wait_value(uint64_t) {
    waits++;
    for(auto& a:allocations) if(a->copied&&!a->destroyed){a->completed=true;if(coherent&&a->host)a->cpu=a->gpu;}
}
void write_png(const char*,const uint8_t* p,uint32_t w,uint32_t h){png.assign(p,p+(size_t)w*h*4);}
int _mkdir(const char*){return 0;}
/* FULL_BARRIER */
} // namespace mkwvk

/* SOURCE */

namespace mkwvk {
void consume_dump(bool burst_open) {
    const uint64_t value=1;
    /* CALLER */
}
}

void require(bool ok,const char* what) {
    if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}
}
template<class T> void upload(const std::vector<T>& v) {
    mkwvk::payload.resize(v.size()*sizeof(T));
    std::memcpy(mkwvk::payload.data(),v.data(),mkwvk::payload.size());
}

int main(int argc,char** argv) {
    if(argc!=4)return 2;
    using namespace mkwvk;
    const std::string which=argv[1];
    dir=".";
    coherent=atoi(argv[2])!=0;implicit_host=atoi(argv[3])!=0;
    Texture t;t.image=reinterpret_cast<VkImage>(1);
    const auto cmd=reinterpret_cast<VkCommandBuffer>(1);
    std::vector<uint8_t> expected{64,128,191,255,128,64,255,255};
    std::vector<float> floats{.25f,.5f,.75f,1.f,.5f,.25f,1.f,1.f};
    if(which=="empty") {
        consume_dump(false);require(waits==0 && invalidations==0 && png.empty(),"empty queue reads/waits");return 0;
    }
    armed=true;
    if(which=="ssbo" || which=="ssbo-tail") {
        std::vector<uint32_t> words(which=="ssbo"?8:16384);
        words[1]=1;words[3]=2;words.back()=3;upload(words);
        dump_buffer(cmd,reinterpret_cast<VkBuffer>(5),12,which=="ssbo"?35:65539,"ssbo");
        expected.clear();
    } else {
        if(which=="uint") {
            t.format=VK_FORMAT_R32G32B32A32_UINT;
            upload(std::vector<uint32_t>{0,5,4000000000u,1,100,10,UINT32_MAX,1});
            expected={0,0,0,255,255,255,255,255};
        } else if(which=="sint") {
            t.format=VK_FORMAT_R32G32B32A32_SINT;
            upload(std::vector<int32_t>{INT32_MIN,-5,-2,1,INT32_MAX,5,2,1});
            expected={0,0,0,255,255,255,255,255};
        } else if(which=="nan-inf") {
            t.width=3;
            floats={.25f,.5f,.75f,1.f,std::numeric_limits<float>::quiet_NaN(),0,0,1,
                    std::numeric_limits<float>::infinity(),0,0,1};
            upload(floats);expected={64,128,191,255,255,0,255,255,0,255,0,255};
        } else if(which=="d16" || which=="d24" || which=="d32" || which=="depth-scaled") {
            t.aspect=VK_IMAGE_ASPECT_DEPTH_BIT;
            t.width=which=="depth-scaled"?64:4;
            t.format=which=="d16"?VK_FORMAT_D16_UNORM:which=="d24"?VK_FORMAT_D24_UNORM_S8_UINT:VK_FORMAT_D32_SFLOAT;
            if(which=="d16")upload(std::vector<uint16_t>{0,16384,32768,65535});
            else if(which=="d24")upload(std::vector<uint32_t>{0xaa000000,0xbb400000,0xcc800000,0xddffffff});
            else {
                floats.resize(t.width);
                for(uint32_t i=0;i<t.width;i++)floats[i]=(float)i/(t.width-1);
                upload(floats);
            }
            expected.clear();
            const uint32_t w=std::min(16u,t.width);
            for(uint32_t i=0;i<w;i++) {
                // Golden nearest sample positions, independent of the source expression.
                const float v=which=="depth-scaled"?(float)(i*4)/63.f:
                    (which=="d32"?(float)i/3.f:(i==0?0.f:i==1?.25f:i==2?.5f:1.f));
                const uint8_t x=(uint8_t)std::lround(v*255.f);
                expected.insert(expected.end(),{x,x,x,255});
            }
        } else if(which=="rgb9e5") {
            t.format=VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            upload(std::vector<uint32_t>{(16u<<27)|(256u<<18)|(128u<<9)|64u,
                                        (16u<<27)|(64u<<18)|(256u<<9)|128u});
            expected={64,128,255,255,128,255,64,255};
        } else upload(floats);
        dump_image(cmd,&t,0,0,which.c_str());
    }
    require(dump_pending() && allocations.size()==1,"producer queues copy");
    consume_dump(true);
    require(waits==0 && invalidations==0 && png.empty() && !allocations[0]->destroyed,"open burst deferred");
    consume_dump(false);
    require(waits==1,"completed-submission wait");
    if(which=="ssbo" || which=="ssbo-tail") {
        const auto wanted=which=="ssbo"?"3 of 8 words not zero:":"3 of 16384 words not zero:";
        require(lines.size()==1 && lines[0].find(wanted)!=std::string::npos,"actual buffer nonzero statistics");
        require(lines[0].find(which=="ssbo"?"[7]=0x3":"[16383]=0x3")!=std::string::npos,"buffer tail visible");
    } else if(which=="stats") {
        require(png.empty() && lines.size()==1 && lines[0].find("mean 0.375 0.375 0.875 1")!=std::string::npos,"statistics-only values");
    } else {
        require(png==expected,"actual typed dump output");
        if(which=="nan-inf")require(lines[0].find("NaN 1, Inf 1 of 3")!=std::string::npos,"NaN/Inf statistics");
        if(which=="raw") {
            std::ifstream f;
            for(const auto& entry:std::filesystem::directory_iterator("."))
                if(entry.path().extension()==".f32")f=std::ifstream(entry.path(),std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
            require(bytes.size()==8+floats.size()*sizeof(float),"raw size");
            uint32_t wh[2]{};memcpy(wh,bytes.data(),8);
            require(wh[0]==2 && wh[1]==1 && !memcmp(bytes.data()+8,floats.data(),floats.size()*4),"raw dimensions/pixels");
        }
    }
    require(!dump_pending() && allocations[0]->destroyed,"resource cleanup");
    consume_dump(false);require(waits==1,"consumed queue does not wait again");
    std::puts("PASS actual dump readback output, burst deferral, wait and cleanup");
}
