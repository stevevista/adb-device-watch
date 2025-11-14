/*
 * 简化版短哈希算法实现
 * 专为≤1024字节输入设计的8字节哈希
 */

#include "short_hash_simple.h"

// 质数常量
static const uint64_t PRIMES[8] = {
    0x9e3779b97f4a7c15ULL,  // 黄金比例
    0xc6a4a7935bd1e995ULL,  // 大质数1
    0x165667b19e3779f9ULL,  // 大质数2
    0x85ebca77c2b2ae63ULL,  // 大质数3
    0xa54ff53a5f1d36f1ULL,  // 大质数4
    0x72be5d74f27b8965ULL,  // 大质数5
    0x3c6ef372fe94f82aULL,  // 大质数6
    0x510e527fade682d1ULL   // 大质数7
};

// 旋转常量
static const uint32_t ROTATIONS[16] = {
    13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73
};

// 最大输入长度
#define MAX_INPUT_SIZE 1024

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
    result *= 0xff51afd7ed558ccdULL;  // 最终大质数
    
    return result;
}

// 处理数据块（64字节）
static void process_block(uint64_t state[4], const uint8_t* block, size_t block_size, uint64_t* counter) {
    uint64_t words[8] = {0};
    size_t word_count = (block_size < 64) ? (block_size / 8) : 8;
    
    // 将块转换为64位整数数组
    for (size_t i = 0; i < word_count; ++i) {
        words[i] = 0;
        for (size_t j = 0; j < 8; ++j) {
            words[i] |= ((uint64_t)block[i * 8 + j]) << (j * 8);
        }
    }
    
    // 处理剩余字节
    if (block_size % 8 != 0) {
        size_t remaining = block_size % 8;
        uint64_t last_word = 0;
        for (size_t j = 0; j < remaining; ++j) {
            last_word |= ((uint64_t)block[word_count * 8 + j]) << (j * 8);
        }
        words[word_count] = last_word;
        word_count++;
    }
    
    // 多轮混合处理
    for (size_t round = 0; round < 8; ++round) {
        uint64_t temp_state[4];
        temp_state[0] = state[0];
        temp_state[1] = state[1];
        temp_state[2] = state[2];
        temp_state[3] = state[3];
        
        for (size_t i = 0; i < word_count; ++i) {
            uint64_t mixed = mix(words[i], *counter + i, round);
            temp_state[i % 4] ^= mixed;
            temp_state[(i + 1) % 4] += rotate_left(mixed, i + round);
            temp_state[(i + 2) % 4] ^= rotate_right(mixed, i + round + 1);
        }
        
        // 交叉混合
        state[0] = mix(temp_state[0], temp_state[1], round);
        state[1] = mix(temp_state[1], temp_state[2], round + 1);
        state[2] = mix(temp_state[2], temp_state[3], round + 2);
        state[3] = mix(temp_state[3], temp_state[0], round + 3);
    }
    
    *counter += block_size;
}

// 最终化 - 从内部状态生成8字节哈希
static Hash64 finalize(const uint64_t state[4], uint64_t counter) {
    Hash64 result;
    result.value = 0;
    
    // 多轮最终混合
    for (int round = 0; round < 4; ++round) {
        uint64_t mixed = mix(state[round], counter, round);
        result.value ^= mixed;
        result.value = rotate_left(result.value, ROTATIONS[round * 4]);
        result.value += mixed * PRIMES[round + 4];
    }
    
    // 最终非线性变换
    result.value ^= result.value >> 33;
    result.value *= 0xff51afd7ed558ccdULL;
    result.value ^= result.value >> 33;
    result.value *= 0xc4ceb9fe1a85ec53ULL;
    result.value ^= result.value >> 33;
    
    return result;
}

// 处理短输入的特殊优化
static Hash64 process_short_input(const uint8_t* data, size_t length) {
    uint64_t state[4];
    uint64_t counter = 0;
    
    // 初始化状态
    state[0] = PRIMES[0];
    state[1] = PRIMES[1];
    state[2] = PRIMES[2];
    state[3] = PRIMES[3];
    
    // 对于非常短的输入 (< 64字节)，使用特殊处理
    if (length < 64) {
        // 填充到64字节，使用输入长度作为填充模式的一部分
        uint8_t padded[64];
        memset(padded, 0, 64);
        memcpy(padded, data, length);
        
        // 使用长度信息进行填充，增加熵
        for (size_t i = length; i < 64; ++i) {
            padded[i] = (uint8_t)((length * i + 0x9e) & 0xFF);
        }
        
        process_block(state, padded, 64, &counter);
    } else {
        // 处理多个64字节块
        size_t full_blocks = length / 64;
        size_t remaining = length % 64;
        
        for (size_t i = 0; i < full_blocks; ++i) {
            process_block(state, data + i * 64, 64, &counter);
        }
        
        // 处理剩余数据
        if (remaining > 0) {
            uint8_t last_block[64];
            memset(last_block, 0, 64);
            memcpy(last_block, data + full_blocks * 64, remaining);
            
            // 使用剩余长度和原始长度进行填充
            for (size_t i = remaining; i < 64; ++i) {
                last_block[i] = (uint8_t)(((length + i) * 0x37) & 0xFF);
            }
            
            process_block(state, last_block, 64, &counter);
        }
    }
    
    // 添加长度信息到最终状态
    state[0] ^= length;
    state[1] ^= counter;
    state[2] ^= (length * 0x1234567890abcdefULL);
    state[3] ^= (counter * 0xfedcba9876543210ULL);
    
    return finalize(state, counter);
}

// 主要哈希函数
Hash64 short_hash(const uint8_t* data, size_t length) {
    if (length > MAX_INPUT_SIZE) {
        // 超过最大长度，返回错误哈希
        Hash64 error_hash;
        error_hash.value = 0xDEADBEEFDEADBEEFULL;
        return error_hash;
    }
    
    return process_short_input(data, length);
}

// 字符串哈希函数
Hash64 short_hash_string(const char* str) {
    if (str == NULL) {
        Hash64 null_hash;
        null_hash.value = 0;
        return null_hash;
    }
    
    size_t length = strlen(str);
    return short_hash((const uint8_t*)str, length);
}

// 将哈希转换为字符串
char* hash_to_string(Hash64 hash) {
    static char buffer[17]; // 16个十六进制字符 + 1个null终止符
    snprintf(buffer, sizeof(buffer), "%016llx", (unsigned long long)hash.value);
    return buffer;
}

// 打印算法信息
void print_hash_info(void) {
    printf("ShortHashOptimized v1.0 - 专为≤1024字节输入优化的8字节哈希算法\n");
    printf("最大输入长度: %d 字节\n", MAX_INPUT_SIZE);
    printf("输出长度: 8 字节 (64位)\n");
    printf("算法特性: 多轮混合、非线性变换、雪崩效应\n");
}