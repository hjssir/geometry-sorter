/*
 * ============================================================================
 *  conveyor.cpp —— 直流传送带 + 红外/激光传感器 实现
 * ============================================================================
 *  引脚（config.h）：out1=A1 / out2=A2 / out3=A3（直流电机，继电器/MOS 驱动，
 *                    HIGH=启动）；传感器 SENSOR1_PIN / SENSOR2_PIN（NPN，LOW=检测到）。
 *
 *  参考 sketch_oct10a.ino：
 *    - 直流电机用 pinMode(OUTPUT) + digitalWrite(HIGH/LOW) 通断。
 *    - 传感器用 INPUT_PULLUP，读到 LOW 表示检测到物体，约 200ms 消抖。
 * ============================================================================
 */
#include "conveyor.h"
#include "logger.h"

// ---- 三级传送带运行状态 ----
static bool running[CONVEYOR_LEVEL_COUNT] = {false, false, false};

// ---- 传感器内部状态 ----
struct SensorState {
  uint8_t pin;               // 引脚
  bool    raw;               // 上一次原始采样
  bool    stable;            // 消抖后的稳定状态（true=检测到）
  bool    edge;              // 待消费的下降沿标志
  unsigned long lastChangeMs;// 最近一次原始跳变时刻
};
static SensorState sensors[2];

// ---------------------------------------------------------------------------
// 传送带
// ---------------------------------------------------------------------------
// level(1..3) -> 引脚
static uint8_t levelPin(int level) {
  switch (level) {
    case 1:  return OUT1_PIN;
    case 2:  return OUT2_PIN;
    case 3:  return OUT3_PIN;
    default: return 0xFF;   // 非法
  }
}

// 统一启停
static void applyLevel(int level, bool on) {
  uint8_t p = levelPin(level);
  if (p == 0xFF) {
    LOG_WARN("conveyor 非法层级 %d", level);
    return;
  }
  LOG_INFO("传送带 L%d %s", level, on ? "启动" : "停止");
  if (SIMULATION_MODE) {
    running[level - 1] = on;
    return;
  }
  digitalWrite(p, on ? HIGH : LOW);
  running[level - 1] = on;
}

void conveyorInit() {
  for (int i = 0; i < CONVEYOR_LEVEL_COUNT; i++) running[i] = false;
  if (!SIMULATION_MODE) {
    pinMode(OUT1_PIN, OUTPUT);
    pinMode(OUT2_PIN, OUTPUT);
    pinMode(OUT3_PIN, OUTPUT);
    digitalWrite(OUT1_PIN, LOW);
    digitalWrite(OUT2_PIN, LOW);
    digitalWrite(OUT3_PIN, LOW);
  }
  LOG_INFO("conveyor init ok (三级, 仅通断)");
}

void conveyorStart(int level) { applyLevel(level, true); }
void conveyorStop(int level)  { applyLevel(level, false); }

void conveyorStartAll() {
  for (int i = 1; i <= CONVEYOR_LEVEL_COUNT; i++) conveyorStart(i);
}
void conveyorStopAll() {
  for (int i = 1; i <= CONVEYOR_LEVEL_COUNT; i++) conveyorStop(i);
}

bool conveyorRunning(int level) {
  if (level < 1 || level > CONVEYOR_LEVEL_COUNT) return false;
  return running[level - 1];
}

// ---------------------------------------------------------------------------
// 传感器
// ---------------------------------------------------------------------------
// 读取某传感器原始「是否检测到」状态
static bool readRaw(int which) {
  if (SIMULATION_MODE) return sensors[which].raw;  // 模拟模式保持注入值
  return (digitalRead(sensors[which].pin) == LOW); // 真实：NPN，LOW=检测到
}

void sensorInit() {
  sensors[0].pin = SENSOR1_PIN;
  sensors[1].pin = SENSOR2_PIN;

  for (int i = 0; i < 2; i++) {
    if (!SIMULATION_MODE) {
      pinMode(sensors[i].pin, INPUT_PULLUP);   // NPN 开集输出，需要上拉
    }
    sensors[i].raw  = readRaw(i);
    sensors[i].stable = sensors[i].raw;
    sensors[i].edge = false;
    sensors[i].lastChangeMs = 0;
    LOG_INFO("sensor%d init (pin %d, detected=%d)", i + 1, sensors[i].pin, sensors[i].stable);
  }
}

void sensorUpdate() {
  for (int i = 0; i < 2; i++) {
    bool raw = readRaw(i);
    unsigned long now = millis();

    // 原始信号跳变，记录时刻
    if (raw != sensors[i].raw) {
      sensors[i].raw = raw;
      sensors[i].lastChangeMs = now;
    }

    // 稳定超过消抖时间后才更新稳定状态
    if ((now - sensors[i].lastChangeMs) >= SENSOR_DEBOUNCE_MS && raw != sensors[i].stable) {
      if (raw == true && sensors[i].stable == false) {
        sensors[i].edge = true;   // 未检测 -> 检测：物体出现
      }
      sensors[i].stable = raw;
    }
  }
}

bool sensorDetected(int which) {
  if (which < 1 || which > 2) return false;
  return sensors[which - 1].stable;
}

bool sensorFallingEdge(int which) {
  if (which < 1 || which > 2) return false;
  bool e = sensors[which - 1].edge;
  sensors[which - 1].edge = false;
  return e;
}
