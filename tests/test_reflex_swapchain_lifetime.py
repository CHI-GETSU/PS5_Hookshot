"""Compile actual Reflex consumers with deterministic swapchain-retirement interleavings. No GPU/SDK."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
p.add_argument('--cxx', default='clang++')
a = p.parse_args()
source = (a.source/'gpu/vk/vk_reflex.cpp').read_text()

def function(marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

functions = '\n'.join(function(marker) for marker in [
    'bool on()', 'void mark(', 'bool reflex_swapchain_stale()',
    'void reflex_swapchain_created(', 'void reflex_swapchain_destroyed()',
    'void reflex_present_begins()', 'void reflex_submit_ends()',
    'const void* reflex_present_id(', 'void reflex_presented()', 'void reflex_sleep()',
])
fixture = r'''
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <mutex>
using VkSwapchainKHR=uint64_t;using VkLatencyMarkerNV=int;
constexpr uint64_t VK_NULL_HANDLE=0;
constexpr int VK_SUCCESS=0,VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV=1,
    VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO=2,VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV=3,
    VK_LATENCY_MARKER_SIMULATION_START_NV=4,VK_LATENCY_MARKER_INPUT_SAMPLE_NV=5,
    VK_LATENCY_MARKER_SIMULATION_END_NV=6,VK_LATENCY_MARKER_RENDERSUBMIT_START_NV=7,
    VK_LATENCY_MARKER_RENDERSUBMIT_END_NV=8,VK_LATENCY_MARKER_PRESENT_START_NV=9,VK_LATENCY_MARKER_PRESENT_END_NV=10;
struct VkLatencySleepInfoNV {int type;uint64_t signalSemaphore=0,value=0;};
struct VkSemaphoreWaitInfo {int type;unsigned semaphoreCount=0;const uint64_t* pSemaphores=nullptr;const uint64_t* pValues=nullptr;};
struct VkSetLatencyMarkerInfoNV {int type;uint64_t presentID=0;int marker=0;};
struct PresentId {const void* pNext=nullptr;unsigned swapchainCount=0;const uint64_t* pPresentIds=nullptr;};
void (*before_lock)()=nullptr;
struct TestMutex {
    std::mutex native;
    void lock(){auto hook=before_lock;before_lock=nullptr;if(hook)hook();native.lock();}
    void unlock(){native.unlock();}
};
int markers=0,sleeps=0,modes=0;bool invalid=false,fg=false;int wanted=1;
void marker_call(int,uint64_t chain,const VkSetLatencyMarkerInfoNV*){if(!chain)invalid=true;++markers;}
int sleep_call(int,uint64_t chain,const VkLatencySleepInfoNV*){if(!chain)invalid=true;++sleeps;return VK_SUCCESS;}
struct Reflex {
    bool ready=true,latency=true,latency_next=true,frame_marked=true;
    uint64_t swapchain=42,swapchain_generation=0,wake=100,wake_value=0,id=1;
    int mode=1;TestMutex mu;PresentId pid;
    void (*marker)(int,uint64_t,const VkSetLatencyMarkerInfoNV*)=marker_call;
    int (*sleep)(int,uint64_t,const VkLatencySleepInfoNV*)=sleep_call;
} state;
struct {int device=1;}dev;
Reflex& rx(){return state;}
bool framegen_on(){return fg;}
int wanted_mode(){return wanted;}
void apply_mode(){if(!state.swapchain)invalid=true;++modes;}
void log_timings(){}
template<class... T> void logf(const char*,T...){}
int vkWaitSemaphores(int,const VkSemaphoreWaitInfo*,uint64_t);
''' + functions + r'''
enum Action {Keep,Retire,Replace,ReuseHandle,LaterSleep,Disable,EnableFG};
Action action=Keep;
int vkWaitSemaphores(int,const VkSemaphoreWaitInfo* wait,uint64_t){
    const uint64_t requested=*wait->pValues;
    if(action==Retire || action==Replace || action==ReuseHandle){
        auto old=state.swapchain;reflex_swapchain_destroyed();
        if(action!=Retire)reflex_swapchain_created(action==ReuseHandle?old:99);
    }
    if(action==LaterSleep)++state.wake_value;
    if(action==Disable)wanted=0;
    if(action==EnableFG)fg=true;
    if(*wait->pValues!=requested)invalid=true; // The wait must use its own timeline target.
    return VK_SUCCESS;
}
void reset(){
    state.swapchain=42;state.swapchain_generation=0;state.ready=state.latency=state.latency_next=state.frame_marked=true;
    state.wake_value=0;state.id=1;state.mode=1;markers=sleeps=modes=0;invalid=fg=false;wanted=1;before_lock=nullptr;action=Keep;
}
void retire_before_lock(){reflex_swapchain_destroyed();}
int main(){
    reset();reflex_sleep();if(invalid||markers!=2||sleeps!=1||!state.frame_marked)return 1;
    for(auto change:{Retire,Replace,ReuseHandle,LaterSleep,Disable,EnableFG}){
        reset();action=change;reflex_sleep();
        if(invalid||markers){std::puts("FAIL: a completed wait marked a retired/replaced/ineligible chain");return 2;}
        if((change==Retire||change==Replace||change==ReuseHandle)&&state.frame_marked)return 3;
    }
    // Retirement between the eligibility check and its lock must stop every consumer.
    reset();before_lock=retire_before_lock;reflex_sleep();if(invalid||sleeps||modes||markers)return 4;
    reset();before_lock=retire_before_lock;reflex_present_begins();if(invalid||markers)return 5;
    reset();before_lock=retire_before_lock;reflex_submit_ends();if(invalid||markers)return 6;
    reset();before_lock=retire_before_lock;int next=1;if(reflex_present_id(&next)!=&next||invalid||markers)return 7;
    reset();before_lock=retire_before_lock;reflex_presented();if(invalid||markers)return 8;
    reset();action=ReuseHandle;reflex_sleep();action=Keep;reflex_sleep();if(invalid||markers!=2||!state.frame_marked)return 9;
    reset();reflex_present_begins();reflex_submit_ends();reflex_present_id(nullptr);reflex_presented();
    if(invalid||markers!=5)return 10;
    std::puts("PASS: Reflex retirement before locks and across waits, reused handles, stable wait targets and recovery");
}
'''
with tempfile.TemporaryDirectory(prefix='reflex-swapchain-') as tmp:
    cpp, exe = Path(tmp)/'fixture.cpp', Path(tmp)/'fixture.exe'
    cpp.write_text(fixture)
    subprocess.run([a.cxx, '-std=c++20', '-O1', str(cpp), '-o', str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
