/*
 * ============================================================================
 *  hmi.cpp —— 淘晶驰 TJC 串口屏 通信实现
 * ============================================================================
 *  串口：硬件 Serial1（Mega2560 TX=18, RX=19），波特率 9600。
 *
 *  说明：不使用 SoftwareSerial（旧参考代码里的 SoftwareSerial 是给 ESP32
 *        用的，在 Mega2560 上会与硬件 Serial1 引脚 18/19 冲突）。
 * ============================================================================
 */
#include "hmi.h"
#include "logger.h"

// 串口屏接 Serial1
#define HMI_PORT   Serial1

// 图片控件轮换槽位（p0..p5）
static int picSlot = 0;

// 记录列表当前条数（用于 hmiRecordItem 的序号）
static int recordCount = 0;

// ---------------------------------------------------------------------------
// 发送结束符 0xFF 0xFF 0xFF
// ---------------------------------------------------------------------------
static void hmiSendEnd() {
  HMI_PORT.write(0xFF);
  HMI_PORT.write(0xFF);
  HMI_PORT.write(0xFF);
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
void hmiInit() {
  if (!SIMULATION_MODE) {
    HMI_PORT.begin(HMI_BAUD);
  }
  picSlot = 0;
  recordCount = 0;
  LOG_INFO("hmi init (TJC, Serial1 @%ld)", (long)HMI_BAUD);
}

// ---------------------------------------------------------------------------
// 发送指令
// ---------------------------------------------------------------------------
void hmiSendCommand(const char *cmd) {
  if (SIMULATION_MODE) return;   // 模拟模式不真发串口
  HMI_PORT.print(cmd);
  hmiSendEnd();
}

// ---------------------------------------------------------------------------
// 文本 / 编号 / 名称显示
// ---------------------------------------------------------------------------
void hmiShowText(const char *text) {
  LOG_INFO("HMI 文本显示: %s", text);
  if (SIMULATION_MODE) return;

  // 拼指令： t0.txt="<text>"
  HMI_PORT.print(HMI_TEXT_CTRL);
  HMI_PORT.print(".");
  HMI_PORT.print(HMI_TEXT_PROP);
  HMI_PORT.print("=\"");
  HMI_PORT.print(text);
  HMI_PORT.print("\"");
  hmiSendEnd();
}

void hmiShowObject(int objectId) {
  if (objectId < 0 || objectId >= OBJECT_COUNT) {
    LOG_WARN("HMI 非法物体 ID %d，不显示", objectId);
    return;
  }

  // 数学公式法：数字 ID -> 颜色下标 + 形状下标
  int colorIdx = objectId % COLOR_COUNT;   // 0..12
  int shapeIdx = objectId / COLOR_COUNT;   // 0..30
  int colorId = colorIdx + 1;              // 颜色编号 1..13（红=1...粉色=13）
  int shapeId = shapeIdx + 1;              // 形状编号 1..31（半圆=1...斜圆柱体=31）

  LOG_INFO("HMI 显示物体: ID=%d -> 颜色编号=%d 形状编号=%d", objectId, colorId, shapeId);
  if (SIMULATION_MODE) return;

  // 发送颜色编号（数字控件），不直接发中文，避免串口屏乱码
  HMI_PORT.print(HMI_COLOR_CTRL);
  HMI_PORT.print(".");
  HMI_PORT.print(HMI_COLOR_PROP);
  HMI_PORT.print("=");
  HMI_PORT.print(colorId);
  hmiSendEnd();

  // 发送形状编号（数字控件）
  HMI_PORT.print(HMI_SHAPE_CTRL);
  HMI_PORT.print(".");
  HMI_PORT.print(HMI_SHAPE_PROP);
  HMI_PORT.print("=");
  HMI_PORT.print(shapeId);
  hmiSendEnd();
}

// ---------------------------------------------------------------------------
// 图片显示（p0..p5 轮换）
// ---------------------------------------------------------------------------
void hmiShowPicture(int picId) {
  if (picId < 0) return;
  if (picSlot >= HMI_MAX_PICS) {
    LOG_WARN("HMI 图片控件已用满（%d 张）", HMI_MAX_PICS);
    return;
  }

  // 拼指令： p<slot>.pic=<picId>
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "%s%d.pic=%d", HMI_PIC_CTRL_PREFIX, picSlot, picId);
  LOG_INFO("HMI 显示图片: %s", cmd);
  hmiSendCommand(cmd);
  picSlot++;
}

// ---------------------------------------------------------------------------
// 数据列表记录（data0 控件）
// ---------------------------------------------------------------------------
// 数据内容格式："序号^名称^数量"，例如 "1^红球体^3"
// 已存在名称则更新（up），新名称则插入（insert）。
void hmiRecordItem(const char *name, int count) {
  if (name == NULL || name[0] == '\0') return;
  // 这里只做单条插入/更新的简化版；真正的"查重更新"由主程序维护计数后传入。
  char content[40];
  snprintf(content, sizeof(content), "%d^%s^%d", recordCount + 1, name, count);

  char cmd[56];
  snprintf(cmd, sizeof(cmd), "%s.insert(\"%s\")", HMI_DATA_CTRL, content);
  LOG_INFO("HMI 记录: %s", content);
  hmiSendCommand(cmd);
  recordCount++;
}

// ---------------------------------------------------------------------------
// 状态更新
// ---------------------------------------------------------------------------
void hmiUpdateStatus(const char *msg) {
  // 状态文本也走文本控件显示（或仅打日志，视 HMI 工程而定）
  LOG_INFO("HMI 状态: %s", msg);
  hmiShowText(msg);
}
