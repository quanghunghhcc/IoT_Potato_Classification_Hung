#pragma once
#include <cstdlib>
#include <cstdint>
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
inline size_t heap_caps_get_free_size(uint32_t) { return 100000; }
inline size_t heap_caps_get_minimum_free_size(uint32_t) { return 90000; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 80000; }
inline void* heap_caps_malloc(size_t n, uint32_t) { return std::malloc(n); }
inline void* heap_caps_aligned_alloc(size_t a, size_t n, uint32_t) { (void)a; return std::malloc(n); }
inline void heap_caps_free(void* p) { std::free(p); }
