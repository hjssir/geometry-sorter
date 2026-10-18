/*
 * ============================================================================
 *  GeometricSorting.ino —— 几何体分选主控程序（主入口）
 * ============================================================================
 *  硬件：Arduino Mega2560（外接改装版）
 *  功能：整合「传感器 → 三级传送带 → 视觉识别 → 分选 → 开合 → 复位」完整流程，
 *        并驱动串口屏显示识别结果。
 *
 *  模块划分（同一文件夹下）：
 *    config.h        —— 引脚 / 串口 / 电机参数 / 对照表 / 常量（集中配置）
 *    motion.h/.cpp   —— 步进电机控制（分选电机 + 开合电机）
 *    conveyor.h/.cpp —— 直流传送带 + 红外/激光传感器
 *    vision.h/.cpp   —— K230 视觉串口解析 + 对照表查询
 *    hmi.h/.cpp      —— 淘晶驰串口屏通信
 *    logger.h        —— 串口日志
 *
 *  主流程（状态机）：
 *    IDLE → FEED(三级全启动) → DETECT(传感器逐级停带) → STOP_POS(停3号,视觉开始)
 *    → VISION(收K230结果) → DECIDE(查表/分盒) → SORT(分选电机到位)
 *    → RELEASE(开合电机打开) → DELAY(落料停留) → RESET(复位+三级重启) → IDLE
 *    另含 ERROR / ESTOP 异常状态。
 *
 *  详见同目录 README.md。
 * ============================================================================
 */
#include "config.h"
#include "logger.h"
#include "motion.h"
#include "conveyor.h"
#include "vision.h"
#include "hmi.h"
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// 状态枚举
// ---------------------------------------------------------------------------
enum State {
  IDLE,      // 待机
  FEED,      // 启动三级传送带
  DETECT,    // 监测传感器，逐级停带
  STOP_POS,  // 停 3 号传送带，视觉识别开始
  VISION,    // 收 K230 识别结果
  DECIDE,    // 查表 / 分配盒位
  SORT,      // 分选电机到位
  RELEASE,   // 开合电机打开放行
  DELAY,     // 落料停留
  RESET,     // 复位 + 三级重启
  ERROR,     // 异常
  ESTOP      // 急停
};

static State state = IDLE;      // 当前状态
static bool  entered = true;    // 刚进入当前状态（执行一次性动作）
static bool  xTriggered = false;// 传感器1 是否已触发
static bool  yTriggered = false;// 传感器2 是否已触发
static VisionResult visionResult;         // 最近一次视觉结果
static unsigned long delayStartMs = 0;    // DELAY 状态起始时刻
static unsigned long visionStartMs = 0;   // VISION 状态起始时刻

// ---------------------------------------------------------------------------
// 标签 → 储物盒 映射（6 个盒位）
// ---------------------------------------------------------------------------
// 规则：同一标签永远分到同一个盒；新标签占用一个空盒；6 盒全满 → 第 7 类。
// 掉电不保存，每次上电视为新任务。
struct BoxSlot {
  bool used;
  char tag[TAG_MAX_LEN];
};
static BoxSlot boxes[SORTER_POSITION_COUNT];

static void boxesClear() {
  for (int i = 0; i < SORTER_POSITION_COUNT; i++) {
    boxes[i].used = false;
    boxes[i].tag[0] = '\0';
  }
}

// 分配盒位：已有相同标签→返回原盒位；新标签→分配空盒；全满→-1
static int boxesAssign(const char *tag) {
  if (tag == NULL || tag[0] == '\0') return -1;
  for (int i = 0; i < SORTER_POSITION_COUNT; i++) {
    if (boxes[i].used && strcmp(boxes[i].tag, tag) == 0) {
      LOG_INFO("标签 %s 已登记 -> 复用盒位 %d", tag, i);
      return i;
    }
  }
  for (int i = 0; i < SORTER_POSITION_COUNT; i++) {
    if (!boxes[i].used) {
      boxes[i].used = true;
      strncpy(boxes[i].tag, tag, TAG_MAX_LEN - 1);
      boxes[i].tag[TAG_MAX_LEN - 1] = '\0';
      LOG_INFO("标签 %s -> 新分配盒位 %d", tag, i);
      return i;
    }
  }
  LOG_ERROR("标签 %s：六个储物盒已占满（第 7 类）", tag);
  return -1;
}

// ---------------------------------------------------------------------------
// 状态切换 + 状态名
// ---------------------------------------------------------------------------
static void setState(State s) {
  if (state != s) {
    state = s;
    entered = true;
  }
}

