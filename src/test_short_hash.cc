#include "short_hash_optimized.h"
#include <iostream>
#include <random>
#include <iomanip>
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>

using namespace short_hash;

// 生成随机字符串
std::string generate_random_string(size_t length) {
    static const char charset[] = 
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

// 生成测试数据
std::vector<std::string> generate_test_data(size_t count, size_t max_length = 1024) {
    std::vector<std::string> data;
    data.reserve(count);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> length_dis(1, max_length);
    
    for (size_t i = 0; i < count; ++i) {
        size_t length = length_dis(gen);
        data.push_back(generate_random_string(length));
    }
    
    return data;
}

// 测试基本功能
void test_basic_functionality() {
    std::cout << "=== 基本功能测试 ===" << std::endl;
    
    // 测试空字符串
    auto hash1 = ShortHashOptimized::hash("");
    std::cout << "空字符串哈希: " << hash1.to_string() << std::endl;
    
    // 测试相同输入
    auto hash2 = ShortHashOptimized::hash("hello world");
    auto hash3 = ShortHashOptimized::hash("hello world");
    std::cout << "相同输入一致性: " << (hash2 == hash3 ? "通过" : "失败") << std::endl;
    
    // 测试不同输入
    auto hash4 = ShortHashOptimized::hash("hello world1");
    auto hash5 = ShortHashOptimized::hash("hello world2");
    std::cout << "不同输入区分性: " << ((hash2 != hash4 && hash2 != hash5 && hash4 != hash5) ? "通过" : "失败") << std::endl;
    
    // 测试最大长度
    std::string max_length_input(1024, 'a');
    auto hash6 = ShortHashOptimized::hash(max_length_input);
    std::cout << "最大长度输入哈希: " << hash6.to_string() << std::endl;
    
    std::cout << std::endl;
}

// 测试碰撞概率
void test_collision_probability() {
    std::cout << "=== 碰撞概率测试 ===" << std::endl;
    
    const size_t test_sizes[] = {1000, 5000, 10000, 50000, 100000};
    
    for (size_t test_size : test_sizes) {
        std::cout << "测试规模: " << test_size << " 个输入" << std::endl;
        
        auto test_data = generate_test_data(test_size);
        CollisionDetector detector;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (const auto& input : test_data) {
            detector.add(input);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        auto stats = detector.get_stats();
        auto collisions = detector.get_collisions();
        
        std::cout << "  处理时间: " << duration.count() << "ms" << std::endl;
        std::cout << "  碰撞数量: " << stats.collision_count << std::endl;
        std::cout << "  碰撞率: " << std::fixed << std::setprecision(6) 
                  << (stats.collision_rate * 100) << "%" << std::endl;
        std::cout << "  负载因子: " << std::fixed << std::setprecision(2) 
                  << stats.load_factor << std::endl;
        
        // 显示前几个碰撞（如果有的话）
        if (!collisions.empty()) {
            std::cout << "  前3个碰撞示例:" << std::endl;
            for (size_t i = 0; i < std::min(size_t(3), collisions.size()); ++i) {
                const auto& collision = collisions[i];
                std::cout << "    哈希值: " << collision.hash.to_string() << std::endl;
                std::cout << "    输入数量: " << collision.inputs.size() << std::endl;
                std::cout << "    输入示例: " << collision.inputs[0].substr(0, 50) << "..." << std::endl;
            }
        }
        
        std::cout << std::endl;
    }
}

// 测试性能
void test_performance() {
    std::cout << "=== 性能测试 ===" << std::endl;
    
    const size_t iterations = 1000000;
    const size_t data_size = 512; // 中等长度
    
    // 生成测试数据
    std::vector<std::string> test_data;
    test_data.reserve(iterations / 100); // 减少内存使用
    for (size_t i = 0; i < iterations / 100; ++i) {
        test_data.push_back(generate_random_string(data_size));
    }
    
    std::cout << "数据规模: " << iterations << " 次哈希运算" << std::endl;
    std::cout << "输入长度: " << data_size << " 字节" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < iterations; ++i) {
        ShortHashOptimized::hash(test_data[i % test_data.size()]);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double ops_per_second = (iterations * 1000000.0) / duration.count();
    double avg_time_us = static_cast<double>(duration.count()) / iterations;
    
    std::cout << "总耗时: " << duration.count() << " 微秒" << std::endl;
    std::cout << "平均每次哈希: " << std::fixed << std::setprecision(3) 
              << avg_time_us << " 微秒" << std::endl;
    std::cout << "哈希速度: " << std::fixed << std::setprecision(0) 
              << ops_per_second << " 次/秒" << std::endl;
    
    std::cout << std::endl;
}

// 测试分布均匀性
void test_distribution_uniformity() {
    std::cout << "=== 分布均匀性测试 ===" << std::endl;
    
    const size_t test_count = 100000;
    const size_t bucket_count = 256; // 分成256个桶
    
    std::vector<size_t> buckets(bucket_count, 0);
    auto test_data = generate_test_data(test_count);
    
    for (const auto& input : test_data) {
        auto hash = ShortHashOptimized::hash(input);
        size_t bucket = (hash.value >> 56) & 0xFF; // 取最高8位
        buckets[bucket]++;
    }
    
    double expected = static_cast<double>(test_count) / bucket_count;
    double max_deviation = 0;
    double total_deviation = 0;
    
    for (size_t i = 0; i < bucket_count; ++i) {
        double deviation = std::abs(static_cast<double>(buckets[i]) - expected);
        max_deviation = std::max(max_deviation, deviation);
        total_deviation += deviation;
    }
    
    double avg_deviation = total_deviation / bucket_count;
    double relative_deviation = (max_deviation / expected) * 100;
    double avg_relative_deviation = (avg_deviation / expected) * 100;
    
    std::cout << "测试数量: " << test_count << std::endl;
    std::cout << "桶数量: " << bucket_count << std::endl;
    std::cout << "期望每桶: " << std::fixed << std::setprecision(2) << expected << std::endl;
    std::cout << "最大偏差: " << std::fixed << std::setprecision(2) << max_deviation << std::endl;
    std::cout << "平均偏差: " << std::fixed << std::setprecision(2) << avg_deviation << std::endl;
    std::cout << "最大相对偏差: " << std::fixed << std::setprecision(2) << relative_deviation << "%" << std::endl;
    std::cout << "平均相对偏差: " << std::fixed << std::setprecision(2) << avg_relative_deviation << "%" << std::endl;
    
    // 显示前10个和后10个桶的计数
    std::cout << "前10个桶计数: ";
    for (size_t i = 0; i < 10; ++i) {
        std::cout << buckets[i] << " ";
    }
    std::cout << std::endl;
    
    std::cout << "后10个桶计数: ";
    for (size_t i = bucket_count - 10; i < bucket_count; ++i) {
        std::cout << buckets[i] << " ";
    }
    std::cout << std::endl;
    
    std::cout << std::endl;
}

// 测试雪崩效应
void test_avalanche_effect() {
    std::cout << "=== 雪崩效应测试 ===" << std::endl;
    
    const size_t test_count = 1000;
    const size_t bit_changes = 64;
    
    std::vector<double> bit_change_rates;
    bit_change_rates.reserve(test_count);
    
    for (size_t test = 0; test < test_count; ++test) {
        // 生成随机输入
        std::string input = generate_random_string(100);
        auto original_hash = ShortHashOptimized::hash(input);
        
        size_t total_bit_changes = 0;
        
        // 改变输入的每一位，观察输出变化
        for (size_t bit_pos = 0; bit_pos < 8; ++bit_pos) { // 只改变一个字节
            std::string modified_input = input;
            modified_input[0] ^= (1 << bit_pos); // 翻转第bit_pos位
            
            auto modified_hash = ShortHashOptimized::hash(modified_input);
            
            // 计算输出中变化的位数
            uint64_t diff = original_hash.value ^ modified_hash.value;
            size_t changed_bits = 0;
            for (size_t i = 0; i < 64; ++i) {
                if (diff & (1ULL << i)) {
                    changed_bits++;
                }
            }
            
            total_bit_changes += changed_bits;
        }
        
        double avg_bit_change_rate = static_cast<double>(total_bit_changes) / (8 * 64);
        bit_change_rates.push_back(avg_bit_change_rate);
    }
    
    double overall_avg = std::accumulate(bit_change_rates.begin(), bit_change_rates.end(), 0.0) / test_count;
    
    std::cout << "测试数量: " << test_count << std::endl;
    std::cout << "平均位变化率: " << std::fixed << std::setprecision(2) 
              << (overall_avg * 100) << "%" << std::endl;
    std::cout << "理想值: 50%" << std::endl;
    std::cout << "偏差: " << std::fixed << std::setprecision(2) 
              << (std::abs(overall_avg - 0.5) * 100) << "%" << std::endl;
    
    std::cout << std::endl;
}

int main() {
    std::cout << "短哈希算法测试程序" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << ShortHashOptimized::get_info() << std::endl;
    std::cout << std::endl;
    
    test_basic_functionality();
    test_collision_probability();
    test_performance();
    test_distribution_uniformity();
    test_avalanche_effect();
    
    std::cout << "测试完成！" << std::endl;
    
    return 0;
}