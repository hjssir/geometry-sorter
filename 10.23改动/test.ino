/*
 * ============================================================================
 *  test.ino —— 模拟 K230 发送图片代码，验证 识别→分桶→HMI显示 全流程
 * ============================================================================
 *  作用：不接真实 K230，用预设代码序列模拟识别结果，验证：
 *     1) 代码 → 物体名称 映射正确（13 色 × 34 形状 = 442 类）
 *     2) 代码 → 分桶 正确（同名物体复用同一桶）
 *     3) 重复物体 → data0 第三列「成功分拣数量」+1
 *     4) 新物体 → 图片只显示一次（p0~p5，一物体一图）
 *
 *  用法：
 *     - 串口监视器 115200 查看验证结果（含预期对照）
 *     - 若接了串口屏（Serial1，115200），指令会同时真正发到屏上
 *     - loop 里可手动输入数字代码（回车）继续模拟 K230
 *
 *  注意：本文件与 AutoMeasureSort.ino 的映射表/分桶/HMI 逻辑完全一致，
 *        仅去掉电机/传感器部分，专注验证识别与 HMI 上报流程。
 * ============================================================================
 */
#include <Arduino.h>
#include <avr/pgmspace.h>

#define HMI Serial1   // HMI 接串口 1（18/19），波特率 115200

// ===== 映射表：13 色 × 34 形状 = 442 类（来源：对照表.xls）=====
const char* COLORS[13] = {"红","橙","黄","绿","青","蓝","紫","黑","白","灰","棕","卡其色","粉色"};
const char* SHAPES[34] = {
  "半圆","方管","胶囊体","六棱台","六棱柱","镂空正方体",
  "平行六面体","球体","三角筒","三棱台","三棱柱","三棱锥",
  "双锥体","1/4圆","四棱台","四棱锥","椭圆","五角管",
  "五棱台","五棱柱","五棱锥","斜三棱柱","圆环","圆台",
  "圆筒","圆柱体","圆锥体","长方体","正八面体","正方体","斜圆柱体",
  "五棱柱管","长方体柱管","六棱柱管"
};

const byte NUM_BUCKETS = 6;      // 6 个桶 = 6 个图片槽位 = data0 的 6 行
bool learned[NUM_BUCKETS];       // 桶是否已占用
int  learnedCode[NUM_BUCKETS];   // 每个桶对应的物料码
byte bucketCount[NUM_BUCKETS];   // 每个桶当前数量

// ---------------------------------------------------------------------------
// 代码 → 物体名称（如 91 → "红球体"）
// AVR 字符串在 flash，用 strcpy_P / strcat_P 读取
// ---------------------------------------------------------------------------
void getObjectName(int code, char* name) {
  if (code < 0 || code >= 442) { strcpy_P(name, PSTR("未知")); return; }
  strcpy_P(name, COLORS[code % 13]);
  strcat_P(name, SHAPES[code / 13]);
}

// 指令结束符 FF FF FF
void sendEnd() {
  HMI.write(0xFF);
  HMI.write(0xFF);
  HMI.write(0xFF);
}

// 发送图片到 p<slot>（仅新物体调用，一物体一图）
void sendPic(int slot, int picId) {
  HMI.print("p"); HMI.print(slot); HMI.print(".pic="); HMI.print(picId); sendEnd();
  Serial.print("    [HMI] p"); Serial.print(slot); Serial.print(".pic="); Serial.println(picId);
}

// 发送 data0 记录 + t0 名称
void sendRecord(int code, int count, int row, bool isNew) {
  char name[48];
  getObjectName(code, name);

  // t0.txt = 名称
  HMI.print("t0.txt=\""); HMI.print(name); HMI.print("\""); sendEnd();

  // data0 记录（序号^名称^数量）
  char content[64];
  snprintf(content, sizeof(content), "%d^%s^%d", row + 1, name, count);
  if (isNew) {
    HMI.print("data0.insert(\""); HMI.print(content); HMI.print("\")"); sendEnd();
    Serial.print("    [HMI] data0.insert(\""); Serial.print(content); Serial.println("\")");
  } else {
    HMI.print("data0.up(\""); HMI.print(content); HMI.print("\","); HMI.print(row); HMI.print(")"); sendEnd();
    Serial.print("    [HMI] data0.up(\""); Serial.print(content); Serial.print("\","); Serial.print(row); Serial.println(")");
  }
  Serial.print("    [HMI] t0.txt=\""); Serial.print(name); Serial.println("\"");
}

// ---------------------------------------------------------------------------
// 分桶：同名复用同一桶（数量+1）；新码占空桶（数量=1，发图）
// 与 AutoMeasureSort.ino 的 codeToBucket 逻辑一致
// ---------------------------------------------------------------------------
void codeToBucket(int code) {
  // 查旧
  for (byte i = 0; i < NUM_BUCKETS; i++)
    if (learned[i] && learnedCode[i] == code) {
      bucketCount[i]++;
      Serial.print("    => 重复！桶"); Serial.print(i + 1); Serial.print(" 数量="); Serial.println(bucketCount[i]);
      sendRecord(code, bucketCount[i], i, false);   // data0.up 数量+1，不发图
      return;
    }
  // 占新
  for (byte i = 0; i < NUM_BUCKETS; i++)
    if (!learned[i]) {
      learned[i] = true; learnedCode[i] = code; bucketCount[i]++;
      Serial.print("    => 新物体！桶"); Serial.print(i + 1); Serial.print(" 数量="); Serial.println(bucketCount[i]);
      sendPic(i, code);                              // 发图（只此一次）
      sendRecord(code, bucketCount[i], i, true);     // data0.insert
      return;
    }
  Serial.println("    => 六个桶已满，忽略");
}

// 模拟一次 K230 识别
void testCode(int code) {
  char name[48];
  getObjectName(code, name);
  Serial.print("\n[K230] 代码 "); Serial.print(code); Serial.print(" → "); Serial.println(name);
  codeToBucket(code);
}

// ===========================================================================
void setup() {
  Serial.begin(115200);
  HMI.begin(115200);
  delay(100);
  for (byte i = 0; i < NUM_BUCKETS; i++) { learned[i] = false; learnedCode[i] = -1; bucketCount[i] = 0; }

  Serial.println("==============================================");
  Serial.println(" test.ino：模拟 K230 图片代码，验证流程");
  Serial.println("==============================================");

  // 测试序列：覆盖 新物体 / 重复 / 不同物体
  int seq[] = {91, 78, 91, 403, 78, 91};
  int n = sizeof(seq) / sizeof(seq[0]);
  for (int k = 0; k < n; k++) testCode(seq[k]);

  // 结果核对
  Serial.println("\n============== 结果核对 ==============");
  Serial.println("预期（应与你看到的一致）：");
  Serial.println("  91 红球体        -> 桶1 数量1（首次）");
  Serial.println("  78 红平行六面体  -> 桶2 数量1（首次）");
  Serial.println("  91 红球体        -> 桶1 数量2（重复+1）");
  Serial.println("  403 红五棱柱管   -> 桶3 数量1（首次）");
  Serial.println("  78 红平行六面体  -> 桶2 数量2（重复+1）");
  Serial.println("  91 红球体        -> 桶1 数量3（重复+1）");
  Serial.println("\n（以上桶号按 1 起始显示；data0 行号从 0 起，序号=行号+1）");
  Serial.println(" 串口监视器输入数字代码（回车）可继续模拟 K230。");
}

void loop() {
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() > 0) {
      long code = s.toInt();
      testCode((int)code);
    }
  }
}
