#define VK_NO_PROTOTYPES
#define VK_USE_64_BIT_PTR_DEFINES 0
#include <vulkan/vulkan.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
constexpr uint32_t kLogW=320,kLogH=180;
struct Allocation { std::vector<uint8_t> bytes; };
using VmaAllocation=Allocation*;
struct LogSlot { VkBuffer buf{}; VmaAllocation alloc{}; void* map{}; bool used{}; int64_t at{}; uint32_t seq{},kind{}; };
LogSlot log_slots[3];
VkImage log_image{};
VmaAllocation log_image_alloc{};
std::mutex log_mu;
std::condition_variable log_cv;
std::deque<std::vector<uint8_t>> log_queue;
std::atomic<int> log_state{0};
std::set<uint64_t> live;
int allocations=0,fail_allocation=0,missing_map=0;
int opens=0,closes=0,starts=0,detaches=0,ready_messages=0,errors=0;
bool fail_file=false,fail_thread=false,file_live=false;
int writes=0,flushes=0,fail_at=1;
bool fail_flush=false;
std::vector<std::vector<uint8_t>> written;
const char* framelog_path() { return "synthetic-frame-log"; }
void logf(const char* format,...) {
    if(std::strstr(format,"writes every frame")) ++ready_messages;
    else ++errors;
}
int GetCurrentThread() { return 0; }
void SetThreadDescription(int,const wchar_t*) {}
bool create_image(const VkImageCreateInfo&,VkImage& image,VmaAllocation& allocation) {
    if(++allocations==fail_allocation)return false;
    image=uint64_t(allocations);allocation=new Allocation;live.insert(image);return true;
}
bool create_buffer_cached(uint64_t bytes,VkBufferUsageFlags,VkBuffer& buffer,VmaAllocation& allocation,void** mapped) {
    if(++allocations==fail_allocation)return false;
    buffer=uint64_t(allocations);allocation=new Allocation;allocation->bytes.resize(size_t(bytes));
    *mapped=allocations==missing_map?nullptr:allocation->bytes.data();live.insert(buffer);return true;
}
void destroy_buffer(VkBuffer buffer,VmaAllocation allocation) { CHECK(live.erase(buffer)==1);delete allocation; }
void destroy_image(VkImage image,VmaAllocation allocation) { CHECK(live.erase(image)==1);delete allocation; }
FILE* token() { return reinterpret_cast<FILE*>(uintptr_t(1234)); }
FILE* fake_open(const char*,const char*) { ++opens;if(fail_file)return nullptr;file_live=true;return token(); }
struct UnexpectedWrite {};
size_t fake_write(const void* bytes,size_t size,size_t count,FILE* file) {
    CHECK(file==token() && file_live && size==1);++writes;
    if(writes>fail_at)throw UnexpectedWrite{}; // Bounds an old worker that ignores the first failure.
    written.emplace_back(static_cast<const uint8_t*>(bytes),static_cast<const uint8_t*>(bytes)+count);
    return !fail_flush && writes==fail_at?count-1:count;
}
int fake_flush(FILE* file) { CHECK(file==token() && file_live);++flushes;return fail_flush && writes==fail_at?EOF:0; }
int fake_close(FILE* file) { CHECK(file==token() && file_live);file_live=false;++closes;return 0; }
struct FakeThread {
    template<class F> explicit FakeThread(F&&) { if(fail_thread)throw std::runtime_error("injected creation failure");++starts; }
    void detach() { ++detaches; }
};
#define fopen fake_open
#define fwrite fake_write
#define fflush fake_flush
#define fclose fake_close
/* DROP */
/* WRITER */
/* ENQUEUE */
/* READY */
#undef fopen
#undef fwrite
#undef fflush
#undef fclose
void startup(const std::string& name) {
    if(name=="image")fail_allocation=1;
    if(name.rfind("buffer",0)==0)fail_allocation=2+name.back()-'0';
    if(name.rfind("map",0)==0)missing_map=2+name.back()-'0';
    fail_file=name=="file";fail_thread=name=="thread";
    bool ok=false;
    try { ok=framelog_ready(); } catch(const std::exception&) { CHECK(false && "startup escaped instead of disabling the sink"); }
    if(name=="healthy") {
        CHECK(ok && live.size()==4 && opens==1 && file_live && starts==1 && detaches==1 && ready_messages==1);
        CHECK(framelog_ready() && allocations==4 && opens==1 && starts==1);
        fake_close(token());framelog_resources_drop();CHECK(live.empty());
    } else {
        CHECK(!ok && live.empty() && !file_live && ready_messages==0 && errors==1);
        const int attempts=allocations;CHECK(!framelog_ready() && allocations==attempts);
        for(const auto& slot:log_slots)CHECK(!slot.buf && !slot.alloc && !slot.map);
        CHECK(!log_image && !log_image_alloc && starts==0);
        CHECK(closes==int(fail_thread));
    }
}
void worker(const std::string& name) {
    fail_flush=name.rfind("flush",0)==0;fail_at=name.back()-'0';log_state=1;
    FILE* f=fake_open("synthetic","wb");
    for(int i=1;i<=6;i++)framelog_enqueue(std::vector<uint8_t>(8,uint8_t(i)));
    bool unexpected=false;
    std::atomic<bool> done{false};
    std::thread thread([&] { try { framelog_write(f); } catch(const UnexpectedWrite&) { unexpected=true; } done=true; });
    std::thread producer([&] { while(log_state.load()>0 && !done.load())std::this_thread::yield();framelog_enqueue(std::vector<uint8_t>(8,99)); });
    thread.join();producer.join();
    CHECK(!unexpected && log_state.load()<0 && closes==1 && !file_live && errors==1);
    CHECK(writes==fail_at && flushes==fail_at-int(!fail_flush));
    CHECK(log_queue.empty() && written.size()==size_t(fail_at));
    for(size_t i=0;i<written.size();i++)CHECK(written[i]==std::vector<uint8_t>(8,uint8_t(i+1)));
    framelog_enqueue(std::vector<uint8_t>(8,100));CHECK(log_queue.empty());
}
void queue_bounds() {
    framelog_enqueue({1});CHECK(log_queue.empty());
    log_state=1;
    for(int i=0;i<513;i++)framelog_enqueue({uint8_t(i)});
    CHECK(log_queue.size()==512 && log_queue.front()[0]==0 && log_queue.back()[0]==255);
    log_queue.clear();log_state=-1;framelog_enqueue({1});CHECK(log_queue.empty());
}
int main(int argc,char** argv) {
    CHECK(argc==2);const std::string name=argv[1];
    if(name=="queue")queue_bounds();
    else if(name.rfind("write",0)==0 || name.rfind("flush",0)==0)worker(name);
    else startup(name);
    std::printf("PASS %s\n",name.c_str());
}
