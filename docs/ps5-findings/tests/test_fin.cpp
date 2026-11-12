// PS5 port: unit test of the decision functions of ps5_switches.h (measure 5a, eviction fix)
#include "ps5_switches.h"   /* gpu/vk/ps5_switches.h from patch 03 (pass -I) */
#include <cstdio>
#include <tuple>
#include <vector>
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
    // option parsing: off by default
    CHECK(!ps5_keep_rendered(nullptr)); CHECK(!ps5_keep_rendered("0")); CHECK(ps5_keep_rendered("1"));
    // the bug case: texture #1400, rendered, not released, not overwritten: protected only with the option
    CHECK(ps5_rendered_protected(true, true, false, false));
    CHECK(!ps5_rendered_protected(false, true, false, false));   // option off: old behaviour
    CHECK(!ps5_rendered_protected(true, false, false, false));   // not rendered: guest copy can refill it
    CHECK(!ps5_rendered_protected(true, true, true, false));     // finalized by the game: may go
    CHECK(!ps5_rendered_protected(true, true, false, true));     // overwritten in guest memory: stale anyway
    // candidate: finalized and unused since
    CHECK(!ps5_finalize_candidate(0, 5));
    CHECK(ps5_finalize_candidate(100, 100)); CHECK(ps5_finalize_candidate(100, 90));
    CHECK(!ps5_finalize_candidate(100, 101));                     // used again after: not a candidate
    // block tally: block 1 only candidates (whole), block 2 mixed, block 3 none
    std::vector<std::tuple<uint64_t, uint64_t, bool>> r = {
        {1, 10 << 20, true}, {2, 4 << 20, true}, {1, 6 << 20, true}, {2, 8 << 20, false}, {3, 2 << 20, false}};
    Ps5BlockTally k = ps5_block_tally(r.begin(), r.end());
    CHECK(k.blocks == 3); CHECK(k.whole == 1); CHECK(k.whole_bytes == (16u << 20));
    CHECK(k.cand_n == 3); CHECK(k.cand_bytes == (20u << 20));
    std::vector<std::tuple<uint64_t, uint64_t, bool>> e;
    k = ps5_block_tally(e.begin(), e.end()); CHECK(k.blocks == 0 && k.whole == 0 && k.cand_n == 0);
    puts(fails ? "FAILED" : "ALL OK");
    return fails != 0;
}
