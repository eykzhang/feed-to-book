#include <benchmark/benchmark.h>

// Placeholder so the bench target has something to run. Delete once real benchmarks exist.
static void BM_Noop(benchmark::State& state) {
  for (auto _ : state) {
    int x = 0;
    benchmark::DoNotOptimize(x);
  }
}
BENCHMARK(BM_Noop);
