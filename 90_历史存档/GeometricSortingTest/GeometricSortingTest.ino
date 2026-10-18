/*
 * ============================================================================
 *  GeometricSortingTest.ino —— 几何体分选测试程序（方案 B，上电自跑）
 * ============================================================================
 *  无需电脑、无需传感器，上电自动循环运行。
 *
 *  完整流程（一个周期）：
 *    1) 上电   ：等待 K230 启动（约 10s）→ 探测 K230；未接入/无响应 → 停机(HALT)。
 *    2) 送料   ：单独启动三级传送带（不启动其它执行机构）。
 *    3) 识别   ：循环向 K230 发送 "image" 轮询；收到有效标签 → 停三级传送带。
 *    4) 显示   ：HMI 显示识别结果（t1=颜色编号，t2=形状编号）。
 *    5) 分选   ：分选电机转随机位置(0..5) → 开合电机打开放料 → 停留 → 关闭 + 复位。
 *    6) 循环   ：重启三级传送带，进入下一轮（无终止条件）。
 *
 *  关键决策（2026-10-16 已确认）：
 *    - 电机：张大头 ZDT 闭环步进电机，脉冲控制(PUL/DIR/ENA)，16 细分 = 3200 脉冲/圈。
 *      分选 X：STEP=10 DIR=26；开合 Y：STEP=11 DIR=28；使能 EN=24（低有效）。
 *    - 通信：K230 走 Serial2(16/17, 115200)，触发词 "image"；
 *            K230 回 "<标签> <置信度>\n" 或 "No objects were identified."（无换行）。
 *            HMI 走 Serial1(18/19, 9600)，TJC 指令 <控件>.<属性>=值 + 0xFF 0xFF 0xFF。
 *    - HMI 控件名与屏工程(ji.HMI)一致：t0(文本)/t1(颜色编号)/t2(形状编号)。
 *    - 分选位置：随机 6 选 1，暂不做「类别→盒位」固定映射。
 *
 *  依赖：AccelStepper 库。
 * ============================================================================
 */

#include <AccelStepper.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "diaglog.h"

// ============================================================================
// 一、工作模式（0=真实硬件；1=模拟：不驱动真实 IO，视觉返回假结果，用于逻辑预演）
// ============================================================================
#define SIMULATION_MODE 0

// ============================================================================
// 二、串口分配（Arduino Mega2560）
// ============================================================================
#define SERIAL_BAUD  115200   // Serial(USB) 调试日志
#define HMI_BAUD     9600     // Serial1(18/19) TJC 串口屏
#define VISION_BAUD  115200   // Serial2(16/17) K230 视觉

// ============================================================================
// 三、引脚定义
// ============================================================================
#define OUT1_PIN A1          // 1 号传送带（直流，HIGH=启动）
#define OUT2_PIN A2          // 2 号传送带
#define OUT3_PIN A3          // 3 号传送带
#define X_STEP_PIN 10        // 分选电机 脉冲 PUL
#define X_DIR_PIN  26        // 分选电机 方向 DIR
#define Y_STEP_PIN 11        // 开合电机 脉冲 PUL
#define Y_DIR_PIN  28        // 开合电机 方向 DIR
#define MOTOR_EN_PIN 24      // 电机使能 ENA（低有效，两轴共用）

// ============================================================================
// 四、电机参数（16 细分 = 3200 脉冲/圈，已上机实测确认）
// ============================================================================
#define MOTOR_INTERFACE_TYPE 1   // 1 = STEP/DIR（脉冲+方向）

const long  SORTER_STEPS_PER_REV  = 3200;  // 分选电机每圈脉冲
const int   SORTER_POSITION_COUNT = 6;     // 6 个分选位置（=6 个盒）
const float SORTER_SPEED          = 300;   // 步/秒
const float SORTER_ACCEL          = 300;

const long  GATE_STEPS_PER_REV  = 3200;    // 开合电机每圈脉冲
const float GATE_OPEN_DEG       = 80.0;    // 打开角度
const long  GATE_OPEN_STEPS     = (long)(GATE_OPEN_DEG / 360.0 * GATE_STEPS_PER_REV + 0.5); // ≈711
const float GATE_SPEED          = 400;
const float GATE_ACCEL          = 400;

