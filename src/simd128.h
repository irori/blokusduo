#pragma once

#include <stdint.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#else
#error "simd128.h requires ARM NEON or WebAssembly SIMD"
#endif

namespace blokusduo::simd128 {

#if defined(_MSC_VER)
#define SIMD128_INLINE __forceinline
#else
#define SIMD128_INLINE inline __attribute__((always_inline))
#endif

#if defined(__ARM_NEON)

using U16x8 = uint16x8_t;

SIMD128_INLINE U16x8 zero() { return vdupq_n_u16(0); }
SIMD128_INLINE U16x8 splat(uint16_t value) { return vdupq_n_u16(value); }
SIMD128_INLINE U16x8 load(const uint16_t* values) {
  return vld1q_u16(values);
}

SIMD128_INLINE U16x8 bit_and(U16x8 a, U16x8 b) {
  return vandq_u16(a, b);
}

SIMD128_INLINE U16x8 bit_or(U16x8 a, U16x8 b) {
  return vorrq_u16(a, b);
}

SIMD128_INLINE U16x8 and_not(U16x8 value, U16x8 mask) {
  return vbicq_u16(value, mask);
}

template <int Bits>
SIMD128_INLINE U16x8 shift_left(U16x8 value) {
  static_assert(Bits >= 0 && Bits < 16);
  return vshlq_n_u16(value, Bits);
}

template <int Bits>
SIMD128_INLINE U16x8 shift_right(U16x8 value) {
  static_assert(Bits >= 0 && Bits < 16);
  return vshrq_n_u16(value, Bits);
}

// Returns lanes [Lanes..7] from low followed by [0..Lanes-1] from high.
template <int Lanes>
SIMD128_INLINE U16x8 align_right(U16x8 low, U16x8 high) {
  static_assert(Lanes == 1 || Lanes == 2 || Lanes == 7);
  return vextq_u16(low, high, Lanes);
}

template <int Lane>
SIMD128_INLINE U16x8 replace_lane(U16x8 value, uint16_t lane_value) {
  static_assert(Lane >= 0 && Lane < 8);
  return vsetq_lane_u16(lane_value, value, Lane);
}

template <int Lane>
SIMD128_INLINE U16x8 or_lane(U16x8 value, uint16_t bits) {
  static_assert(Lane >= 0 && Lane < 8);
  return replace_lane<Lane>(
      value, vgetq_lane_u16(value, Lane) | bits);
}

SIMD128_INLINE bool any_true(U16x8 value) {
  const uint64x2_t words = vreinterpretq_u64_u16(value);
  return (vgetq_lane_u64(words, 0) | vgetq_lane_u64(words, 1)) != 0;
}

SIMD128_INLINE int popcount_sum(U16x8 a, U16x8 b) {
  const uint8x16_t byte_counts = vaddq_u8(
      vcntq_u8(vreinterpretq_u8_u16(a)),
      vcntq_u8(vreinterpretq_u8_u16(b)));
  const uint16x8_t pair_counts = vpaddlq_u8(byte_counts);
  const uint32x4_t quad_counts = vpaddlq_u16(pair_counts);
  const uint64x2_t half_counts = vpaddlq_u32(quad_counts);
  return vgetq_lane_u64(half_counts, 0) +
         vgetq_lane_u64(half_counts, 1);
}

#elif defined(__wasm_simd128__)

using U16x8 = v128_t;

SIMD128_INLINE U16x8 zero() { return wasm_u16x8_splat(0); }
SIMD128_INLINE U16x8 splat(uint16_t value) {
  return wasm_u16x8_splat(value);
}
SIMD128_INLINE U16x8 load(const uint16_t* values) {
  return wasm_v128_load(values);
}

SIMD128_INLINE U16x8 bit_and(U16x8 a, U16x8 b) {
  return wasm_v128_and(a, b);
}

SIMD128_INLINE U16x8 bit_or(U16x8 a, U16x8 b) {
  return wasm_v128_or(a, b);
}

SIMD128_INLINE U16x8 and_not(U16x8 value, U16x8 mask) {
  return wasm_v128_andnot(value, mask);
}

template <int Bits>
SIMD128_INLINE U16x8 shift_left(U16x8 value) {
  static_assert(Bits >= 0 && Bits < 16);
  return wasm_i16x8_shl(value, Bits);
}

template <int Bits>
SIMD128_INLINE U16x8 shift_right(U16x8 value) {
  static_assert(Bits >= 0 && Bits < 16);
  return wasm_u16x8_shr(value, Bits);
}

// Returns lanes [Lanes..7] from low followed by [0..Lanes-1] from high.
template <int Lanes>
SIMD128_INLINE U16x8 align_right(U16x8 low, U16x8 high) {
  static_assert(Lanes == 1 || Lanes == 2 || Lanes == 7);
  if constexpr (Lanes == 1) {
    return wasm_i16x8_shuffle(
        low, high, 1, 2, 3, 4, 5, 6, 7, 8);
  } else if constexpr (Lanes == 2) {
    return wasm_i16x8_shuffle(
        low, high, 2, 3, 4, 5, 6, 7, 8, 9);
  } else {
    return wasm_i16x8_shuffle(
        low, high, 7, 8, 9, 10, 11, 12, 13, 14);
  }
}

template <int Lane>
SIMD128_INLINE U16x8 replace_lane(U16x8 value, uint16_t lane_value) {
  static_assert(Lane >= 0 && Lane < 8);
  return wasm_u16x8_replace_lane(value, Lane, lane_value);
}

template <int Lane>
SIMD128_INLINE U16x8 or_lane(U16x8 value, uint16_t bits) {
  static_assert(Lane >= 0 && Lane < 8);
  return replace_lane<Lane>(
      value, wasm_u16x8_extract_lane(value, Lane) | bits);
}

SIMD128_INLINE bool any_true(U16x8 value) {
  return wasm_v128_any_true(value);
}

SIMD128_INLINE int popcount_sum(U16x8 a, U16x8 b) {
  const v128_t byte_counts = wasm_i8x16_add(
      wasm_i8x16_popcnt(a), wasm_i8x16_popcnt(b));
  const v128_t pair_counts =
      wasm_u16x8_extadd_pairwise_u8x16(byte_counts);
  const v128_t quad_counts =
      wasm_u32x4_extadd_pairwise_u16x8(pair_counts);
  return wasm_u32x4_extract_lane(quad_counts, 0) +
         wasm_u32x4_extract_lane(quad_counts, 1) +
         wasm_u32x4_extract_lane(quad_counts, 2) +
         wasm_u32x4_extract_lane(quad_counts, 3);
}

#endif

#undef SIMD128_INLINE

}  // namespace blokusduo::simd128
