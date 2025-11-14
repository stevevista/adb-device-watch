/*
 * 短输入优化哈希算法 - 专为≤1024字节输入设计的8字节哈希
 * 基于多轮混合和密钥调度技术，最大化碰撞抵抗能力
 */

#ifndef SHORT_HASH_OPTIMIZED_H
#define SHORT_HASH_OPTIMIZED_H

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <chrono>

namespace short_hash {

// 8字节哈希结果
struct Hash64 {
    uint64_t value;
    
    Hash64() : value(0) {}
    explicit Hash64(uint64_t v) : value(v) {}
    
    std::string to_string() const {
        char buffer[17];
        snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
        return std::string(buffer);
    }
    
    bool operator==(const Hash64& other) const {
        return value == other.value;
    }
    
    bool operator<(const Hash64& other) const {
        return value < other.value;
    }
};

// 优化的短哈希算法
class ShortHashOptimized {
private:
    // 质数常量 - 用于混合
    static constexpr uint64_t PRIMES[8] = {
        0x9e3779b97f4a7c15,  // 黄金比例
        0xc6a4a7935bd1e995,  // 大质数1
        0x165667b19e3779f9,  // 大质数2
        0x85ebca77c2b2ae63,  // 大质数3
        0xa54ff53a5f1d36f1,  // 大质数4
        0x72be5d74f27b8965,  // 大质数5
        0x3c6ef372fe94f82a,  // 大质数6
        0x510e527fade682d1   // 大质数7
    };
    
    // 旋转常量
    static constexpr uint32_t ROTATIONS[16] = {
        13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73
    };
    
    // 最大输入长度
    static constexpr size_t MAX_INPUT_SIZE = 1024;
    
    // 内部状态
    struct InternalState {
        uint64_t state[4];  // 256位内部状态
        uint64_t counter;     // 输入计数器
        
        InternalState() {
            reset();
        }
        
        void reset() {
            state[0] = PRIMES[0];
            state[1] = PRIMES[1];
            state[2] = PRIMES[2];
            state[3] = PRIMES[3];
            counter = 0;
        }
    };
    
    // 旋转操作
    static uint64_t rotate_left(uint64_t x, uint32_t n) {
        return (x << n) | (x >> (64 - n));
    }
    
    static uint64_t rotate_right(uint64_t x, uint32_t n) {
        return (x >> n) | (x << (64 - n));
    }
    
    // 混合函数 - 使用多轮非线性变换
    static uint64_t mix(uint64_t x, uint64_t key, uint32_t round) {
        uint64_t result = x;
        
        // 轮次1: XOR + 乘法
        result ^= key;
        result *= PRIMES[round % 8];
        
        // 轮次2: 旋转 + XOR
        result = rotate_left(result, ROTATIONS[round % 16]);
        result ^= result >> 32;
        
        // 轮次3: 非线性变换
        result ^= (result << 21) ^ (result >> 17);
        result *= PRIMES[(round + 1) % 8];
        
        // 轮次4: 最终混合
        result = rotate_right(result, ROTATIONS[(round + 2) % 16]);
        result ^= result >> 13;
        result *= 0xff51afd7ed558ccd;  // 最终大质数
        
        return result;
    }
    
    // 处理数据块（64字节）
    static void process_block(InternalState& state, const uint8_t* block, size_t block_size) {
        // 将块转换为64位整数数组
        uint64_t words[8] = {0};
        size_t word_count = std::min(block_size / 8, size_t(8));
        
        for (size_t i = 0; i < word_count; ++i) {
            words[i] = *reinterpret_cast<const uint64_t*>(block + i * 8);
        }
        
        // 处理剩余字节
        if (block_size % 8 != 0) {
            size_t remaining = block_size % 8;
            uint64_t last_word = 0;
            std::memcpy(&last_word, block + word_count * 8, remaining);
            words[word_count] = last_word;
            word_count++;
        }
        
        // 多轮混合处理
        for (size_t round = 0; round < 8; ++round) {
            uint64_t temp_state[4] = {state.state[0], state.state[1], state.state[2], state.state[3]};
            
            for (size_t i = 0; i < word_count; ++i) {
                uint64_t mixed = mix(words[i], state.counter + i, round);
                temp_state[i % 4] ^= mixed;
                temp_state[(i + 1) % 4] += rotate_left(mixed, i + round);
                temp_state[(i + 2) % 4] ^= rotate_right(mixed, i + round + 1);
            }
            
            // 交叉混合
            state.state[0] = mix(temp_state[0], temp_state[1], round);
            state.state[1] = mix(temp_state[1], temp_state[2], round + 1);
            state.state[2] = mix(temp_state[2], temp_state[3], round + 2);
            state.state[3] = mix(temp_state[3], temp_state[0], round + 3);
        }
        
        state.counter += block_size;
    }
    