const unsigned int MIN_PULSE_US = 20;      // 闭环驱动光耦输入最小脉冲宽度(微秒)

// ============================================================================
// 五、时序参数（毫秒）
// ============================================================================
const unsigned long K230_BOOT_WAIT_MS = 10000; // 上电后等 K230 启动
const unsigned long VISION_TIMEOUT_MS  = 3000;  // 发 image 后无响应超时
const unsigned long POLL_GAP_MS        = 300;   // 收到 "No objects" 后重新触发间隔
const unsigned long QUIET_MS           = 20;    // 收帧静默期（用于无换行的 No objects）
const unsigned long RELEASE_DELAY_MS   = 1000;  // 开合打开后落料停留

// ============================================================================
// 六、K230 / HMI 协议常量
// ============================================================================
const char* VISION_TRIGGER = "image";
#define VISION_FRAME_MAX 48
#define TAG_MAX_LEN      16

#define HMI_TEXT_CTRL  "t0"   // 文本控件
#define HMI_COLOR_CTRL "t1"   // 颜色编号（数字控件）
#define HMI_SHAPE_CTRL "t2"   // 形状编号（数字控件）

// ============================================================================
// 七、对照表（颜色 13 × 形状 31 = 403，数学公式法，仅用于日志打印可读名称）
// ============================================================================
static const int   COLOR_COUNT  = 13;
static const int   SHAPE_COUNT  = 31;
static const int   OBJECT_COUNT = COLOR_COUNT * SHAPE_COUNT;  // 403
static const char* const COLORS[COLOR_COUNT] = {
  "红","橙","黄","绿","青","蓝","紫","黑","白","灰","棕","卡其色","粉色"
};
static const char* const SHAPES[SHAPE_COUNT] = {
  "半圆","方管","胶囊体","六棱台","六棱柱","镂空正方体","平行六面体","球体",
  "三角筒","三棱台","三棱柱","三棱锥","双锥体","1/4圆","四棱台","四棱锥",
  "椭圆","五角管","五棱台","五棱柱","五棱锥","斜三棱柱","圆环","圆台",
  "圆筒","圆柱体","圆锥体","长方体","正八面体","正方体","斜圆柱体"
};

// ============================================================================
// 八、对象与状态
// ============================================================================
AccelStepper sorter(MOTOR_INTERFACE_TYPE, X_STEP_PIN, X_DIR_PIN);
AccelStepper gate(MOTOR_INTERFACE_TYPE, Y_STEP_PIN, Y_DIR_PIN);

enum State {
  BOOT,      // 等待 K230 启动
  PROBE,     // 探测 K230 是否接入
  FEED,      // 启动三级传送带
  VISION,    // 轮询 K230 识别结果
  SHOW,   // HMI 显示 + 选定随机分选位置
  SORT,      // 分选电机到位
  RELEASE,   // 开合电机打开放料
  DELAY,     // 落料停留
  RESET,     // 开合关闭 + 分选复位 + 重启传送带
  HALT       // K230 未接入，停机
};
State state = BOOT;
bool  entered = true;
unsigned long stateStartMs = 0;
unsigned long delayStartMs = 0;

// 视觉结果
static bool   vValid = false;
static char   vLabel[TAG_MAX_LEN];
static int    vId    = -1;
static float  vConf  = 0.0f;
static bool   vFakeReady = false;

// 本次分选随机位置
static int targetPos = 0;

// ============================================================================
// 九、日志（走 USB 调试串口）
// ============================================================================
static void logMsg(const char* level, const char* fmt, ...) {
  char buf[120];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print("["); Serial.print(level); Serial.print("] ");
  Serial.print(millis()); Serial.print("ms ");
  Serial.println(buf);
}
#define LOG_INFO(...)  logMsg("INFO",  __VA_ARGS__)
#define LOG_WARN(...)  logMsg("WARN",  __VA_ARGS__)
#define LOG_ERROR(...) logMsg("ERROR", __VA_ARGS__)

// ============================================================================
// 十、传送带（三级直流，仅通断）
// ============================================================================
static bool running[3] = {false, false, false};

