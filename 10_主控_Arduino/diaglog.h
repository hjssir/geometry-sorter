/*
 * ============================================================================
 *  diaglog.h —— 离线诊断日志模块（EEPROM 持久化，无需串口即可运行）
 * ============================================================================
 *  用途：在设备不接串口的离线状态下，把「上电→执行→异常」的关键事件持久化
 *        写入 Mega2560 内置 EEPROM（4KB），事后接一次串口发送 "dump" 即可回读。
 *
 *  记录内容：上电(BOOT)、状态迁移(STATE)、传送带启停(CONV)、分选电机(SORTER)、
 *            开合电机(GATE)、K230 识别(K230)、电源电压(VCC)、循环边界(CYCLE)、
 *            异常复位(RESET)。
 *
 *  关键设计：
 *    - 头部 16 字节：魔数/版本/上电次数/循环计数/写指针/复位前状态。
 *    - 记录区：每条 10 字节（类型+p1+p2+val+时间戳），环形覆盖。
 *    - 记录 DIAG_MAX_CYCLES(5) 个分选循环后自动停止写新记录（boot 计数仍递增）。
 *    - VCC 用内部 1.1V 带隙反测（毫伏），用于诊断电源压降。
 *    - 复位检测：上电时若发现上次运行未到循环边界（非 CYCLE 收尾）则记 RESET。
 *
 *  回读命令（串口 115200，仅事后接电脑时用）：
 *    dump  —— 打印全部日志；  clear —— 清空日志。
 * ============================================================================
 */
#ifndef DIAGLOG_H
#define DIAGLOG_H

#include <Arduino.h>
#include <EEPROM.h>

// ============ 常量 ============
#define DIAG_EEPROM_SIZE   4096        // ATmega2560 内置 EEPROM 4KB
#define DIAG_MAGIC         0x4C47      // 魔数 "LG"
#define DIAG_VERSION       1
#define DIAG_MAX_CYCLES    5           // 记录 N 个分选循环后停止

#define DIAG_HEADER_SIZE   16
#define DIAG_RECORD_SIZE   10
#define DIAG_RECORD_MAX    ((DIAG_EEPROM_SIZE - DIAG_HEADER_SIZE) / DIAG_RECORD_SIZE)  // = 408

// 头部字段偏移
#define H_MAGIC      0
#define H_VERSION    2
#define H_FLAGS      3
#define H_BOOTS      4      // uint16
#define H_CYCLES     6      // uint16
#define H_WRITEIDX   8      // uint16
#define H_LASTSTATE  10     // uint8
// 11..15 保留

#define FLAG_ACTIVE  0x01   // bit0：1=仍在记录，0=已达上限停止

// 记录类型
enum DiagType : uint8_t {
  DG_BOOT   = 0x01,  // p1=boot序号, val=VCC(mV)
  DG_STATE  = 0x02,  // p1=旧状态, p2=新状态
  DG_CONV   = 0x03,  // p1=层级1..3, p2=0停/1启
  DG_SORTER = 0x04,  // p1=位置0..5, val=目标步数
  DG_GATE   = 0x05,  // p1=0开/1关, val=目标步数
  DG_K230   = 0x06,  // p1=0无效/1有效, p2=id低8位, val=置信度*100
  DG_VCC    = 0x07,  // val=VCC(mV)
  DG_CYCLE  = 0x08,  // p1=循环号
  DG_RESET  = 0x09   // p1=复位前状态
};

// ============ RAM 镜像 ============
static bool     _dgActive    = true;
static uint16_t _dgBoots     = 0;
static uint16_t _dgCycles    = 0;
static uint16_t _dgWridx     = 0;
static uint8_t  _dgLastState = 0xFF;

// ============ 底层读写 ============
static uint16_t dgRd16(int a) {
  return (uint16_t)EEPROM.read(a) | ((uint16_t)EEPROM.read(a + 1) << 8);
}
static void dgWr16(int a, uint16_t v) {
  EEPROM.update(a, (uint8_t)(v & 0xFF));
  EEPROM.update(a + 1, (uint8_t)(v >> 8));
}

// 读 VCC（内部 1.1V 带隙反测，单位 mV，约 ±2% 精度，足够判断压降）
static uint16_t diagReadVcc() {
  uint8_t oldMux = ADMUX;
  ADMUX = (1 << REFS0) | 0x1E;          // AVcc 基准 + 1.1V 带隙通道(0x1E)
  delay(2);
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC));
  uint16_t adc = ADC;
  ADMUX = oldMux;
  if (adc == 0) return 0;
  return (uint16_t)(1126400UL / adc);   // Vcc(mV) = 1.1*1024*1000/adc
}