static const char* stateName(State s) {
  switch (s) {
    case IDLE: return "IDLE";
    case FEED: return "FEED";
    case DETECT: return "DETECT";
    case STOP_POS: return "STOP_POS";
    case VISION: return "VISION";
    case DECIDE: return "DECIDE";
    case SORT: return "SORT";
    case RELEASE: return "RELEASE";
    case DELAY: return "DELAY";
    case RESET: return "RESET";
    case ERROR: return "ERROR";
    case ESTOP: return "ESTOP";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// 各状态处理（每个主循环周期调用一次）
// ---------------------------------------------------------------------------
static void updateIdle() {
  // 真实硬件模式：上电即自动开始输送；模拟模式：等串口命令 start
  if (!SIMULATION_MODE) setState(FEED);
}

static void updateFeed() {
  if (entered) {
    entered = false;
    conveyorStartAll();            // 三级同时启动
    hmiUpdateStatus("启动输送");
  }
  setState(DETECT);
}

static void updateDetect() {
  if (entered) {
    entered = false;
    xTriggered = false;
    yTriggered = false;
  }

  sensorUpdate();                  // 采样 + 消抖

  // 传感器1 触发（物料经过 1 号末端）→ 停 1 号
  if (sensorFallingEdge(1)) {
    conveyorStop(1);
    xTriggered = true;
    LOG_INFO("传感器1 触发 → 停 1 号传送带");
  }
  // 传感器2 触发（物料经过 2 号末端）→ 停 2 号
  if (sensorFallingEdge(2)) {
    conveyorStop(2);
    yTriggered = true;
    LOG_INFO("传感器2 触发 → 停 2 号传送带");
  }

  // 前两级都触发过 → 进入视觉定位
  if (xTriggered && yTriggered) setState(STOP_POS);
}

static void updateStopPos() {
  if (entered) {
    entered = false;
    conveyorStop(3);               // 停 3 号，保证单物料进识别通道
    hmiUpdateStatus("视觉识别中");
    LOG_INFO("停 3 号传送带，开始视觉识别");
  }
  setState(VISION);
}

static void updateVision() {
  if (entered) {
    entered = false;
    visionStartMs = millis();
    visionRequest();               // 发 "image" 触发 K230
    LOG_INFO("向 K230 发送 image，等待识别结果");
  }

  if (visionPoll()) {
    visionResult = visionGetResult();
    setState(DECIDE);
    return;
  }

  // 超时：K230 没回复（低置信度时 K230 不回复）
  if (millis() - visionStartMs >= VISION_TIMEOUT_MS) {
    visionResult.valid = false;
    visionResult.tag[0] = '\0';
    LOG_WARN("视觉超时（%lums）无回复", (unsigned long)VISION_TIMEOUT_MS);
    setState(DECIDE);
  }
}

static void updateDecide() {
  // 情况一：没拿到有效标签
  if (!visionResult.valid) {
    LOG_WARN("视觉结果无效（未识别/置信度不足/超时）");
    if (HALT_ON_VISION_INVALID) {
      LOG_ERROR("按策略停机：所有电机停止");
      setState(ERROR);
    } else {
      LOG_WARN("按策略跳过分选与开合，物料直接放行");
      delayStartMs = millis();
      setState(DELAY);
    }
    return;
  }

  // 情况二：有标签 —— 用数学公式法查对照表，得到颜色+形状名称
  String color, shape;
  getObjectInfo(visionResult.id, color, shape);
  LOG_INFO("识别 ID=%d -> 颜色=%s 形状=%s conf=%.2f",
           visionResult.id, color.c_str(), shape.c_str(), (double)visionResult.confidence);

  // 串口屏：发送颜色编号 + 形状编号（屏本地映射成文字/图片）
  hmiShowObject(visionResult.id);

  // 分配储物盒（仍用标签字符串作唯一标识）
  int slot = boxesAssign(visionResult.tag);
  if (slot < 0) {
    LOG_ERROR("第 7 类（标签 %s）：六个储物盒已占满", visionResult.tag);
    if (HALT_ON_SLOT_FULL) {
      setState(ERROR);
    } else {
      delayStartMs = millis();
      setState(DELAY);
    }
    return;
  }

  // 正常：记录盒位，驱动分选电机
  LOG_INFO("DECIDE: 标签=%s -> 盒位=%d（盒号=%d）conf=%.2f",
           visionResult.tag, slot, slot + 1, (double)visionResult.confidence);
  sorterMoveToPosition(slot);
  setState(SORT);
}

static void updateSort() {
  sorterRun();
  if (!sorterBusy()) setState(RELEASE);
}

static void updateRelease() {
  if (entered) {
    entered = false;
    gateOpen();                    // 打开开合机构放行
  }
  gateRun();
  if (!gateBusy()) {
    delayStartMs = millis();
    setState(DELAY);
  }
}

static void updateDelay() {
  if (millis() - delayStartMs >= RELEASE_DELAY_MS) {
    setState(RESET);
  }
}

static void updateReset() {
  if (entered) {
    entered = false;
    gateClose();                   // 开合机构复位
    sorterHome();                  // 分选机构回原点
  }
  gateRun();
  sorterRun();
  if (!gateBusy() && !sorterBusy()) {
    conveyorStartAll();            // 三级同时重启
    setState(IDLE);
  }
}

static void updateError() {
  if (entered) {
    entered = false;
    conveyorStopAll();
    LOG_ERROR("进入 ERROR 状态，等待 reset 命令");
  }
}

static void updateEstop() {
  if (entered) {
    entered = false;
    conveyorStopAll();
    LOG_ERROR("急停 ESTOP！所有传送带已停");
  }
}

// ---------------------------------------------------------------------------
// 主状态机调度
// ---------------------------------------------------------------------------
static void stateMachineUpdate() {
  State prev = state;
  switch (state) {
    case IDLE:     updateIdle();     break;
    case FEED:     updateFeed();     break;
    case DETECT:   updateDetect();   break;
    case STOP_POS: updateStopPos();  break;
    case VISION:   updateVision();   break;
    case DECIDE:   updateDecide();   break;
    case SORT:     updateSort();     break;
    case RELEASE:  updateRelease();  break;
    case DELAY:    updateDelay();    break;
    case RESET:    updateReset();    break;
    case ERROR:    updateError();    break;
    case ESTOP:    updateEstop();    break;
  }
  if (prev != state) {
    LOG_INFO("状态迁移: %s -> %s", stateName(prev), stateName(state));
  }
}

// ---------------------------------------------------------------------------
// 模拟模式：串口命令注入
// ---------------------------------------------------------------------------
static void injectCommand(const char *cmd) {
  if (strcmp(cmd, "start") == 0 || strcmp(cmd, "go") == 0) {
    if (state == IDLE) setState(FEED);
    else LOG_WARN("已在运行中，忽略 start");
  }
  else if (strcmp(cmd, "x") == 0 || strcmp(cmd, "1") == 0) {
    if (SIMULATION_MODE) { conveyorStop(1); xTriggered = true; LOG_INFO("注入 传感器1 触发 → 停1号传送带"); }
    else LOG_WARN("非模拟模式，忽略 x");
  }
  else if (strcmp(cmd, "y") == 0 || strcmp(cmd, "2") == 0) {
    if (SIMULATION_MODE) { conveyorStop(2); yTriggered = true; LOG_INFO("注入 传感器2 触发 → 停2号传送带"); }
    else LOG_WARN("非模拟模式，忽略 y");
  }
  else if (strncmp(cmd, "vision", 6) == 0) {
    const char *p = cmd + 6;
    while (*p == ' ') p++;
    visionSetFakeTag(p);
  }
  else if (strcmp(cmd, "estop") == 0 || strcmp(cmd, "stop") == 0) {
    setState(ESTOP);
  }
  else if (strcmp(cmd, "reset") == 0) {
    if (state == ESTOP || state == ERROR) {
      boxesClear();
      setState(IDLE);
    } else {
      LOG_WARN("当前非 ESTOP/ERROR，忽略 reset");
    }
  }
  else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "s") == 0) {
    LOG_INFO("当前状态=%s", stateName(state));
  }
  else if (strcmp(cmd, "map") == 0) {
    for (int i = 0; i < SORTER_POSITION_COUNT; i++) {
      LOG_INFO("  盒位 %d -> %s", i, boxes[i].used ? boxes[i].tag : "(空)");
    }
  }
  else if (strcmp(cmd, "clear") == 0) {
    boxesClear();
    LOG_INFO("标签映射表已清空");
  }
  else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
    LOG_INFO("命令: start|go   启动输送");
    LOG_INFO("      x|y(1|2)  注入传感器触发(仅模拟)");
    LOG_INFO("      vision N  设置视觉假标签");
    LOG_INFO("      map       打印标签->盒位映射");
    LOG_INFO("      clear     清空映射表");
    LOG_INFO("      estop|stop 急停");
    LOG_INFO("      reset     从ESTOP/ERROR恢复");
    LOG_INFO("      status|s  打印状态");
    LOG_INFO("      help|h    帮助");
  }
  else {
    LOG_WARN("未知命令: %s（输入 help 查看）", cmd);
  }
}

