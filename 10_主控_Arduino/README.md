# 主控程序（AutoMeasureSort）

> 全项目**唯一**主线程序目录。编译/烧录只认这里；旧版程序在 `90_历史存档/`，禁止编译。

## 文件

| 文件 | 作用 |
|---|---|
| `AutoMeasureSort.ino` | 主线：上电自动传送 → K230 检测 → 停带 → 分选 → 开合落料 → 复位循环 |
| `diaglog.h` | 离线诊断日志（EEPROM 408 条 × 10 B，`d` dump / `c` clear） |

## 编译与烧录

- 板卡：Arduino Mega 2560（ATmega2560，256 KB Flash / 8 KB SRAM / 4 KB EEPROM）
- 依赖库：`AccelStepper`（版本待固化）
- **当前状态（2026-10-18）**：本机 PATH 里没有 `avr-g++` / `arduino-cli`，工作区也没有构建脚本 → **"0 错 0 警告"目前不可复现**（看板 B3）
- 阶段 1-6 要求：装 `arduino-cli` + 固定 AVR core 版本 + 固定 AccelStepper，固化一条可重复命令，写回本 README，例如：

```powershell
# 待工具链装好后替换为真实命令，并把版本号写死
arduino-cli compile --fqbn arduino:avr:mega --output-dir _build .
arduino-cli upload  --fqbn arduino:avr:mega -p COMx .
```

## 运行与日志

- USB 串口：**115200**（`Serial`），看启动横幅 + 状态迁移日志
- 命令：`d` = dump 诊断日志、`c` = clear；`e` 急停 / `r` 复位 **属阶段 1 新增，当前未实现**
- 注意：运行中发 `d` 会同步打印 408 条，约 1~2 s 不执行电机 → 可能丢步，**停机时再 dump**

## 当前程序结构（改动前基线）

- 6 个状态：`RUN / DETECTED / SORT / RELEASE / DELAY / RESET`（`AutoMeasureSort.ino:91`）
- **没有** `BOOT / PROBE / ERROR / ESTOP / SIMULATION`
- 每 500 ms 无条件重发 `image`，无重试计数（`:257-258`）
- `NONE` / `No objects` 分支目前等于死代码（`:180、:252`）
- SORT / RELEASE 无超时看门狗（`:286-291`）

> 目标状态与异常策略见 `00_接口约定/状态机与异常策略.md`；参数唯一值见 `00_接口约定/参数表.md`。

## 改动纪律

1. 凡是接口（回包格式、HMI 指令、波特率、N、超时、脉冲数）→ **先改 `00_接口约定/`，打 tag，再改代码**
2. 每改一处 → 立即编译（阶段 1 起必须可重复）→ 语法/警告清零再继续
3. 每次改完 → 日志/现象存 `99_调试记录/<日期>_<主题>/`（模板已给）
4. 不改 `90_历史存档/` 里的任何东西；需要旧版本用 `git show v0.1-baseline:<路径>`
