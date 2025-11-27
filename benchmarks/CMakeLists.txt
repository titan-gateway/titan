# SIMD Benchmark
add_executable(bench_simd bench_simd.cpp)

target_compile_options(bench_simd PRIVATE
  -O3
  -march=native
  -stdlib=libc++
)

target_link_libraries(bench_simd PRIVATE
  c++
  c++abi
)
