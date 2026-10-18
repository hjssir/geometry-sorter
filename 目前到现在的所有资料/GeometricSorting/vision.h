/*
 * ============================================================================
 *  vision.h —— 视觉识别（K230，接 Serial2 / 引脚 RX=16 TX=17）
 * ============================================================================
 *  功能：与 K230 视觉模块通信，接收识别结果并解析。
 *
 *  协议：
 *    Arduino -> K230 ：发送 "image"（触发词）触发一次拍照识别
 *    K230 -> Arduino ：支持两种格式，本模块都兼容：
 *       格式1（数字标签）  "<标签> <置信度>\n"   例如 "37 0.95\n"
 *       格式2（名称+编号）  "<名称>,<编号>\n"     例如 "红球体,3\n"
 *       未识别             "No objects were identified."（可能无换行）
 *       置信度不足         什么都不发（由超时处理）
 *
 *  本模块不做任何阻塞等待，由主程序轮询调用 poll() 驱动。
 * ============================================================================
 */
#ifndef VISION_H
#define VISION_H

#include <Arduino.h>
#include "config.h"

// 视觉识别结果
struct VisionResult {
  bool  valid;                  // true = 收到有效结果
  char  tag[TAG_MAX_LEN];       // 标签字符串（数字或名称），valid 时才有意义
  int   id;                     // 解析出的数字 ID（0..402），非数字时为 -1
  float confidence;             // 置信度 0..1
};

// 初始化视觉串口（setup 调用）
void visionInit();

// 向 K230 发送触发词 "image"
void visionRequest();

// 非阻塞收帧：收到一帧完整结果（含"未识别"）返回 true
bool visionPoll();

// 取最近一次结果
VisionResult visionGetResult();

// 模拟模式：设置假标签（对应串口命令 vision N）
void visionSetFakeTag(const char *tag);

// 数学公式法查对照表：id(0..402) -> 颜色名 + 形状名。
// 越界（id<0 或 id>=403）时 color/shape 都置为 "UNKNOWN"。
void getObjectInfo(int id, String &color, String &shape);

#endif // VISION_H
