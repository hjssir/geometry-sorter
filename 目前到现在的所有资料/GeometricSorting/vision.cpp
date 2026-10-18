/*
 * ============================================================================
 *  vision.cpp —— 视觉识别（K230，Serial2）实现
 * ============================================================================
 *  串口：K230 接 Mega2560 的 Serial2（RX=16, TX=17），波特率 115200。
 *
 *  收帧：逐字节累积，收到 '\n' 判定为一帧结束；K230 的"未识别"回包可能没有
 *        换行符，靠前缀 "No objects" 提前判定，避免死等超时。
 * ============================================================================
 */
#include "vision.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>

// K230 接 Serial2
#define VISION_PORT   Serial2

static char   _buf[VISION_FRAME_MAX];   // 收帧缓冲
static uint8_t _len = 0;                // 已收字节数
static bool   _ready = false;           // 是否已收到完整帧
static bool   _fakeReady = false;       // 模拟模式：假结果就绪标志
static VisionResult _result;            // 最近一次结果

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
void visionInit() {
  _len = 0;
  _buf[0] = '\0';
  _ready = false;
  _fakeReady = false;
  _result.valid = false;
  _result.tag[0] = '\0';
  _result.id = -1;
  _result.confidence = 0.0f;

  if (!SIMULATION_MODE) {
    VISION_PORT.begin(VISION_BAUD);
  }
  if (SIMULATION_MODE) {
    // 模拟模式：给一个默认假标签，保证不上硬件也能跑通
    visionSetFakeTag("0");
  }
  LOG_INFO("vision init (K230, Serial2 @%ld, 触发词=\"%s\")",
           (long)VISION_BAUD, VISION_TRIGGER_WORD);
}

// ---------------------------------------------------------------------------
// 触发
// ---------------------------------------------------------------------------
void visionRequest() {
  _len = 0;
  _buf[0] = '\0';
  _ready = false;

  if (SIMULATION_MODE) {
    _fakeReady = true;   // 模拟模式：不真发串口，结果立刻可用
    return;
  }
  VISION_PORT.print(VISION_TRIGGER_WORD);
}

// ---------------------------------------------------------------------------
// 解析一帧（兼容多种格式）
// ---------------------------------------------------------------------------
// 支持格式：
//   1) "标签 置信度"   例 "37 0.95"   -> tag="37", confidence=0.95
//   2) "名称,编号"     例 "红球体,3"  -> tag="红球体,3"（整体当标签，编号随后拆）
//   3) "数字" 或 "名称"  单 token     -> tag=该 token
//   4) "No objects..."            -> 无效（未识别）

// 从标签字符串解析数字 ID：优先取逗号后的数字，否则整串按数字解析。
// 含非数字字符时解析失败，返回 -1。
static int tagToId(const char *tag) {
  const char *num = strchr(tag, ',');
  if (num != NULL) num = num + 1;      // "名称,编号" 取逗号后
  else num = tag;
  while (*num == ' ') num++;
  if (*num == '\0') return -1;
  for (const char *q = num; *q != '\0'; q++) {
    if (*q < '0' || *q > '9') return -1;   // 含非数字字符 -> 无效
  }
  return atoi(num);
}

