#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using Pixels = std::shared_ptr<std::vector<uint8_t>>;
static void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static Pixels sample(Pixels px, uint32_t kShotW, uint32_t kShotH) {
/* SAMPLING */
    return small;
}
struct Detection { bool event; int hits, up, peak, region_hits, region_peak; };
static Detection detect(Pixels sa, Pixels sb, Pixels small) {
    constexpr uint32_t W = 64, H = 36;
    int flashes_left = 1;
/* DETECTOR */
    return {/* DECISION */, hits, up, peak, region_hits, region_peak};
}
static Pixels solid(uint32_t w, uint32_t h, std::array<uint8_t, 3> rgb) {
    auto p = std::make_shared<std::vector<uint8_t>>((size_t)w * h * 4, 19);
    for (size_t i = 0; i < (size_t)w * h; i++)
        for (int ch = 0; ch < 3; ch++) (*p)[i * 4 + ch] = rgb[ch];
    return p;
}
static void constants() {
    const uint32_t sizes[][2] = {{1280,720},{1920,1080},{3440,1440},{5120,1440},{3840,2160},
        {1439,701},{65,37},{64,36},{63,35},{1,1},{17,1440},{3440,9}};
    const std::array<uint8_t, 3> colors[] = {{0,0,0},{64,64,64},{128,128,128},{200,200,200},
        {254,254,254},{255,255,255},{17,73,211}};
    for (auto& size : sizes) for (auto rgb : colors) {
        auto result = sample(solid(size[0], size[1], rgb), size[0], size[1]);
        require(result->size() == 64 * 36 * 3, "the fixed detector grid size");
        for (size_t i = 0; i < result->size(); i++)
            require((*result)[i] == rgb[i % 3], "a constant channel must keep its brightness at every capture size");
    }
}
static void partitions() {
    const uint32_t sizes[][2] = {{3440,1440},{1439,701},{65,37},{5120,1440}};
    for (auto& size : sizes) {
        uint32_t w = size[0], h = size[1];
        auto p = solid(w, h, {0,0,0});
        // Label each source pixel by the normalized cell owning it. Every sample
        // inside that cell must return its label, including remainder rows/columns.
        for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
            const unsigned cx = ((uint64_t)(x + 1) * 64 + w - 1) / w - 1;
            const unsigned cy = ((uint64_t)(y + 1) * 36 + h - 1) / h - 1;
            (*p)[((size_t)y*w+x)*4] = (uint8_t)(cx*3);
            (*p)[((size_t)y*w+x)*4+1] = (uint8_t)(cy*7);
            (*p)[((size_t)y*w+x)*4+2] = (uint8_t)((cx+cy)*2);
        }
        auto result = sample(p,w,h);
        for (unsigned y=0;y<36;y++) for(unsigned x=0;x<64;x++) {
            const size_t at = (y*64+x)*3;
            require((*result)[at] == x*3 && (*result)[at+1] == y*7 && (*result)[at+2] == (x+y)*2,
                "normalized cells must retain their own source region through the image edges");
        }
    }
}
static void downstream() {
    for (const auto size : {std::array<uint32_t,2>{1280,720},{1920,1080},{3440,1440},{5120,1440},{65,37}}) {
        const auto w=size[0],h=size[1];
        auto a=sample(solid(w,h,{200,200,200}),w,h);
        auto b=sample(solid(w,h,{255,255,255}),w,h);
        auto pulse=detect(a,b,a);
        require(pulse.event && pulse.hits==64*36 && pulse.up==64*36 && pulse.peak==55 && pulse.region_peak==55,
            "the production detector must preserve the polarity and magnitude of a bright middle-frame pulse");
        require(!detect(a,a,a).event,"a stable picture is not a flash");
        auto low=sample(solid(w,h,{210,210,210}),w,h), high=sample(solid(w,h,{250,250,250}),w,h);
        require(!detect(low,high,low).event,"a below-threshold pulse must not become a flash through byte wrapping");
        Pixels edges[3];
        for(unsigned frame=0;frame<3;frame++) {
            auto p=solid(w,h,{0,0,0});
            for(uint32_t y=0;y<h;y++) for(uint32_t x=0;x<w*(frame+1)/4;x++)
                for(unsigned ch=0;ch<3;ch++) (*p)[((size_t)y*w+x)*4+ch]=255;
            edges[frame]=sample(p,w,h);
        }
        require(!detect(edges[0],edges[1],edges[2]).event,"an edge moving in one direction is not a returning one-frame flash");
    }
}
int main(int argc,char**argv) {
#ifdef _WIN32
    _putenv_s("RX_VK_FLASH_REGION","48");
#else
    setenv("RX_VK_FLASH_REGION","48",1);
#endif
    std::string which=argc>1?argv[1]:"all";
    if(which=="all"||which=="constant") constants();
    if(which=="all"||which=="partition") partitions();
    if(which=="all"||which=="detector") downstream();
    std::puts("PASS: production flash sampling and cell/region detection preserve brightness, partitions and controls");
}