static uint8_t levelPin(int level) {
  if (level == 1) return OUT1_PIN;
  if (level == 2) return OUT2_PIN;
  if (level == 3) return OUT3_PIN;
  return 0xFF;
}
static void conveyorStart(int level) {
  uint8_t p = levelPin(level);
  if (p == 0xFF) return;
  if (!SIMULATION_MODE) digitalWrite(p, HIGH);
  running[level - 1] = true;
  LOG_INFO("传送带 L%d 启动", level);
  diagConv(level, true);
}
static void conveyorStop(int level) {
  uint8_t p = levelPin(level);
  if (p == 0xFF) return;
  if (!SIMULATION_MODE) digitalWrite(p, LOW);
  running[level - 1] = false;
  LOG_INFO("传送带 L%d 停止", level);
  diagConv(level, false);
}
static void conveyorStartAll() { for (int i = 1; i <= 3; i++) conveyorStart(i); }
static void conveyorStopAll()  { for (int i = 1; i <= 3; i++) conveyorStop(i); }

// ============================================================================
// 十一、电机（分选 / 开合）
// ============================================================================
static long sorterPosToSteps(int pos) {
  return (long)pos * SORTER_STEPS_PER_REV / SORTER_POSITION_COUNT;
}
static void sorterMoveTo(int pos) {
  long steps = sorterPosToSteps(pos);
  LOG_INFO("分选电机 -> 位置 %d（%ld 脉冲）", pos, steps);
  diagSorter(pos, (uint16_t)steps);
  if (SIMULATION_MODE) return;
  sorter.moveTo(steps);
}
static void sorterHome() {
  if (SIMULATION_MODE) return;
  sorter.moveTo(0);
}
static bool sorterBusy() {
  if (SIMULATION_MODE) return false;
  return sorter.distanceToGo() != 0;
}
static void gateOpen() {
  LOG_INFO("开合电机 打开（%ld 脉冲）", (long)GATE_OPEN_STEPS);
  diagGate(0, (uint16_t)GATE_OPEN_STEPS);
  if (SIMULATION_MODE) return;
  gate.moveTo(GATE_OPEN_STEPS);
}
static void gateClose() {
  diagGate(1, 0);
  if (SIMULATION_MODE) return;
  gate.moveTo(0);
}
static bool gateBusy() {
  if (SIMULATION_MODE) return false;
  return gate.distanceToGo() != 0;
}
static void motorEnable(bool en) {
  if (SIMULATION_MODE) return;
  digitalWrite(MOTOR_EN_PIN, en ? LOW : HIGH);   // 低有效
}

// ============================================================================
// 十二、K230 视觉（Serial2，非阻塞收帧）
// ============================================================================
static char   vbuf[VISION_FRAME_MAX];
static uint8_t vlen = 0;
static unsigned long lastByteMs = 0;

static void visionRequest() {
  vlen = 0;
  vbuf[0] = '\0';
  lastByteMs = millis();
  if (SIMULATION_MODE) {
    vValid = true;
    strcpy(vLabel, "0");
    vId = 0;
    vConf = 0.95f;
    vFakeReady = true;
    return;
  }
  while (Serial2.available() > 0) Serial2.read();   // 清空残留旧帧，避免上一轮结果串扰
  Serial2.print(VISION_TRIGGER);
}

static int tagToId(const char* s) {
  const char* num = strchr(s, ',');
  num = num ? (num + 1) : s;
  while (*num == ' ') num++;
  if (*num == '\0') return -1;
  for (const char* q = num; *q != '\0'; q++) {
    if (*q < '0' || *q > '9') return -1;
  }
  return atoi(num);
}

static int finishFrame() {
  int n = strlen(vbuf);
  while (n > 0 && (vbuf[n-1] == '\r' || vbuf[n-1] == '\n' || vbuf[n-1] == ' ')) vbuf[--n] = '\0';
  char* p = vbuf;
  while (*p == ' ') p++;
  if (*p == '\0') { vlen = 0; vbuf[0] = '\0'; return -1; }
  if (strncmp(p, "No objects", 10) == 0) { vlen = 0; vbuf[0] = '\0'; vValid = false; return 0; }

  vConf = 1.0f;
  char* comma = strchr(p, ',');
  if (comma != NULL) {
    strncpy(vLabel, p, TAG_MAX_LEN - 1);
    vLabel[TAG_MAX_LEN - 1] = '\0';
    vId = tagToId(vLabel);
    vValid = true;
  } else {
    char* sp = strchr(p, ' ');
    if (sp != NULL) {
      *sp = '\0';
      const char* c = sp + 1;
      while (*c == ' ') c++;
      if (*c != '\0') vConf = atof(c);
    }
    strncpy(vLabel, p, TAG_MAX_LEN - 1);
    vLabel[TAG_MAX_LEN - 1] = '\0';
    vId = tagToId(vLabel);
    vValid = true;
  }
  vlen = 0;
  vbuf[0] = '\0';
  return 1;
}

