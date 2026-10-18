/*
 * ============================================================================
 *  motion.h —— 步进电机控制（两轴：分选电机 + 开合电机）
 * ============================================================================
 *  功能：控制两台步进电机（均用 A4988 驱动，STEP/DIR 接口）。
 *    - 分选电机（电机1）：旋转到 6 个预设位置之一（360° 均分 6 位置）
 *    - 开合电机（电机2）：开（80°）/ 关（0）两个位置
 *
 *  实现方式：沿用旧程序的 AccelStepper 库，非阻塞（moveTo + run）。
 *           不重新设计算法、不调速度/加速度，只适配新引脚。
 *
 *  依赖：需要安装 AccelStepper 库
 *        Arduino IDE → 工具 → 管理库 → 搜索 AccelStepper → 安装。
 * ============================================================================
 */
#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>
#include <AccelStepper.h>
#include "config.h"

// 初始化两台步进电机（setup 里调用一次）
void motionInit();

// ---- 分选电机（电机1）----
// 旋转到指定位置（0..5，越界自动约束到 0..5）
void sorterMoveToPosition(int pos);

// 分选电机回到原点（位置 0）
void sorterHome();

// 分选电机非阻塞推进（每个主循环周期调用一次）
void sorterRun();

// 分选电机是否正在运动
bool sorterBusy();

// ---- 开合电机（电机2）----
// 打开（到 GATE_OPEN_STEPS 步）
void gateOpen();

// 关闭（回到原点 0 步）
void gateClose();

// 开合电机非阻塞推进（每个主循环周期调用一次）
void gateRun();

// 开合电机是否正在运动
bool gateBusy();

#endif // MOTION_H