// ============ 写一条记录 ============
static void diagWriteRaw(uint8_t type, uint8_t p1, uint8_t p2, uint16_t val) {
  if (!_dgActive) return;
  uint16_t slot = _dgWridx % DIAG_RECORD_MAX;     // 环形覆盖
  uint32_t off = DIAG_HEADER_SIZE + (uint32_t)slot * DIAG_RECORD_SIZE;
  uint32_t ms = millis();
  EEPROM.update(off + 0, type);
  EEPROM.update(off + 1, p1);
  EEPROM.update(off + 2, p2);
  EEPROM.update(off + 3, (uint8_t)(val & 0xFF));
  EEPROM.update(off + 4, (uint8_t)(val >> 8));
  EEPROM.update(off + 5, (uint8_t)(ms & 0xFF));
  EEPROM.update(off + 6, (uint8_t)((ms >> 8) & 0xFF));
  EEPROM.update(off + 7, (uint8_t)((ms >> 16) & 0xFF));
  EEPROM.update(off + 8, (uint8_t)((ms >> 24) & 0xFF));
  EEPROM.update(off + 9, 0);                       // 保留
  _dgWridx++;
  dgWr16(H_WRITEIDX, _dgWridx);
}

// ============ 对外记录接口 ============
static void diagState(uint8_t oldS, uint8_t newS) {
  _dgLastState = newS;
  EEPROM.update(H_LASTSTATE, newS);
  diagWriteRaw(DG_STATE, oldS, newS, 0);
}
static void diagConv(uint8_t level, bool on) {
  diagWriteRaw(DG_CONV, level, on ? 1 : 0, 0);
}
static void diagSorter(uint8_t pos, uint16_t steps) {
  diagWriteRaw(DG_SORTER, pos, 0, steps);
  diagWriteRaw(DG_VCC, 0, 0, diagReadVcc());       // 电机上电前采样电压
}
static void diagGate(uint8_t isClose, uint16_t steps) {
  diagWriteRaw(DG_GATE, isClose ? 1 : 0, 0, steps);
  diagWriteRaw(DG_VCC, 0, 0, diagReadVcc());
}
static void diagK230(bool valid, uint8_t idLow, uint16_t confPct) {
  diagWriteRaw(DG_K230, valid ? 1 : 0, idLow, confPct);
}
static void diagCycle() {
  _dgCycles++;
  dgWr16(H_CYCLES, _dgCycles);
  diagWriteRaw(DG_CYCLE, (uint8_t)_dgCycles, 0, 0);
  if (_dgCycles >= DIAG_MAX_CYCLES) {              // 达上限 → 停止记录
    _dgActive = false;
    EEPROM.update(H_FLAGS, 0x00);
  }
}

// ============ 初始化（上电调用一次）============
static void diagInit() {
  // 载入头部
  uint16_t magic = dgRd16(H_MAGIC);
  if (magic != DIAG_MAGIC) {
    // 首次使用：初始化
    dgWr16(H_MAGIC, DIAG_MAGIC);
    EEPROM.update(H_VERSION, DIAG_VERSION);
    EEPROM.update(H_FLAGS, FLAG_ACTIVE);
    dgWr16(H_BOOTS, 0);
    dgWr16(H_CYCLES, 0);
    dgWr16(H_WRITEIDX, 0);
    EEPROM.update(H_LASTSTATE, 0xFF);
    _dgBoots = 0; _dgCycles = 0; _dgWridx = 0;
    _dgActive = true; _dgLastState = 0xFF;
  } else {
    _dgBoots     = dgRd16(H_BOOTS);
    _dgCycles    = dgRd16(H_CYCLES);
    _dgWridx     = dgRd16(H_WRITEIDX);
    _dgLastState = EEPROM.read(H_LASTSTATE);
    _dgActive    = (EEPROM.read(H_FLAGS) & FLAG_ACTIVE) != 0;
    if (_dgCycles >= DIAG_MAX_CYCLES) _dgActive = false;

    // 复位检测：上次上电曾运行过（boots>0），且上次最后状态非"空"（非首次）
    if (_dgBoots > 0 && _dgLastState != 0xFF) {
      diagWriteRaw(DG_RESET, _dgLastState, 0, 0);
    }
  }

  // 上电计数 +1，写 BOOT 记录（含 VCC）
  _dgBoots++;
  dgWr16(H_BOOTS, _dgBoots);
  if (_dgActive) {
    diagWriteRaw(DG_BOOT, (uint8_t)(_dgBoots & 0xFF), 0, diagReadVcc());
  }
}