// ---------------------------------------------------------------------------
// 初始化 / 主循环
// ---------------------------------------------------------------------------
void setup() {
  logger_init(SERIAL_BAUD);
  LOG_INFO("==== GeometricSorting 主控程序启动 ====");
  LOG_INFO("SIMULATION_MODE = %d（%s）", SIMULATION_MODE,
           SIMULATION_MODE ? "模拟" : "真实硬件");

  // 等待 K230 启动完成（老机器实测约需 10 秒）
  if (!SIMULATION_MODE) {
    LOG_INFO("等待 K230 启动 %d ms ...", K230_BOOT_WAIT_MS);
    delay(K230_BOOT_WAIT_MS);
  }

  // 初始化各模块
  motionInit();
  conveyorInit();
  sensorInit();
  visionInit();
  hmiInit();
  boxesClear();

  state = IDLE;
  entered = true;
  LOG_INFO("初始化完成，当前状态=%s", stateName(state));

  if (SIMULATION_MODE) {
    LOG_INFO("模拟模式：输入 help 查看串口命令");
  }
}

void loop() {
  // 驱动状态机（非阻塞）
  stateMachineUpdate();

  // 模拟模式：从串口读取命令注入
  if (SIMULATION_MODE && Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      injectCommand(line.c_str());
    }
  }
}
