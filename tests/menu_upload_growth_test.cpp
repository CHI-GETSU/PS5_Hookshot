// Production Bitmap/Upload/copy_over are inserted by the Python runner; CPU-only spies.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#define VK_NO_PROTOTYPES
#define VK_USE_64_BIT_PTR_DEFINES 0
#include <vulkan/vulkan.h>
using VmaAllocation = void*;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
/* ACTUAL_STRUCTS */
std::mutex bmp_mu;
std::map<VkBuffer, std::vector<uint8_t>> live;
uint64_t serial = 0;
int allocations = 0, destructions = 0, copies = 0;
bool fail = false;
VkBuffer copied_buffer = 0;
uint32_t copied_count = 0;
VkBufferImageCopy copied_region{};
bool create_buffer(uint64_t size, VkBufferUsageFlags usage, bool host_visible, VkBuffer& b, VmaAllocation& a, void** map) {
    ++allocations;
    CHECK(usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT && host_visible);
    if (fail) { b = 0; a = nullptr; return false; }
    b = ++serial; a = reinterpret_cast<void*>(uintptr_t(b));
    live[b].resize(size); *map = live.at(b).data(); return true;
}
void destroy_buffer(VkBuffer b, VmaAllocation a) {
    CHECK(live.contains(b) && a == reinterpret_cast<void*>(uintptr_t(b)));
    live.erase(b); ++destructions;
}
void vkCmdCopyBufferToImage(VkCommandBuffer, VkBuffer b, VkImage, VkImageLayout layout,
                           uint32_t count, const VkBufferImageCopy* region) {
    CHECK(live.contains(b) && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && count);
    ++copies; copied_buffer = b; copied_count = count; copied_region = *region;
}
/* ACTUAL_FUNCTION */
void bitmap(Bitmap& b, uint32_t side, uint64_t version) {
    b.w = b.h = side; b.version = version; b.px.assign(size_t(side) * side * 4, uint8_t(version));
}
void upload(Bitmap& b, Upload& u) { copy_over(nullptr, 1, b, u, 10, 20, 4096, 4096); }
void check_bytes(const Bitmap& b, const Upload& u) {
    CHECK(u.map == live.at(u.buf).data() && u.size >= b.px.size() && u.version == b.version);
    CHECK(std::memcmp(u.map, b.px.data(), b.px.size()) == 0);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const bool failure = std::strcmp(argv[1], "failure") == 0;
    Bitmap b; Upload u;
    bitmap(b, 16, 1); upload(b, u); CHECK(live.size() == 1 && allocations == 1 && copies == 1); check_bytes(b, u);
    const Upload before = u;
    bitmap(b, 32, 2);
    if (failure) {
        fail = true; upload(b, u);
        CHECK(u.buf == before.buf && u.alloc == before.alloc && u.map == before.map && u.size == before.size && u.version == before.version);
        CHECK(live.size() == 1 && destructions == 0 && copies == 1);
        fail = false;
    }
    upload(b, u); CHECK(live.size() == 1 && destructions == 1 && u.buf != before.buf); check_bytes(b, u);
    CHECK(copied_region.imageOffset.x == 10 && copied_region.imageOffset.y == 20 && copied_region.imageExtent.width == 32);
    const int a = allocations;
    upload(b, u); CHECK(allocations == a); // cached capacity/version
    bitmap(b, 8, 3); upload(b, u); CHECK(allocations == a && live.size() == 1); check_bytes(b, u);
    b.keyed = true;
    VkBufferImageCopy row{}; row.bufferOffset = 4; row.imageOffset = {2, 3, 0}; row.imageExtent = {4, 1, 1};
    b.runs.push_back(row); upload(b, u);
    CHECK(copied_buffer == u.buf && copied_count == 1 && copied_region.imageOffset.x == 12 && copied_region.imageOffset.y == 23);
    CHECK(b.runs[0].imageOffset.x == 2 && b.runs[0].imageOffset.y == 3);
    b.keyed = false;
    for (uint32_t side : {64, 128, 256}) { bitmap(b, side, side); upload(b, u); CHECK(live.size() == 1); check_bytes(b, u); }
    destroy_buffer(u.buf, u.alloc); CHECK(live.empty());
    std::puts("PASS: menu uploads release replaced buffers, preserve old ownership on allocation failure, cache capacity and copy current pixels");
}
