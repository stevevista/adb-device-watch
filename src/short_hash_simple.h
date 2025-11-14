/*
 * 简化版短哈希算法 - 专为≤1024字节输入设计的8字节哈希
 * 使用基本的C++特性，避免复杂的模板和高级特性
 */

#ifndef SHORT_HASH_SIMPLE_H
#define SHORT_HASH_SIMPLE_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// 8字节哈希结果结构体
typedef struct {
    uint64_t value;
} Hash64;

// 哈希函数声明
Hash64 short_hash(const uint8_t* data, size_t length);
Hash64 short_hash_string(const char* str);

// 工具函数声明
char* hash_to_string(Hash64 hash);
void print_hash_info(void);

#endif // SHORT_HASH_SIMPLE_H