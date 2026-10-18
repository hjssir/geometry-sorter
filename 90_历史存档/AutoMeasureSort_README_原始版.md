# AutoMeasureSort —— 上电自动传送 → K230 视觉检测 → 分选

板级框架取自学长已验证的 `Hardware_control.ino`（同型号主控板），主流程为
**非阻塞状态机**（方案 B）。决策依据见 `docs/decisions-measurement-flow.md`。

## 一、流程（一个周期）

```
上电 ──立即──▶ 启动三级传送带
   │
   ▼
 RUN：周期发 "image" 给 K230；物体进入视野（= 到达测量工位）
   │  K230 确认检测到物体才回 "<标签> <置信度>"
   ▼
 DETECTED：停三级传送带 → HMI 显示 + USB 串口输出 RESULT
   ▼
 SORT：分选电机转到 6 位置之一
   ▼
 RELEASE：开合电机打开 80°（≈711 脉冲）
   ▼
 DELAY：落料停留 1s
   ▼
 RESET：开合关闭 + 分选复位 → 重启三级传送带 → 回到 RUN（循环）
```

> 到位检测 = **K230 视觉即到位**（本硬件无超声、无到位传感器）：传送带持续运转，
> K230 看到物体即视为到位，此时才返回结果 —— 天然满足「未检测到不提前返回」。

## 二、接线（与 Hardware_control.ino 一致）

| 设备 | 引脚 | 说明 |
|---|---|---|
| 传送带 1/2/3 | A1 / A2 / A3 | HIGH=启动 |
| 分选电机 PUL/DIR | 10 / 26 | 闭环步进 |
| 开合电机 PUL/DIR | 11 / 28 | 闭环步进 |
| 电机使能 ENA | 24 | 低有效 |
| 串口屏 TJC | Serial1 (18/19) | 9600 |
| K230 | Serial2 (16/17) | 115200 |
| USB 串口 | Serial | 115200（日志 + 上报上位机） |

## 三、关键参数（按几何体动作）

- 闭环步进 **16 细分 = 3200 脉冲/圈**；分选 6 位置；开合 80° ≈ 711 脉冲。
- 分选速度 300 步/s，开合速度 400 步/s。
- `VISION_WAIT_MS=500`：发 image 后 500ms 无结果则重发；`RELEASE_DELAY_MS=1000` 落料停留。
- 分选位置当前为「随机 6 选 1」，类别→盒位映射待定。

## 四、K230 侧改动（det_video.py）

同目录 `det_video.py` 是修改版，相对 reference 原版改了两处：

1. **空读保护**：`uart.read(10)` 无数据时返回 `None`，原代码 `None.decode()` 会崩溃退出（这就是之前
   K230 完全不回应的原因）；现加 `if text is None or len(text)==0: continue`。
2. **未检测到不提前返回**：删掉无物体时的 `uart.write("No objects were identified.")`，
   只有确认检测到物体（score>0.6）才回 `<标签> <置信度>`。

> 部署：把 `det_video.py` 拷到 K230 的 `/sdcard/app/tests/detect/`（并确保 `deploy_config.json` 与 `.kmodel` 就位）。

## 五、烧录与使用

1. Arduino IDE 安装 **AccelStepper** 库，打开 `AutoMeasureSort.ino`（同目录有 `diaglog.h`）。
2. 选 `Arduino Mega or Mega 2560` + COM 口，上传。
3. 上电自跑：**立即启动传送带**，K230 检测到物体后自动分选并循环。
4. 上位机查看：USB 串口（115200）会打印 `RESULT:<标签>,<置信度>`。

## 六、HMI 发送格式（沿用 GeometricSortingTest.ino）

TJC 指令：`<控件>.<属性>=值` + 结束符 `0xFF 0xFF 0xFF`：

| 指令 | 含义 |
|---|---|
| `t0.txt="..."` | 文本（如 \"DETECT\"） |
| `t1.val=N` | 颜色编号（1..13） |
| `t2.val=N` | 形状编号（1..31） |

检测到目标时：若 K230 标签为**数字 ID(0..402)**，则按公式换算 `颜色 = id%13+1`、`形状 = id/13+1` 并发 `t1.val`/`t2.val`；若为**名称标签**，则发 `t0.txt="DETECT"`。

## 七、离线诊断日志（diaglog.h）

同目录 `diaglog.h`（内置 EEPROM 持久化）：

- 记录 BOOT/STATE/CONV/SORTER/GATE/K230/VCC/CYCLE/RESET，含 VCC 采样与异常复位检测；
- 记录 5 个循环后停止（`DIAG_MAX_CYCLES`）；
- 事后接串口发 `d` = dump 全部日志，发 `c` = 清空。

## 七、待确认 / 可调项

- **分选位置映射**：当前随机；若需「标签 → 固定盒位」，给映射表即可替换。
- **K230 标签格式**：当前按 `<标签> <置信度>` 解析；若实际是其它分隔符，改 `finishFrame()` 即可。
- **HMI 发送格式**：沿用 `GeometricSortingTest.ino` 的 TJC 格式 `<控件>.<属性>=值` + `0xFF 0xFF 0xFF`，控件 `t0.txt`(文本) / `t1.val`(颜色编号) / `t2.val`(形状编号)（见第六节）。
