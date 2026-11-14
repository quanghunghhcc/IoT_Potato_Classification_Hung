#pragma once
#include <cstddef>
#include <cstdint>
namespace flatbuffers {
class Verifier { public: Verifier(const uint8_t* buf, size_t len, uint32_t max_depth = 64, uint32_t max_tables = 1000000, bool check_alignment = true); };
template <class T> struct Offset {};
template <class T> struct Vector;
template <class T> struct Vector<Offset<T>> { uint32_t size() const; const T* Get(uint32_t i) const; };
struct String { const char* c_str() const; };
}
