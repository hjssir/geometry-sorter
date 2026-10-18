/*
 * ============================================================================
 *  conveyor.h —— 直流传送带 + 红外/激光传感器 联动
 * ============================================================================
 *  功能：
 *    - 三级直流传送带（out1 / out2 / out3）：只上电/断电，单向、不可调速。
 *    - 两个红外/激光传感器（NPN 输出，LOW=检测到物体）：带软件消抖 + 边沿检测。
 *
 *  层级约定：
 *    level 1 = out1 = 1 号传送带（最上层）
 *    level 2 = out2 = 2 号传送带
 *    level 3 = out3 = 3 号传送带
 *    物料从 1 → 2 → 3 逐级向下输送。
 *
 *  传感器约定（参考 sketch_oct10a）：
 *    传感器1 对应 out1（1 号传送带末端），传感器2 对应 out2（2 号传送带末端）。
 *    检测到物体（LOW）→ 对应传送带停止。
 * ============================================================================
 */
#ifndef CONVEYOR_H
#define CONVEYOR_H

#include <Arduino.h>
#include "config.h"

#define CONVEYOR_LEVEL_COUNT  3   // 三级传送带

// ---- 传送带 ----
void conveyorInit();                          // 初始化（setup 调用）
void conveyorStart(int level);                // 启动某级（level 1..3）
void conveyorStop(int level);                 // 停止某级
void conveyorStartAll();                      // 三级同时启动
void conveyorStopAll();                       // 三级同时停止
bool conveyorRunning(int level);              // 某级是否运行中

// ---- 传感器 ----
// 传感器编号 1/2（对应 SENSOR1_PIN / SENSOR2_PIN）
void sensorInit();                            // 初始化两个传感器
void sensorUpdate();                          // 采样 + 消抖 + 边沿检测（每循环一次）
bool sensorDetected(int which);               // 某传感器当前是否检测到物体
bool sensorFallingEdge(int which);            // 某传感器是否刚发生「物体出现」下降沿（读后清零）

#endif // CONVEYOR_H
