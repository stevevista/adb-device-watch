/*
 * 简化版短哈希算法测试程序
 */

#include "short_hash_simple.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 生成随机字符串
char* generate_random_string(size_t length) {
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static int initialized = 0;
    
    if (!initialized) {
        srand((unsigned int)time(NULL));
        initialized = 1;
    }
    
    char* result = (char*)malloc(length + 1);
    if (result == NULL) return NULL;
    
    for (size_t i = 0; i < length; ++i) {
        result[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    result[length] = '\0';
    
    return result;
}

// 测试基本功能
void test_basic_functionality() {
    printf("=== 基本功能测试 ===\n");
    
    // 测试空字符串
    Hash64 hash1 = short_hash_string("");
    printf("空字符串哈希: %s\n", hash_to_string(hash1));
    
    // 测试相同输入
    Hash64 hash2 = short_hash_string("hello world");
    Hash64 hash3 = short_hash_string("hello world");
    printf("相同输入一致性: %s\n", (hash2.value == hash3.value) ? "通过" : "失败");
    
    // 测试不同输入
    Hash64 hash4 = short_hash_string("hello world1");
    Hash64 hash5 = short_hash_string("hello world2");
    int different1 = (hash2.value != hash4.value) && (hash2.value != hash5.value) && (hash4.value != hash5.value);
    printf("不同输入区分性: %s\n", different1 ? "通过" : "失败");
    
    // 测试最大长度
    char* max_length_input = (char*)malloc(1025);
    if (max_length_input) {
        memset(max_length_input, 'a', 1024);
        max_length_input[1024] = '\0';
        Hash64 hash6 = short_hash_string(max_length_input);
        printf("最大长度输入哈希: %s\n", hash_to_string(hash6));
        free(max_length_input);
    }
    
    printf("\n");
}

// 测试碰撞概率（简化版）
void test_collision_probability() {
    printf("=== 碰撞概率测试 ===\n");
    
    const size_t test_sizes[] = {1000, 5000, 10000};
    
    for (size_t t = 0; t < sizeof(test_sizes)/sizeof(test_sizes[0]); ++t) {
        size_t test_size = test_sizes[t];
        printf("测试规模: %zu 个输入\n", test_size);
        
        clock_t start = clock();
        
        // 使用简单的哈希表来检测碰撞
        uint64_t* hashes = (uint64_t*)malloc(test_size * sizeof(uint64_t));
        size_t collision_count = 0;
        
        for (size_t i = 0; i < test_size; ++i) {
            char* random_str = generate_random_string(100);
            Hash64 hash = short_hash_string(random_str);
            hashes[i] = hash.value;
            free(random_str);
        }
        
        // 检测碰撞（简单排序法）
        for (size_t i = 0; i < test_size; ++i) {
            for (size_t j = i + 1; j < test_size; ++j) {
                if (hashes[i] == hashes[j]) {
                    collision_count++;
                    break;
                }
            }
        }
        
        clock_t end = clock();
        double duration = ((double)(end - start)) / CLOCKS_PER_SEC * 1000;
        
        double collision_rate = (double)collision_count / test_size * 100;
        
        printf("  处理时间: %.2f ms\n", duration);
        printf("  碰撞数量: %zu\n", collision_count);
        printf("  碰撞率: %.6f%%\n", collision_rate);
        
        free(hashes);
        printf("\n");
    }
}

// 测试性能
void test_performance() {
    printf("=== 性能测试 ===\n");
    
    const size_t iterations = 100000;
    const size_t data_size = 512; // 中等长度
    
    printf("数据规模: %zu 次哈希运算\n", iterations);
    printf("输入长度: %zu 字节\n", data_size);
    
    clock_t start = clock();
    
    for (size_t i = 0; i < iterations; ++i) {
        char* random_str = generate_random_string(data_size);
        short_hash_string(random_str);
        free(random_str);
    }
    
    clock_t end = clock();
    double duration = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    double ops_per_second = iterations / duration;
    double avg_time_us = (duration * 1000000) / iterations;
    
    printf("总耗时: %.3f 秒\n", duration);
    printf("平均每次哈希: %.3f 微秒\n", avg_time_us);
    printf("哈希速度: %.0f 次/秒\n", ops_per_second);
    
    printf("\n");
}

// 测试分布均匀性（简化版）
void test_distribution_uniformity() {
    printf("=== 分布均匀性测试 ===\n");
    
    const size_t test_count = 50000;
    const size_t bucket_count = 256; // 分成256个桶
    
    size_t* buckets = (size_t*)calloc(bucket_count, sizeof(size_t));
    if (buckets == NULL) {
        printf("内存分配失败\n");
        return;
    }
    
    for (size_t i = 0; i < test_count; ++i) {
        char* random_str = generate_random_string(100);
        Hash64 hash = short_hash_string(random_str);
        size_t bucket = (hash.value >> 56) & 0xFF; // 取最高8位
        buckets[bucket]++;
        free(random_str);
    }
    
    double expected = (double)test_count / bucket_count;
    double max_deviation = 0;
    double total_deviation = 0;
    
    for (size_t i = 0; i < bucket_count; ++i) {
        double deviation = (double)buckets[i] > expected ? 
                          (double)buckets[i] - expected : expected - (double)buckets[i];
        if (deviation > max_deviation) max_deviation = deviation;
        total_deviation += deviation;
    }
    
    double avg_deviation = total_deviation / bucket_count;
    double relative_deviation = (max_deviation / expected) * 100;
    double avg_relative_deviation = (avg_deviation / expected) * 100;
    
    printf("测试数量: %zu\n", test_count);
    printf("桶数量: %zu\n", bucket_count);
    printf("期望每桶: %.2f\n", expected);
    printf("最大偏差: %.2f\n", max_deviation);
    printf("平均偏差: %.2f\n", avg_deviation);
    printf("最大相对偏差: %.2f%%\n", relative_deviation);
    printf("平均相对偏差: %.2f%%\n", avg_relative_deviation);
    
    // 显示前几个和后几个桶的计数
    printf("前5个桶计数: ");
    for (size_t i = 0; i < 5; ++i) {
        printf("%zu ", buckets[i]);
    }
    printf("\n");
    
    printf("后5个桶计数: ");
    for (size_t i = bucket_count - 5; i < bucket_count; ++i) {
        printf("%zu ", buckets[i]);
    }
    printf("\n");
    
    free(buckets);
    printf("\n");
}

int main() {
    printf("短哈希算法测试程序\n");
    printf("==================\n");
    print_hash_info();
    printf("\n");
    
    test_basic_functionality();
    test_collision_probability();
    test_performance();
    test_distribution_uniformity();
    
    printf("测试完成！\n");
    
    return 0;
}