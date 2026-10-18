# 主控程序（AutoMeasureSort）

> 全项目**唯一**主线程序目录。编译/烧录只认这里；旧版程序在 `90_历史存档/`，禁止编译。

## 文件

| 文件 | 作用 |
|---|---|
| `AutoMeasureSort.ino` | 主线：上电自动传送 → K230 检测 → 停带 → 分选 → 开合落料 → 复位循环 |
| `diaglog.h` | 离线诊断日志（EEPROM 408 条 × 10 B，`d` dump / `c` clear） |

## 编译与烧录

- 板卡：Arduino Mega 2560（ATmega2560，256 KB Flash / 8 KB SRAM / 4 KB EEPROM）
- 工具链（2026-10-18 实测并固定）：

| 组件 | 版本 | 位置 |
|---|---|---|
| `arduino-cli` | **1.5.2-rc.1** | `%LOCALAPPDATA%\Programs\arduino-cli\`（已加入用户 PATH） |
| `arduino:avr` core | **1.8.8** | `%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\1.8.8` |
| `avr-gcc` | **7.3.0-atmel3.6.1-arduino7** | Arduino15 内 |
| `AccelStepper` | **1.64** | `%USERPROFILE%\Documents\Arduino\libraries` |

- **一条命令**（`_build/` 被 gitignore，不污染仓库）：

```powershell
.\10_主控_Arduino\compile.ps1                      # 编译
.\10_主控_Arduino\compile.ps1 -Upload -Port COM5   # 编译 + 烧录
```

- 裸命令（Arduino 要求 sketch 目录名 = `.ino` 名，脚本会自动暂存到 `_build/AutoMeasureSort/`）：

```powershell
arduino-cli compile --fqbn arduino:avr:mega --warnings more --output-dir _build/out _build/AutoMeasureSort
```

- **实测结果（2026-10-18）**：`0 错误 0 警告`；Flash **17,824 B（7%）**；全局变量 **1,369 B（16%）**，剩 6,823 B
- ⚠️ 警告口径：`--warnings more`（= `-Wall`）本工程 0 警告；`--warnings all`（= `-Wextra`）会多出 **5 条来自 Arduino core `new.cpp` 的上游警告**，不是本工程代码。验收统一用 `more`
- 细节与历史记录：`99_调试记录/2026-10-18_工具链与编译/记录.md`

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