static int visionPoll() {
  if (SIMULATION_MODE) {
    if (vFakeReady) { vFakeReady = false; return 1; }
    return -1;
  }
  bool gotByte = false;
  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();
    gotByte = true;
    if (vlen < VISION_FRAME_MAX - 1) { vbuf[vlen++] = c; vbuf[vlen] = '\0'; }
    lastByteMs = millis();
    if (c == '\n') return finishFrame();
  }
  if (vlen > 0 && !gotByte && (millis() - lastByteMs) >= QUIET_MS) return finishFrame();
  return -1;
}

// ============================================================================
// 十三、HMI（Serial1，TJC 指令）
// ============================================================================
static void hmiSend(const char* cmd) {
  if (SIMULATION_MODE) return;
  Serial1.print(cmd);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
}
static void hmiShowText(const char* ctrl, const char* text) {
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", ctrl, text);
  hmiSend(cmd);
}
static void hmiShowNum(const char* ctrl, int v) {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "%s.val=%d", ctrl, v);
  hmiSend(cmd);
}

// ============================================================================
// 十四、状态切换
// ============================================================================
static void setState(State s) {
  if (state != s) { diagState((uint8_t)state, (uint8_t)s); state = s; entered = true; }
}
static const char* stateName(State s) {
  switch (s) {
    case BOOT:    return "BOOT";
    case PROBE:   return "PROBE";
    case FEED:    return "FEED";
    case VISION:  return "VISION";
    case SHOW: return "SHOW";
    case SORT:    return "SORT";
    case RELEASE: return "RELEASE";
    case DELAY:   return "DELAY";
    case RESET:   return "RESET";
    case HALT:    return "HALT";
  }
  return "UNKNOWN";
}

// ============================================================================
// 十五、各状态处理
// ============================================================================
static void updateBoot() {
  if (entered) { entered = false; stateStartMs = millis(); LOG_INFO("上电，等待 K230 启动 %lu ms", (unsigned long)K230_BOOT_WAIT_MS); }
  if (millis() - stateStartMs >= K230_BOOT_WAIT_MS) setState(PROBE);
}

static void updateProbe() {
  if (entered) {
    entered = false;
    stateStartMs = millis();
    LOG_INFO("探测 K230：发送 image ...");
    visionRequest();
  }
  int r = visionPoll();
  if (r == 1 || r == 0) {
    LOG_INFO("K230 在线，进入送料");
    setState(FEED);
  } else if (millis() - stateStartMs >= VISION_TIMEOUT_MS) {
    LOG_ERROR("K230 无响应（未接入），停机");
    setState(HALT);
  }
}

static void updateFeed() {
  if (entered) {
    entered = false;
    conveyorStartAll();
    LOG_INFO("三级传送带已启动（其它执行机构未启动）");
  }
  setState(VISION);
}

static void updateVision() {
  if (entered) {
    entered = false;
    stateStartMs = millis();
    visionRequest();
  }
  int r = visionPoll();
  if (r == 1) {
    conveyorStopAll();
    LOG_INFO("识别到: 标签=%s id=%d conf=%d%% → 停传送带", vLabel, vId, (int)(vConf * 100));
    diagK230(true, (uint8_t)(vId & 0xFF), (uint16_t)(vConf * 100));
    setState(SHOW);
  } else if (r == 0) {
    if (millis() - stateStartMs >= POLL_GAP_MS) { visionRequest(); stateStartMs = millis(); }
  } else {
    if (millis() - stateStartMs >= VISION_TIMEOUT_MS) { visionRequest(); stateStartMs = millis(); }
  }
}