    // 最终化 - 从内部状态生成8字节哈希
    static Hash64 finalize(const InternalState& state) {
        uint64_t result = 0;
        
        // 多轮最终混合
        for (int round = 0; round < 4; ++round) {
            uint64_t mixed = mix(state.state[round], state.counter, round);
            result ^= mixed;
            result = rotate_left(result, ROTATIONS[round * 4]);
            result += mixed * PRIMES[round + 4];
        }
        
        // 最终非线性变换
        result ^= result >> 33;
        result *= 0xff51afd7ed558ccd;
        result ^= result >> 33;
        result *= 0xc4ceb9fe1a85ec53;
        result ^= result >> 33;
        
        return Hash64(result);
    }
    
    // 处理短输入的特殊优化
    static Hash64 process_short_input(const uint8_t* data, size_t length) {
        InternalState state;
        
        // 对于非常短的输入 (< 64字节)，使用特殊处理
        if (length < 64) {
            // 填充到64字节，使用输入长度作为填充模式的一部分
            uint8_t padded[64] = {0};
            std::memcpy(padded, data, length);
            
            // 使用长度信息进行填充，增加熵
            for (size_t i = length; i < 64; ++i) {
                padded[i] = static_cast<uint8_t>((length * i + 0x9e) & 0xFF);
            }
            
            process_block(state, padded, 64);
        } else {
            // 处理多个64字节块
            size_t full_blocks = length / 64;
            size_t remaining = length % 64;
            
            for (size_t i = 0; i < full_blocks; ++i) {
                process_block(state, data + i * 64, 64);
            }
            
            // 处理剩余数据
            if (remaining > 0) {
                uint8_t last_block[64] = {0};
                std::memcpy(last_block, data + full_blocks * 64, remaining);
                
                // 使用剩余长度和原始长度进行填充
                for (size_t i = remaining; i < 64; ++i) {
                    last_block[i] = static_cast<uint8_t>(((length + i) * 0x37) & 0xFF);
                }
                
                process_block(state, last_block, 64);
            }
        }
        
        // 添加长度信息到最终状态
        state.state[0] ^= length;
        state.state[1] ^= state.counter;
        state.state[2] ^= (length * 0x1234567890abcdef);
        state.state[3] ^= (state.counter * 0xfedcba9876543210);
        
        return finalize(state);
    }

public:
    // 主要哈希函数
    static Hash64 hash(const uint8_t* data, size_t length) {
        if (length > MAX_INPUT_SIZE) {
            // 超过最大长度，返回错误哈希
            return Hash64(0xDEADBEEFDEADBEEF);
        }
        
        return process_short_input(data, length);
    }
    
    static Hash64 hash(const std::string& input) {
        return hash(reinterpret_cast<const uint8_t*>(input.data()), input.length());
    }
    
    // 批量哈希函数
    static std::vector<Hash64> hash_batch(const std::vector<std::string>& inputs) {
        std::vector<Hash64> results;
        results.reserve(inputs.size());
        
        for (const auto& input : inputs) {
            results.push_back(hash(input));
        }
        
        return results;
    }
    
    // 获取算法信息
    static std::string get_info() {
        return "ShortHashOptimized v1.0 - 专为≤1024字节输入优化的8字节哈希算法";
    }
};

// 碰撞检测器
class CollisionDetector {
private:
    std::unordered_map<uint64_t, std::vector<std::string>> hash_map_;
    
public:
    void add(const std::string& input) {
        auto hash = ShortHashOptimized::hash(input);
        hash_map_[hash.value].push_back(input);
    }
    
    struct CollisionInfo {
        Hash64 hash;
        std::vector<std::string> inputs;
    };
    
    std::vector<CollisionInfo> get_collisions() const {
        std::vector<CollisionInfo> collisions;
        
        for (const auto& [hash_value, inputs] : hash_map_) {
            if (inputs.size() > 1) {
                collisions.push_back({Hash64(hash_value), inputs});
            }
        }
        
        return collisions;
    }
    
    struct Stats {
        size_t total_inputs;
        size_t unique_hashes;
        size_t collision_count;
        double collision_rate;
        double load_factor;
    };
    
    Stats get_stats() const {
        Stats stats{};
        stats.total_inputs = 0;
        stats.unique_hashes = hash_map_.size();
        stats.collision_count = 0;
        
        for (const auto& [hash_value, inputs] : hash_map_) {
            stats.total_inputs += inputs.size();
            if (inputs.size() > 1) {
                stats.collision_count++;
            }
        }
        
        stats.collision_rate = stats.unique_hashes > 0 ? 
            static_cast<double>(stats.collision_count) / stats.unique_hashes : 0.0;
        stats.load_factor = static_cast<double>(stats.total_inputs) / stats.unique_hashes;
        
        return stats;
    }
};

} // namespace short_hash

// 为unordered_map提供哈希函数
namespace std {
    template<>
    struct hash<short_hash::Hash64> {
        size_t operator()(const short_hash::Hash64& h) const noexcept {
            return std::hash<uint64_t>{}(h.value);
        }
    };
}

#endif // SHORT_HASH_OPTIMIZED_H