static bool parseFrame(const char *frame) {
  char tmp[VISION_FRAME_MAX];
  strncpy(tmp, frame, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  // 去掉首尾空白/换行
  int n = strlen(tmp);
  while (n > 0 && (tmp[n - 1] == '\r' || tmp[n - 1] == '\n' || tmp[n - 1] == ' ')) {
    tmp[--n] = '\0';
  }
  char *p = tmp;
  while (*p == ' ') p++;

  if (*p == '\0') return false;   // 空帧

  // K230 未识别时的固定回包
  if (strncmp(p, "No objects", 10) == 0) return false;

  // 默认置信度 1.0
  _result.confidence = 1.0f;

  // 格式2："名称,编号" —— 找到逗号，拆成名称+编号
  char *comma = strchr(p, ',');
  if (comma != NULL) {
    // 逗号前是名称，逗号后是编号，整体标签存成 "名称,编号"
    strncpy(_result.tag, p, TAG_MAX_LEN - 1);
    _result.tag[TAG_MAX_LEN - 1] = '\0';
    _result.id = tagToId(_result.tag);   // 解析出数字 ID
    _result.valid = true;
    return true;
  }

  // 格式1："标签 置信度" —— 按空格拆分
  char *sp = strchr(p, ' ');
  if (sp != NULL) {
    *sp = '\0';
    const char *conf = sp + 1;
    while (*conf == ' ') conf++;
    if (*conf != '\0') _result.confidence = atof(conf);
  }

  strncpy(_result.tag, p, TAG_MAX_LEN - 1);
  _result.tag[TAG_MAX_LEN - 1] = '\0';

  if (_result.tag[0] == '\0') {
    _result.valid = false;
    return false;
  }
  _result.id = tagToId(_result.tag);   // 解析出数字 ID
  _result.valid = true;
  return true;
}

// ---------------------------------------------------------------------------
// 轮询收帧（非阻塞）
// ---------------------------------------------------------------------------
bool visionPoll() {
  if (_ready) return true;

  // 模拟模式
  if (SIMULATION_MODE) {
    if (!_fakeReady) return false;
    _fakeReady = false;
    _ready = true;
    LOG_INFO("vision(模拟) result: tag=%s id=%d conf=%.2f",
             _result.tag, _result.id, (double)_result.confidence);
    return true;
  }

  // 真实模式：逐字节累积
  while (VISION_PORT.available() > 0) {
    char c = (char)VISION_PORT.read();

    if (c == '\n') {
      _buf[_len] = '\0';
      if (parseFrame(_buf)) {
        _ready = true;
        // 用数学公式法查对照表，打印出可读的颜色 + 形状
        String color, shape;
        getObjectInfo(_result.id, color, shape);
        LOG_INFO("vision result: id=%d (颜色=%s 形状=%s) conf=%.2f",
                 _result.id, color.c_str(), shape.c_str(), (double)_result.confidence);
      } else {
        LOG_WARN("vision 收到无效帧: [%s]", _buf);
      }
      _len = 0;
      _buf[0] = '\0';
      if (_ready) return true;
      continue;
    }

    if (_len < VISION_FRAME_MAX - 1) {
      _buf[_len++] = c;
      _buf[_len] = '\0';
    }

    // "No objects" 没有换行，靠前缀提前判定
    if (_len >= 10 && strncmp(_buf, "No objects", 10) == 0) {
      _result.valid = false;
      _result.tag[0] = '\0';
      _result.id = -1;
      _ready = true;
      LOG_WARN("vision: 未识别到物体");
      _len = 0;
      _buf[0] = '\0';
      return true;
    }
  }
  return false;
}

VisionResult visionGetResult() {
  return _result;
}

void visionSetFakeTag(const char *tag) {
  _result.valid = true;
  strncpy(_result.tag, tag, TAG_MAX_LEN - 1);
  _result.tag[TAG_MAX_LEN - 1] = '\0';
  _result.id = tagToId(_result.tag);   // 解析出数字 ID
  _result.confidence = 0.99f;
  LOG_INFO("vision(模拟) 设置假标签 = %s (id=%d)", _result.tag, _result.id);
}

// ---------------------------------------------------------------------------
// 查对照表（数学公式法）
// ---------------------------------------------------------------------------
// 用公式替代 403 项大数组，避免 SRAM 耗尽：
//   颜色 = COLORS[id % COLOR_COUNT]   （id % 13）
//   形状 = SHAPES[id / COLOR_COUNT]   （id / 13）
// 越界（id < 0 或 id >= 403）时 color/shape 都置为 "UNKNOWN"。
void getObjectInfo(int id, String &color, String &shape) {
  if (id < 0 || id >= OBJECT_COUNT) {
    color = "UNKNOWN";
    shape = "UNKNOWN";
    return;
  }
  color = COLORS[id % COLOR_COUNT];
  shape = SHAPES[id / COLOR_COUNT];
}