// ============ 回读（dump）与清空（clear）============
static void diagDump() {
  Serial.println(F("===== 诊断日志 Dump ====="));
  Serial.print(F("模块版本 = ")); Serial.println(DIAG_VERSION);
  Serial.print(F("上电次数 = ")); Serial.println(_dgBoots);
  Serial.print(F("完成循环 = ")); Serial.println(_dgCycles);
  Serial.print(F("记录状态 = ")); Serial.println(_dgActive ? F("记录中") : F("已停止(达5循环上限)"));
  Serial.print(F("复位前状态 = ")); Serial.println(_dgLastState);
  Serial.print(F("总记录条数 = ")); Serial.println(_dgWridx);

  uint16_t total = _dgWridx;
  uint16_t start = 0, count = total;
  if (total > DIAG_RECORD_MAX) { start = total % DIAG_RECORD_MAX; count = DIAG_RECORD_MAX; }

  Serial.println(F("--- 记录列表（时间=上电后 ms） ---"));
  for (uint16_t i = 0; i < count; i++) {
    uint16_t slot = (start + i) % DIAG_RECORD_MAX;
    uint32_t off = DIAG_HEADER_SIZE + (uint32_t)slot * DIAG_RECORD_SIZE;
    uint8_t  t  = EEPROM.read(off + 0);
    uint8_t  p1 = EEPROM.read(off + 1);
    uint8_t  p2 = EEPROM.read(off + 2);
    uint16_t v  = (uint16_t)EEPROM.read(off + 3) | ((uint16_t)EEPROM.read(off + 4) << 8);
    uint32_t ms = (uint32_t)EEPROM.read(off + 5)
                | ((uint32_t)EEPROM.read(off + 6) << 8)
                | ((uint32_t)EEPROM.read(off + 7) << 16)
                | ((uint32_t)EEPROM.read(off + 8) << 24);

    Serial.print(ms); Serial.print(F("ms  "));
    switch (t) {
      case DG_BOOT:   Serial.print(F("BOOT #")); Serial.print(p1); Serial.print(F("  VCC=")); Serial.print(v); Serial.println(F("mV")); break;
      case DG_STATE:  Serial.print(F("STATE ")); Serial.print(p1); Serial.print(F(" -> ")); Serial.println(p2); break;
      case DG_CONV:   Serial.print(F("CONV L")); Serial.print(p1); Serial.println(p2 ? F(" 启动") : F(" 停止")); break;
      case DG_SORTER: Serial.print(F("SORTER 位置")); Serial.print(p1); Serial.print(F("  目标")); Serial.print(v); Serial.println(F(" 步")); break;
      case DG_GATE:   Serial.print(F("GATE ")); Serial.print(p1 ? F("关") : F("开")); Serial.print(F("  目标")); Serial.print(v); Serial.println(F(" 步")); break;
      case DG_K230:   Serial.print(F("K230 ")); Serial.print(p1 ? F("有效") : F("无效")); Serial.print(F(" id=")); Serial.print(p2); Serial.print(F(" conf=")); Serial.print(v); Serial.println(F("%")); break;
      case DG_VCC:    Serial.print(F("VCC ")); Serial.print(v); Serial.println(F("mV")); break;
      case DG_CYCLE:  Serial.print(F("CYCLE #")); Serial.println(p1); break;
      case DG_RESET:  Serial.print(F("RESET! 复位前状态=")); Serial.println(p1); break;
      default:        Serial.print(F("UNKNOWN type=")); Serial.println(t); break;
    }
  }
  Serial.println(F("===== Dump 结束 ====="));
}

static void diagClear() {
  for (int i = 0; i < DIAG_EEPROM_SIZE; i++) EEPROM.update(i, 0x00);
  _dgBoots = 0; _dgCycles = 0; _dgWridx = 0;
  _dgActive = true; _dgLastState = 0xFF;
  dgWr16(H_MAGIC, DIAG_MAGIC);
  EEPROM.update(H_VERSION, DIAG_VERSION);
  EEPROM.update(H_FLAGS, FLAG_ACTIVE);
  dgWr16(H_BOOTS, 0);
  dgWr16(H_CYCLES, 0);
  dgWr16(H_WRITEIDX, 0);
  EEPROM.update(H_LASTSTATE, 0xFF);
  Serial.println(F("诊断日志已清空"));
}

#endif // DIAGLOG_H
