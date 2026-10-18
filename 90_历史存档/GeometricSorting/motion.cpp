/*
 * ============================================================================
 *  motion.cpp —— 步进电机控制实现
 * ============================================================================
 *  引脚（config.h）：分选电机 STEP=10 / DIR=26；开合电机 STEP=11 / DIR=28；
 *                    使能 MOTOR_EN_PIN=24（低电平有效，两轴共用）。
 *
 *  本文件完全沿用旧程序（hal_sorter / hal_gate）的控制逻辑：
 *    - A4988 STEP/DIR 接口，AccelStepper 接口类型 1
 *    - 非阻塞：moveTo() 设定目标，run() 每个循环推进一次
 *    - 分选电机 6 位置；开合电机 开/关 两位置
 *  只把旧的「类」改写成「全局变量 + 函数」的简单形式，算法不变。
 * ============================================================================
 */
#include "motion.h"
#include "logger.h"

// AccelStepper 接口类型 1 = STEP/DIR（适配 A4988）
#define MOTOR_INTERFACE_TYPE  1

// ---- 两台步进电机对象 ----
// 注意：AccelStepper 构造需要引脚，这里在 .ino 里通过 motionInit() 传入。
// 用指针+延迟构造，避免全局对象初始化顺序问题。
static AccelStepper *sorterStepper = NULL;   // 分选电机（电机1）
static AccelStepper *gateStepper   = NULL;   // 开合电机（电机2）

static bool sorterIsBusy = false;            // 分选电机运动标志
static bool gateIsBusy   = false;            // 开合电机运动标志
static int  sorterPos    = 0;                // 分选电机当前目标位置 0..5

// ---------------------------------------------------------------------------
// 位置 -> 步数（分选电机）
// ---------------------------------------------------------------------------
// 360° 均分 6 位置：每位置相隔 SORTER_STEPS_PER_REV / 6 步。
// 注意：1600 不能被 6 整除，整数除法每位置有 ±1 步（约 0.225°）舍入误差，
//       对分选定位通常够用；若对角度敏感需换驱动细分或机械减速比。
static long sorterPosToSteps(int pos) {
  return (long)pos * SORTER_STEPS_PER_REV / SORTER_POSITION_COUNT;
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
void motionInit() {
  sorterIsBusy = false;
  gateIsBusy   = false;
  sorterPos    = 0;

  if (SIMULATION_MODE) {
    LOG_INFO("motion init (模拟模式，不驱动真实电机)");
    return;
  }

  // 使能引脚：低电平有效，两轴共用
  pinMode(MOTOR_EN_PIN, OUTPUT);
  digitalWrite(MOTOR_EN_PIN, LOW);   // 低电平 = 使能

  // 创建两台步进电机对象（A4988 STEP/DIR）
  sorterStepper = new AccelStepper(MOTOR_INTERFACE_TYPE, X_STEP_PIN, X_DIR_PIN);
  gateStepper   = new AccelStepper(MOTOR_INTERFACE_TYPE, Y_STEP_PIN, Y_DIR_PIN);

  // 分选电机：速度/加速度沿用旧值
  sorterStepper->setMaxSpeed(SORTER_SPEED);
  sorterStepper->setAcceleration(SORTER_ACCELERATION);
  sorterStepper->setCurrentPosition(0);

  // 开合电机：速度/加速度沿用旧值
  gateStepper->setMaxSpeed(GATE_SPEED);
  gateStepper->setAcceleration(GATE_ACCELERATION);
  gateStepper->setCurrentPosition(0);

  LOG_INFO("motion init ok: 分选电机(STEP=%d,DIR=%d) 开合电机(STEP=%d,DIR=%d)",
           X_STEP_PIN, X_DIR_PIN, Y_STEP_PIN, Y_DIR_PIN);
  LOG_INFO("  分选: %d 位置, %ld 步/圈; 开合: 开 %ld 步/关 %ld 步",
           SORTER_POSITION_COUNT, (long)SORTER_STEPS_PER_REV,
           (long)GATE_OPEN_STEPS, (long)GATE_CLOSE_STEPS);
}

// ---------------------------------------------------------------------------
// 分选电机（电机1）
// ---------------------------------------------------------------------------
void sorterMoveToPosition(int pos) {
  // 越界约束到 0..5
  if (pos < 0) pos = 0;
  if (pos > SORTER_POSITION_COUNT - 1) pos = SORTER_POSITION_COUNT - 1;

  sorterPos = pos;
  long steps = sorterPosToSteps(pos);
  LOG_INFO("分选电机 -> 位置 %d（%ld 步）", pos, steps);

  if (SIMULATION_MODE) {
    sorterIsBusy = false;   // 模拟模式：瞬时到位
    return;
  }
  sorterStepper->moveTo(steps);
  sorterIsBusy = true;
}

void sorterHome() {
  sorterMoveToPosition(0);
}

void sorterRun() {
  if (SIMULATION_MODE || !sorterIsBusy) return;
  sorterStepper->run();
  if (sorterStepper->distanceToGo() == 0) {
    sorterIsBusy = false;
    LOG_INFO("分选电机到位 位置 %d", sorterPos);
  }
}

bool sorterBusy() {
  return sorterIsBusy;
}

// ---------------------------------------------------------------------------
// 开合电机（电机2）
// ---------------------------------------------------------------------------
void gateOpen() {
  LOG_INFO("开合电机 打开（%ld 步）", (long)GATE_OPEN_STEPS);
  if (SIMULATION_MODE) {
    gateIsBusy = false;   // 模拟模式：瞬时到位
    return;
  }
  gateStepper->moveTo(GATE_OPEN_STEPS);
  gateIsBusy = true;
}

void gateClose() {
  LOG_INFO("开合电机 关闭（0 步）");
  if (SIMULATION_MODE) {
    gateIsBusy = false;
    return;
  }
  gateStepper->moveTo(GATE_CLOSE_STEPS);
  gateIsBusy = true;
}

void gateRun() {
  if (SIMULATION_MODE || !gateIsBusy) return;
  gateStepper->run();
  if (gateStepper->distanceToGo() == 0) {
    gateIsBusy = false;
    LOG_INFO("开合电机到位");
  }
}

bool gateBusy() {
  return gateIsBusy;
}
