// The production statements are inserted by test_capture_slot_ownership.py.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
constexpr uint32_t kShotW=2,kShotH=1;
enum { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL=0 };
struct { int cmd=0; } r;
int shot_image=0,c=0;
uint8_t pixels[2][8]{};
void* shot_maps[2]={pixels[0],pixels[1]};
int shot_bufs[2]={0,1},shot_allocs[2]={0,1};
struct Copy { int slot; uint64_t frame; bool done=false; };
std::vector<Copy> queue;
Copy recorded{};
uint64_t finished=1;
bool submitted=false,fast=true;
int waits=0,waits_before_submit=0;
int64_t presents=0;
void complete_through(uint64_t value) {
    for (auto& copy:queue) if (!copy.done && copy.frame<=value) {
        std::memset(pixels[copy.slot],int(copy.frame & 255),sizeof pixels[copy.slot]);
        copy.done=true; finished=std::max(finished,copy.frame);
    }
}
void wait_value(uint64_t value) {
    ++waits; if (!submitted) ++waits_before_submit;
    complete_through(value); CHECK(finished>=value);
}
uint64_t completed() { return finished; }
void invalidate_mapped(int, uint64_t bytes) { CHECK(bytes==sizeof pixels[0]); }
void vkCmdCopyImageToBuffer(int,int,int,int slot,int,const int*) { recorded={slot,uint64_t(presents),false}; }
void submit_recording(decltype(r)&) {
    queue.push_back(recorded); submitted=true;
    complete_through(fast ? uint64_t(presents) : 2);
}
/* STATE */
/* READ_HELPER */
void run(int64_t current,bool fast_new,bool pending_old) {
    queue.clear(); std::memset(pixels,0,sizeof pixels);finished=1;submitted=false;fast=fast_new;
    waits=waits_before_submit=0;
    PendingShot pending;
    if (pending_old) { pending.on=true;pending.value=2;pending.presents=2;queue.push_back({0,2,false}); }
    std::vector<std::pair<int64_t,std::vector<uint8_t>>> captures;
    /* TAKE */
        captures.emplace_back(at_present,*px);
    };
    bool shot=false;
    // This is the actual consume condition on the intervening non-shot frame.
    /* CONSUME */
    CHECK(captures.empty() && waits==0 && pending.on==pending_old);
    presents=current;uint64_t value=uint64_t(current);shot=true;
    /* PRODUCER */
    /* CONSUME */
    if (pending_old) {
        CHECK(captures.size()==1 && captures[0].first==2);
        CHECK(std::all_of(captures[0].second.begin(),captures[0].second.end(),[](uint8_t b){return b==2;}));
        CHECK(waits==1);
        CHECK(waits_before_submit==int((current & 1)==0));
    } else { CHECK(captures.empty() && waits==0); }
    // Re-arming must discard a saved snapshot, not attach it to the new frame.
    /* REARM */
    CHECK(pending.on && pending.value==value && pending.presents==current);
    take(value,current);
    CHECK(captures.back().first==current);
    CHECK(std::all_of(captures.back().second.begin(),captures.back().second.end(),[current](uint8_t b){return b==(current & 255);}));
}
int main() {
    for (int64_t current=3;current<100;current++) {
        run(current,true,true);
        run(current,false,true);
        run(current,true,false);
    }
    std::puts("PASS: pending-shot pixels and metadata survive slot reuse; different slots stay asynchronous, no-pending path does not wait, rearm uses new pixels");
}
