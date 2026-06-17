/**
 * @file    LoginTokenUtil.h
 * @brief   LoginServer 登录票据生成工具
 */

#pragma once

#include <cstddef>

/**
 * @brief 生成 32 字节随机十六进制 token（64 字符 + '\0'）
 * @param out    输出缓冲区，至少 65 字节
 * @param outLen 缓冲区长度
 * @return 成功 true
 */
bool generateLoginToken(char* out, size_t outLen);
