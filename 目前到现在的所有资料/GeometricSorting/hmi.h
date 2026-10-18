/*
 * ============================================================================
 *  hmi.h —— 淘晶驰 TJC 串口屏 通信
 * ============================================================================
 *  功能：封装与串口屏的通信，提供 显示 / 状态更新 / 物体编号显示 等函数。
 *
 *  通信：硬件 Serial1（Mega2560 TX=18 -> 屏 RX，RX=19 -> 屏 TX），9600。
 *  指令格式：<控件>.<属性>="值" 后跟结束符 0xFF 0xFF 0xFF。
 *
 *  参考：reference/final（串口屏相关代码）。
 * ============================================================================
 */
#ifndef HMI_H
#define HMI_H

#include <Arduino.h>
#include "config.h"

// 初始化串口屏（setup 调用）
void hmiInit();

// 发送一条原始指令（自动追加结束符 0xFF 0xFF 0xFF）
void hmiSendCommand(const char *cmd);

// 显示文本到文本控件（HMI_TEXT_CTRL.HMI_TEXT_PROP）
void hmiShowText(const char *text);

// 显示识别物体：把数字 ID(0..402) 换算成「颜色编号 + 形状编号」发给串口屏，
// 由串口屏本地把编号映射成文字/图片（不直接发中文，避免乱码）。
void hmiShowObject(int objectId);

// 显示一张图片（自动轮换 p0..p5 图片控件）
void hmiShowPicture(int picId);

// 在数据列表控件里记录/更新一条「名称 + 数量」
void hmiRecordItem(const char *name, int count);

// 更新状态文本（通用）
void hmiUpdateStatus(const char *msg);

#endif // HMI_H
