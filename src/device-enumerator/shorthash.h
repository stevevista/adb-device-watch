#pragma once

#include <cstdint>
#include <algorithm>
#include <string>

namespace shorthash {

namespace detail {

constexpr uint64_t PRIMES[8] = {
  0x9e3779b97f4a7c15,
  0xc6a4a7935bd1e995,
  0x165667b19e3779f9, 
  0x85ebca77c2b2ae63,
  0xa54ff53a5f1d36f1,
  0x72be5d74f27b8965,
  0x3c6ef372fe94f82a,
  0x510e527fade682d1
};

constexpr uint32_t ROTATIONS[16] = {
  13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73
};

struct hash_state {
  uint64_t state[4] = { PRIMES[0], PRIMES[1], PRIMES[2], PRIMES[3] };
  uint64_t counter = 0;
};

constexpr uint64_t rotate_left(uint64_t x, uint32_t n) {
  return (x << n) | (x >> (64 - n));
}

constexpr uint64_t rotate_right(uint64_t x, uint32_t n) {
  return (x >> n) | (x << (64 - n));
}

constexpr uint64_t mix(uint64_t x, uint64_t key, uint32_t round) {
  uint64_t result = x;

  // round 1: XOR + ADD
  result ^= key;
  result *= PRIMES[round % 8];

  // round 2: ROTATE + XOR
  result = rotate_left(result, ROTATIONS[round % 16]);
  result ^= result >> 32;

  // round 3: non-linear transform
  result ^= (result << 21) ^ (result >> 17);
  result *= PRIMES[(round + 1) % 8];

  // round 4: final mix
  result = rotate_right(result, ROTATIONS[(round + 2) % 16]);
  result ^= result >> 13;
  result *= 0xff51afd7ed558ccd;  // final prime 

  return result;
}

template <typename Iter>
constexpr void process_block(hash_state& state, Iter first, Iter last) {
  size_t block_size = std::distance(first, last);
  uint64_t words[8] = {0};
  size_t word_count = block_size / 8;
  if (word_count > 8) {
    word_count = 8;
  }

  for (size_t i = 0; i < word_count; ++i) {
    words[i] = *reinterpret_cast<const uint64_t*>(&*(first + i * 8));
  }

  if (block_size % 8 != 0) {
    size_t remaining = block_size % 8;
    uint64_t last_word = 0;
    std::copy(last - remaining, last, &last_word);
    words[word_count] = last_word;
    word_count++;
  }

  for (size_t round = 0; round < 8; ++round) {
    uint64_t temp_state[4] = {state.state[0], state.state[1], state.state[2], state.state[3]};
            
    for (size_t i = 0; i < word_count; ++i) {
      uint64_t mixed = mix(words[i], state.counter + i, round);
      temp_state[i % 4] ^= mixed;
      temp_state[(i + 1) % 4] += rotate_left(mixed, i + round);
      temp_state[(i + 2) % 4] ^= rotate_right(mixed, i + round + 1);
    }

    state.state[0] = mix(temp_state[0], temp_state[1], round);
    state.state[1] = mix(temp_state[1], temp_state[2], round + 1);
    state.state[2] = mix(temp_state[2], temp_state[3], round + 2);
    state.state[3] = mix(temp_state[3], temp_state[0], round + 3);
  }

  state.counter += block_size;
}

constexpr uint64_t finalize(const hash_state& state) {
  uint64_t result = 0;

  for (int round = 0; round < 4; ++round) {
    uint64_t mixed = mix(state.state[round], state.counter, round);
    result ^= mixed;
    result = rotate_left(result, ROTATIONS[round * 4]);
    result += mixed * PRIMES[round + 4];
  }

  result ^= result >> 33;
  result *= 0xff51afd7ed558ccd;
  result ^= result >> 33;
  result *= 0xc4ceb9fe1a85ec53;
  result ^= result >> 33;

  return result;
}

} // namespace detail

template <typename Iter>
constexpr uint64_t hash(Iter first, Iter last) {
  auto state = detail::hash_state{};
        
  size_t length = std::distance(first, last);
  if (length < 64) {
    uint8_t padded[64] = {0};
    std::copy(first, last, padded);
            
    for (size_t i = length; i < 64; ++i) {
      padded[i] = static_cast<uint8_t>((length * i + 0x9e) & 0xFF);
    }

    detail::process_block(state, padded, padded + 64);
  } else {
    size_t full_blocks = length / 64;
    size_t remaining = length % 64;
                
    for (size_t i = 0; i < full_blocks; ++i) {
      detail::process_block(state, first + i * 64, first + (i + 1) * 64);
    }

    if (remaining > 0) {
      uint8_t last_block[64] = {0};
      std::copy(first + full_blocks * 64, last, last_block);
                
      for (size_t i = remaining; i < 64; ++i) {
        last_block[i] = static_cast<uint8_t>(((length + i) * 0x37) & 0xFF);
      }
                
      detail::process_block(state, last_block, last_block + 64);
    }
  }

  state.state[0] ^= length;
  state.state[1] ^= state.counter;
  state.state[2] ^= (length * 0x1234567890abcdef);
  state.state[3] ^= (state.counter * 0xfedcba9876543210);
        
  return detail::finalize(state);
}

template <typename Iter>
inline std::string hash_to_string(Iter first, Iter last) {
  uint64_t value = hash(first, last);
  char buffer[17];
  snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
  return std::string(buffer);
}

} // namespace shorthash
