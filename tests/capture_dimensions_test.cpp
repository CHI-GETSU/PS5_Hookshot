#define _CRT_SECURE_NO_WARNINGS
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <exception>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using VmaAllocation = void*;
struct { VkExtent2D swap_extent{1720,720}; VkPhysicalDeviceProperties props{}; VkDevice device{}; bool offscreen=false; } dev;
static unsigned calls=0, capture_entries=0, presented=0, errors=0;
static uintptr_t serial=0;
static uint64_t expected_bytes=0;
void logf(const char*, ...) { errors++; }
void require(bool ok,const char* why) { if(!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); } }
bool create_image(const VkImageCreateInfo& ci,VkImage& image,VmaAllocation& allocation) {
    calls++;
    if(!ci.extent.width || !ci.extent.height || ci.extent.width>dev.props.limits.maxImageDimension2D ||
       ci.extent.height>dev.props.limits.maxImageDimension2D) return false;
    image=reinterpret_cast<VkImage>(++serial); allocation=reinterpret_cast<void*>(serial); return true;
}
bool create_buffer(uint64_t size,VkBufferUsageFlags,bool,VkBuffer& buffer,VmaAllocation& allocation,void** map) {
    calls++; require(size!=0 && size==expected_bytes,"readback byte count must match the validated request");
    buffer=reinterpret_cast<VkBuffer>(++serial); allocation=reinterpret_cast<void*>(serial); *map=allocation; return true;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice,const VkImageViewCreateInfo* ci,const VkAllocationCallbacks*,VkImageView* view) {
    calls++; require(ci->image!=VK_NULL_HANDLE,"view without image"); *view=reinterpret_cast<VkImageView>(++serial); return VK_SUCCESS;
}
void destroy_image(VkImage,VmaAllocation) {}
void destroy_buffer(VkBuffer,VmaAllocation) {}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice,VkImageView,const VkAllocationCallbacks*) {}
/* SETUP */
void present_image(VkImage,uint32_t,uint32_t,const int*,VkImageView) { presented++; }
void tick() {
    VkImage src=reinterpret_cast<VkImage>(uintptr_t(0xff)); const char* shots="test";
    uint32_t sw=1720,sh=720; const int* crop=nullptr; VkImageView direct_view{}; bool interpolated=false;
    /* GATE */ { capture_entries++; }
    /* PRESENT_TAIL */
}
/* PNG */
int main(int argc,char**argv) {
    require(argc>=3,"fixture arguments");
    if(std::strcmp(argv[1],"png")==0) {
        std::vector<uint8_t> rgba(257*251*4,19);
        for(size_t i=0;i<257*251;i++) { rgba[i*4]=17; rgba[i*4+1]=73; rgba[i*4+2]=211; }
        write_png(argv[2],rgba.data(),257,251); return 0;
    }
    dev.props.limits.maxImageDimension2D=(uint32_t)std::strtoull(argv[1],nullptr,10);
    dev.swap_extent.width=std::min(1720u,dev.props.limits.maxImageDimension2D);
    dev.swap_extent.height=std::min(720u,dev.props.limits.maxImageDimension2D);
    const bool valid=std::atoi(argv[2])!=0; dev.offscreen=std::atoi(argv[3])!=0;
    if(valid) expected_bytes=(uint64_t)kShotW*kShotH*4;
    tick(); tick();
    require(capture_entries==(valid?2u:0u),"invalid dimensions reached the capture producer");
    require(valid?calls==5:calls==0,"dimension rejection must precede every Vulkan allocation");
    require(errors==(valid?0u:1u),"invalid capture must be reported exactly once");
    require(presented==(dev.offscreen?0u:2u),"capture rejection must preserve window/headless presentation policy");
}