static void updateDisplay() {
  if (entered) {
    entered = false;
    if (vId >= 0 && vId < OBJECT_COUNT) {
      int color = vId % COLOR_COUNT + 1;
      int shape = vId / COLOR_COUNT + 1;
      hmiShowNum(HMI_COLOR_CTRL, color);
      hmiShowNum(HMI_SHAPE_CTRL, shape);
      LOG_INFO("HMI: 颜色编号=%d 形状编号=%d（%s %s）", color, shape, COLORS[vId % COLOR_COUNT], SHAPES[vId / COLOR_COUNT]);
    } else {
      LOG_WARN("HMI: 收到名称标签 [%s]，无法映射编号（仅日志）", vLabel);
      hmiShowText(HMI_TEXT_CTRL, "DETECT");
    }
    targetPos = random(SORTER_POSITION_COUNT);
    sorterMoveTo(targetPos);
    setState(SORT);
  }
}

static void updateSort() {
  if (!sorterBusy()) setState(RELEASE);
}

static void updateRelease() {
  if (entered) { entered = false; gateOpen(); }
  if (!gateBusy()) { delayStartMs = millis(); setState(DELAY); }
}

static void updateDelay() {
  if (millis() - delayStartMs >= RELEASE_DELAY_MS) setState(RESET);
}

static void updateReset() {
  if (entered) {
    entered = false;
    gateClose();
    sorterHome();
    LOG_INFO("开合关闭 + 分选复位");
  }
  if (!gateBusy() && !sorterBusy()) {
    diagCycle();
    conveyorStartAll();
    LOG_INFO("复位完成，重启三级传送带，进入下一轮");
    setState(FEED);
  }
}

static void updateHalt() {
  if (entered) {
    entered = false;
    conveyorStopAll();
    sorter.stop();
    gate.stop();
    motorEnable(false);
    LOG_ERROR("HALT：全部停机（K230 未接入）。请复位后重试。");
  }
}

// ============================================================================
// 十六、初始化 / 主循环
// ============================================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial1.begin(HMI_BAUD);
  Serial2.begin(VISION_BAUD);
  diagInit();

  randomSeed(analogRead(A0) + (unsigned long)millis());

  pinMode(OUT1_PIN, OUTPUT); digitalWrite(OUT1_PIN, LOW);
  pinMode(OUT2_PIN, OUTPUT); digitalWrite(OUT2_PIN, LOW);
  pinMode(OUT3_PIN, OUTPUT); digitalWrite(OUT3_PIN, LOW);
  pinMode(X_STEP_PIN, OUTPUT); pinMode(X_DIR_PIN, OUTPUT);
  pinMode(Y_STEP_PIN, OUTPUT); pinMode(Y_DIR_PIN, OUTPUT);
  pinMode(MOTOR_EN_PIN, OUTPUT);

  motorEnable(true);
  sorter.setMaxSpeed(SORTER_SPEED);
  sorter.setAcceleration(SORTER_ACCEL);
  sorter.setMinPulseWidth(MIN_PULSE_US);
  sorter.setCurrentPosition(0);
  gate.setMaxSpeed(GATE_SPEED);
  gate.setAcceleration(GATE_ACCEL);
  gate.setMinPulseWidth(MIN_PULSE_US);
  gate.setCurrentPosition(0);

  LOG_INFO("==== GeometricSortingTest 启动（方案 B） ====");
  LOG_INFO("SIMULATION_MODE=%d", SIMULATION_MODE);
  state = BOOT;
  entered = true;
}

void loop() {
  // 串口命令（仅事后接电脑回读诊断日志）：d=dump，c=clear
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'd' || c == 'D') diagDump();
    else if (c == 'c' || c == 'C') diagClear();
  }
  if (!SIMULATION_MODE) { sorter.run(); gate.run(); }

  State prev = state;
  switch (state) {
    case BOOT:    updateBoot();    break;
    case PROBE:   updateProbe();   break;
    case FEED:    updateFeed();    break;
    case VISION:  updateVision();  break;
    case SHOW: updateDisplay(); break;
    case SORT:    updateSort();    break;
    case RELEASE: updateRelease(); break;
    case DELAY:   updateDelay();   break;
    case RESET:   updateReset();   break;
    case HALT:    updateHalt();    break;
  }
  if (prev != state) LOG_INFO("状态迁移: %s -> %s", stateName(prev), stateName(state));
}
