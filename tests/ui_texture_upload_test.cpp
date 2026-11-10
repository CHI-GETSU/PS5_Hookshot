#include <vulkan/vulkan.h>
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
namespace ImGui {
void* MemAlloc(size_t n) { return std::malloc(n); }
void MemFree(void* p) { std::free(p); }
ImGuiIO& GetIO() { std::abort(); } // No HDR/context transformation is modeled by this cache test.
}
/* DESTROY_PIXELS */
using VmaAllocation = void*;
struct { VkDevice device{}; bool hdr=false; } dev;
struct { int hdr_white=200; } opts;
ImU32 hdr10_of(ImU32 c, float) { std::abort(); return c; }
bool srgb_target=false, coherent=false;
int64_t presents=0;
/* STATE */
struct Buffer { std::vector<uint8_t> cpu, visible; bool coherent; };
struct Image { unsigned w,h; std::vector<uint8_t> bytes; VmaAllocation allocation; };
struct Copy { VkBuffer buffer; VkImage image; std::vector<VkBufferImageCopy> regions; };
std::map<VkBuffer, Buffer> buffers;
std::map<VkImage, Image> images;
std::map<VkImageView, VkImage> views;
std::vector<Copy> pending;
uintptr_t serial=10;
unsigned flushes=0, barriers=0;
uint64_t flushed_bytes=0;
std::string fault;
template<class T> T handle() { return reinterpret_cast<T>(++serial); }
bool create_buffer(uint64_t size, VkBufferUsageFlags usage, bool host_visible, VkBuffer& buffer, VmaAllocation& allocation, void** mapped) {
    require(size && usage==VK_BUFFER_USAGE_TRANSFER_SRC_BIT && host_visible,"mapped transfer staging request");
    if(fault=="buffer") return false;
    buffer=handle<VkBuffer>(); auto& b=buffers[buffer]; b.cpu.assign((size_t)size,0x99); b.visible.assign((size_t)size,0xcc); b.coherent=coherent;
    allocation=&b; *mapped=b.cpu.data(); return true;
}
void flush_mapped(VmaAllocation allocation,uint64_t size) {
    auto found=std::find_if(buffers.begin(),buffers.end(),[&](auto& item){return &item.second==allocation;});
    require(found!=buffers.end(),"flush must name the staging allocation");
    auto& b=found->second; require(size && size<=b.cpu.size(),"flush range must be within the staging allocation");
    std::copy_n(b.cpu.begin(),(size_t)size,b.visible.begin()); flushes++; flushed_bytes=size;
}
bool create_image(const VkImageCreateInfo& ci,VkImage& image,VmaAllocation& allocation) {
    if(fault=="image") return false;
    image=handle<VkImage>(); allocation=reinterpret_cast<void*>(++serial);
    images[image]={ci.extent.width,ci.extent.height,std::vector<uint8_t>((size_t)ci.extent.width*ci.extent.height*4,0x77),allocation};
    return true;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice,const VkImageViewCreateInfo* ci,const VkAllocationCallbacks*,VkImageView* view) {
    if(fault=="view") return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    require(images.count(ci->image),"view must refer to a live image"); *view=handle<VkImageView>(); views[*view]=ci->image; return VK_SUCCESS;
}
VkDescriptorSet ImGui_ImplVulkan_AddTexture(VkImageView view,VkImageLayout layout) {
    require(views.count(view) && layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"descriptor request");
    return fault=="descriptor"?VK_NULL_HANDLE:handle<VkDescriptorSet>();
}
void ImGui_ImplVulkan_RemoveTexture(VkDescriptorSet) {}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice,VkImageView view,const VkAllocationCallbacks*) { require(views.erase(view)==1,"view lifetime"); }
void destroy_image(VkImage image,VmaAllocation allocation) {
    require(images.count(image) && images.at(image).allocation==allocation,"image allocation ownership");
    for(auto& v:views) require(v.second!=image,"view must be destroyed before image");
    images.erase(image);
}
void destroy_buffer(VkBuffer buffer,VmaAllocation allocation) {
    require(buffers.count(buffer) && &buffers.at(buffer)==allocation,"staging allocation ownership"); buffers.erase(buffer);
}
void layout(VkCommandBuffer,VkImage image,VkImageLayout,VkImageLayout,VkPipelineStageFlags2,VkAccessFlags2,VkPipelineStageFlags2,VkAccessFlags2) {
    require(images.count(image),"barrier image must be live"); barriers++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer,VkBuffer buffer,VkImage image,VkImageLayout layout,uint32_t count,const VkBufferImageCopy* copies) {
    require(buffers.count(buffer) && images.count(image) && count && layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"valid copy resources");
    pending.push_back({buffer,image,std::vector<VkBufferImageCopy>(copies,copies+count)});
}
void submit() {
    // GPU execution happens after recording returns, not at vkCmdCopyBufferToImage.
    // A flush after recording but before submission is also valid Vulkan usage.
    for(auto& c:pending) {
        require(buffers.count(c.buffer) && images.count(c.image),"submitted resources must remain live");
        auto& b=buffers.at(c.buffer); auto& im=images.at(c.image); auto& bytes=b.coherent?b.cpu:b.visible;
        for(auto& r:c.regions) {
            require(r.bufferOffset%4==0 && r.imageOffset.x>=0 && r.imageOffset.y>=0,"copy offset/alignment");
            require(r.imageOffset.x+r.imageExtent.width<=im.w && r.imageOffset.y+r.imageExtent.height<=im.h,"copy rectangle bounds");
            for(unsigned y=0;y<r.imageExtent.height;y++) {
                const size_t src=(size_t)r.bufferOffset+(size_t)y*r.imageExtent.width*4;
                const size_t dst=((size_t)(r.imageOffset.y+y)*im.w+r.imageOffset.x)*4;
                require(src+r.imageExtent.width*4<=bytes.size(),"staging copy bounds");
                std::copy_n(bytes.begin()+src,r.imageExtent.width*4,im.bytes.begin()+dst);
            }
        }
    }
    pending.clear();
}
/* UPLOADER */
static std::vector<uint8_t> pattern(unsigned w,unsigned h,bool alpha,unsigned seed) {
    std::vector<uint8_t> rgba((size_t)w*h*4);
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        size_t at=((size_t)y*w+x)*4;
        rgba[at]=alpha?255:uint8_t(x*17+y*3+seed*13);
        rgba[at+1]=alpha?255:uint8_t(x*5+y*19+seed*29);
        rgba[at+2]=alpha?255:uint8_t(x*11+y*7+seed*41);
        rgba[at+3]=uint8_t(x*23+y*31+seed*53);
    }
    return rgba;
}
static void init_pixels(ImTextureData& tex,bool alpha,unsigned seed) {
    tex.Width=8; tex.Height=6; tex.Format=alpha?ImTextureFormat_Alpha8:ImTextureFormat_RGBA32; tex.BytesPerPixel=alpha?1:4;
    tex.Pixels=(unsigned char*)ImGui::MemAlloc(8*6*tex.BytesPerPixel); auto rgba=pattern(8,6,alpha,seed);
    for(unsigned i=0;i<8*6;i++) if(alpha) tex.Pixels[i]=rgba[i*4+3]; else std::memcpy(tex.Pixels+i*4,rgba.data()+i*4,4);
    tex.SetStatus(ImTextureStatus_WantCreate);
}
static void install_existing(ImTextureData& tex,bool alpha) {
    Tex* t=IM_NEW(Tex)(); t->image=handle<VkImage>(); t->alloc=reinterpret_cast<void*>(++serial); t->view=handle<VkImageView>(); t->set=handle<VkDescriptorSet>();
    images[t->image]={8,6,pattern(8,6,alpha,1),t->alloc}; views[t->view]=t->image;
    tex.BackendUserData=t; tex.SetTexID((ImTextureID)t->set); tex.SetStatus(ImTextureStatus_WantUpdates);
}
static void finish(ImTextureData& tex) {
    tex.WantDestroyNextFrame=true; tex.SetStatus(ImTextureStatus_WantDestroy); update_texture({},&tex); reap(true);
    require(buffers.empty() && images.empty() && views.empty() && dead.empty(),"retirement must release all resources");
}
int main(int argc,char**argv) {
    require(argc==3,"fixture arguments"); std::string mode=argv[1]; coherent=std::string(argv[2])=="coherent";
    bool alpha=mode.rfind("alpha",0)==0; ImTextureData tex; init_pixels(tex,alpha,2);
    if(mode=="no-backend") { tex.SetStatus(ImTextureStatus_WantUpdates); update_texture({},&tex); require(buffers.empty() && pending.empty() && !flushes,"no backend has no upload"); return 0; }
    if(mode=="empty") { tex.SetStatus(ImTextureStatus_WantCreate); tex.Width=0; update_texture({},&tex); require(buffers.empty() && pending.empty() && !flushes,"empty request has no upload"); return 0; }
    if(mode=="destroy") { install_existing(tex,alpha); finish(tex); require(!flushes && pending.empty(),"destroy has no staging upload"); return 0; }
    bool dirty=mode.find("dirty")!=std::string::npos || mode.find("fallback")!=std::string::npos;
    auto expected=pattern(8,6,alpha,2);
    if(dirty) {
        install_existing(tex,alpha); expected=pattern(8,6,alpha,1);
        std::vector<ImTextureRect> rects=mode.find("fallback")!=std::string::npos?std::vector<ImTextureRect>{{1,1,4,2}}:std::vector<ImTextureRect>{{0,1,2,2},{5,4,3,2}};
        if(rects.size()==1) tex.UpdateRect=rects[0]; else for(auto& r:rects) tex.Updates.push_back(r);
        auto changed=pattern(8,6,alpha,2);
        for(auto& r:rects) for(unsigned y=r.y;y<unsigned(r.y+r.h);y++) for(unsigned x=r.x;x<unsigned(r.x+r.w);x++)
            for(unsigned ch=0;ch<4;ch++) expected[((size_t)y*8+x)*4+ch]=changed[((size_t)y*8+x)*4+ch];
    }
    if(mode.rfind("failure-",0)==0) {
        fault=mode.substr(8); update_texture({},&tex);
        require(tex.Status==ImTextureStatus_WantCreate && !tex.BackendUserData && tex.TexID==ImTextureID_Invalid,"failed create must remain retryable");
        require(buffers.empty() && images.empty() && views.empty() && dead.empty() && pending.empty() && !flushes,"failed create must not flush or record"); fault.clear();
    }
    srgb_target=alpha; update_texture({},&tex);
    require(tex.Status==ImTextureStatus_OK && tex.BackendUserData && pending.size()==1 && barriers==2,"successful upload publication/recording");
    require(flushes<=1,"one staging range is sufficient");
    auto& staged=buffers.at(pending[0].buffer); if(flushes) require(flushed_bytes==staged.cpu.size(),"flush must cover the entire packed staging range");
    VkImage image=pending[0].image; submit();
    require(images.at(image).bytes==expected,"GPU must see expected RGBA/alpha bytes and preserve untouched pixels");
    presents=3; reap(false); require(!buffers.empty(),"staging must live through frames in flight");
    presents=4; reap(false); require(buffers.empty(),"completed staging should retire");
    finish(tex); std::puts("PASS: production UI texture upload visibility and failure/retirement controls");
}
