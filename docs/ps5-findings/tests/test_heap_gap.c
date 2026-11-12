/* PS5 port: HOS_HEAP_GAP's effect on what the heap gives back (PC, mihawk's host kernel read in place).
 * argv[1]: 0 or 1 (gap). Prints what is mapped at the peak, after the frees and after a trim, and checks the count. */
#include "ps5platform/heap.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
size_t ps5p_heap_trim(void);
void ps5p_heap_set_piece(size_t bytes);
void ps5p_heap_set_gap(bool on);
void ps5p_heap_set_count(bool on);
long long ps5p_heap_in_use(void);
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int stop;   /* atomic accesses */
static void *churn(void *arg) {   /* allocations and frees of every size while another thread trims */
   unsigned seed = (unsigned)(size_t)arg;
   void *p[64] = {0};
   for (int r = 0; r < 20000; r++) {
      const int i = rand_r(&seed) % 64;
      ps5_heap_free(p[i]);
      p[i] = ps5_heap_malloc((size_t)(rand_r(&seed) % (256 << 10)) + 1);
      memset(p[i], 7, 16);
   }
   for (int i = 0; i < 64; i++) ps5_heap_free(p[i]);
   return NULL;
}
static void *trimmer(void *arg) { (void)arg; while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) ps5p_heap_trim(); return NULL; }
int main(int argc, char **argv) {
   const bool gap = argc > 1 && atoi(argv[1]);
   const int keep = argc > 2 ? atoi(argv[2]) : 50;   /* one block in `keep` stays */
   ps5p_heap_set_piece((size_t)2 << 20);
   ps5p_heap_set_gap(gap);
   ps5p_heap_set_count(true);
   /* a "compile burst": 400 MiB in 100 KiB blocks, then all freed but one block in 50 (what a long-lived object keeps) */
   enum { N = 4000 };
   static void *p[N];
   for (int i = 0; i < N; i++) { p[i] = ps5_heap_malloc(100 << 10); memset(p[i], 1, 4096); }
   struct ps5_heap_stats a, b, c;
   ps5_heap_stats(&a);
   const long long used_peak = ps5p_heap_in_use();
   for (int i = 0; i < N; i++) if (i % keep) { ps5_heap_free(p[i]); p[i] = NULL; }
   ps5_heap_stats(&b);
   const long long used = ps5p_heap_in_use();
   const size_t gave = ps5p_heap_trim();
   ps5_heap_stats(&c);
   printf("gap %d keep 1/%d: peak mapped %zu MiB (in use %lld), after frees %zu MiB (in use %lld), after trim %zu MiB (trim says %zu), "
          "%u segments\n", gap, keep, a.mapped_bytes >> 20, used_peak >> 20, b.mapped_bytes >> 20, used >> 20, c.mapped_bytes >> 20,
          gave >> 20, c.segments);
   CHECK(used_peak >= (long long)N * (100 << 10) && used_peak < (long long)N * (101 << 10));
   CHECK(used >= (long long)((N + keep - 1) / keep) * (100 << 10) && used < (long long)((N + keep - 1) / keep) * (101 << 10));
   CHECK(gave == b.mapped_bytes - c.mapped_bytes);
   for (int i = 0; i < N; i++) if (p[i]) ((char *)p[i])[100] = 3;   /* what stays is still usable */
   for (int i = 0; i < N; i++) ps5_heap_free(p[i]);
   CHECK(ps5p_heap_in_use() < (1 << 20));
   /* 8 threads allocating and freeing while one trims all the time */
   pthread_t t[8], tr;
   pthread_create(&tr, NULL, trimmer, NULL);
   for (int i = 0; i < 8; i++) pthread_create(&t[i], NULL, churn, (void *)(size_t)(i + 1));
   for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
   __atomic_store_n(&stop, 1, __ATOMIC_RELEASE); pthread_join(tr, NULL);
   CHECK(ps5p_heap_in_use() < (1 << 20));
   ps5p_heap_trim();
   ps5_heap_stats(&c);
   printf("gap %d: after the threads and a trim, %zu MiB mapped, in use %lld KiB\n", gap, c.mapped_bytes >> 20, ps5p_heap_in_use() >> 10);
   puts(fails ? "FAILED" : "ALL OK");
   return fails != 0;
}
