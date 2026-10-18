# 协作规范（三人小组 + GitHub）

> 给第一次用 git/GitHub 的组员：**只看 §1~§4 就够用**，命令可以直接复制。
> 主控（仓库 owner）额外看 §5~§8。

## 0 一句话规则

**main 分支永远能编译；每个人的改动走自己的分支 + Pull Request（PR）；接口改动必须先改 `00_接口约定/`。**

## 1 分工（谁改哪里，这条决定了几乎不会有冲突）

| 目录 | 谁负责 | 说明 |
|---|---|---|
| `00_接口约定/` | 主控 | 三人确认后由主控改；别人提 Issue 或 PR 讨论 |
| `01_计划/`、`02_决策记录/` | 主控 | 计划与决策只在主控这边更新 |
| `10_主控_Arduino/` | 主控 | 固件；**只有这里允许编译/烧录** |
| `20_视觉_K230/` | 视觉同学 | 只改这个目录（`det_video.py`、`待收/`） |
| `30_HMI_串口屏/` | HMI 同学 | 只改这个目录（`.HMI`、`.tft`、`待收/`） |
| `90_历史存档/`、`91_文档快照/` | **谁都不改** | 只读证据；要旧版本用 `git show v0.1-baseline:<路径>` |
| `99_调试记录/` | 主控 | 每人也可以建自己的子目录 `99_调试记录/<日期>_<名字>/` |

**三个人改的是不同目录 → 几乎不会冲突。唯一的共享文件是 `00_接口约定/`，它走 PR 流程。**

## 2 第一次使用（只做一次）

1. 接受邀请：你会收到 GitHub 邮件 → 点 **Accept invitation**（或在仓库 Settings → Collaborators 里看）
2. 装工具（任选）：**GitHub Desktop**（小白推荐，图形界面）或命令行 Git（本机已有）
3. 克隆到自己电脑：

```bash
git clone https://github.com/zyi57909-bit/geometry-sorter.git
```

4. 配一下身份（只做一次）：

```bash
git config user.name  "你的 GitHub 用户名"
git config user.email "你的 GitHub 邮箱"
```

> ⚠️ 不要把仓库放在 **OneDrive/网盘同步目录** 里，也不要让编辑器一直占着二进制文件——之前 `ji.HMI` 被进程锁死，导致读/改/入库全部失败。

## 2.5 分支保护：现状与三条路

**已实测**：本仓库是**私有 + 免费账号**，GitHub 不允许开分支保护：

```text
gh api PUT .../branches/main/protection
→ HTTP 403：Upgrade to GitHub Pro or make this repository public to enable this feature.
```

| 路线 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| **A（推荐）** | 申请 **GitHub Student Developer Pack**（学生免费）→ 账号升级为 Pro → 运行 `.\tools\enable-branch-protection.ps1` | 服务端强制，谁都绕不过；仓库仍是私有 | 需要学生认证（学校邮箱/学生证），通常 1~3 天 |
| B | 把仓库改成 public | 立刻能开保护 | `ji.HMI` 是同学的作品，公开需三人同意，**不建议** |
| **C（今天就能用）** | 每台电脑跑一次 `.\tools\install-hooks.ps1`（本地 `pre-push` 钩子） | 免费、立即生效 | 只能防误操作：`--no-verify` 或 `ALLOW_MAIN_PUSH=1` 可绕过；没装钩子的电脑不受保护 |

> 当前状态：**C 已启用**（本机已装）；A 等你申请学生包后执行一次 `tools/enable-branch-protection.ps1` 即可。
> 不管有没有保护，规矩不变：**main 只接受 PR**。

## 3 日常改动五步（命令行）

```bash
git pull                                          # 1) 先拿别人最新的
git switch -c feat/vision-digital-id              # 2) 新建自己的分支（名字自取）
# ...改文件...
git status                                        # 3) 看改了哪些
git add -A && git commit -m "feat(vision): 回包改数字 id"   # 4) 提交
git push -u origin HEAD                           # 5) 推上去
```

推上去后打开仓库网页，会出现 **Compare & pull request** → 开 PR → 等 review → 合并。

**GitHub Desktop 等价操作**：`Current branch → New branch` → 改文件 → `Commit to ...` → `Push origin` → `Create Pull Request`。

合并后回到 main：

```bash
git switch main && git pull
```

## 4 PR 规矩（很简单，但请照做）

- 提 PR 时页面会**自动带出 PR 模板**（`.github/pull_request_template.md`），照着填空；**"证据"一节不填的 PR 不合并**
- 标题写清改什么；描述里贴**证据**：日志片段 / 截图 / `compile.ps1` 结果
- 至少 1 人 review 后再合并（主控合并 `10_`、`00_`；视觉/HMI 的改动由主控或对方 review）
- **main 只接受 PR**，不要直接往 main push
- 合并方式选 **Squash and merge**（历史干净，一次 PR 一个提交）
- 合并前不要 force push

## 4.5 什么时候开 Issue（3 个模板）

| 场景 | 用哪个模板 | 在哪 |
|---|---|---|
| 要改接口（回包 / 波特率 / 时序 / 参数 / 控件） | **接口变更** | 仓库 **Issues → New issue** |
| 实机 / 联调出故障 | **故障报告**（必须贴日志 + "已排除项"） | 同上 |
| 阶段计划里的具体任务 | **任务 / 待办** | 同上 |

规矩：**"要对方确认的事"一律变成 Issue，不要只在群里口头说**——Issue 有记录、能 @ 人、能关闭、能关联 PR。

## 5 冲突怎么办（新手最怕的部分）

- 冲突只会发生在"两个人改了同一个文件的同一处"。按 §1 分工，正常情况**几乎不会发生**
- 发生时的命令行处理：

```bash
git pull          # 报 conflict
# 打开冲突文件，搜索 <<<<<<<，手工留下正确内容，删掉标记
git add 冲突文件
git commit -m "fix: resolve conflict"
```

- 实在不会：GitHub Desktop 会提示 `Open in editor`；或者把冲突截图发群里，**别乱点"覆盖对方"**
- 消灭冲突的规矩：**`00_接口约定/` 同一时间只有一个人改**，谁改谁先在群里说一声

## 6 绝对不要提交的东西

- `_build/`、`*.hex`、`*.elf`、`*.bin`（已在 `.gitignore` / `.gitattributes` 处理）
- 单个 **>10 MB** 的文件（`ji.HMI` 9.5 MB 已经进库了，别再往里加大文件；`.tft`、`.kmodel` 走 GitHub Release 或 Git LFS）
- 密码 / token / 个人电脑路径
- 别人还没确认的工程文件（视觉/HMI 自己提 PR）

## 7 提交信息格式

`类型(范围): 说明`，类型用 `feat / fix / docs / chore / build / test`：

```text
feat(ams): 加 NONE 分支与 r==0 重试计数
docs(interface): 接口约定升 v0.2 并冻结
fix(vision): color_four 取模防越界
```

一次提交只做一件事；别把"改代码 + 改三份文档"混在一个提交里。

## 8 版本、交付与回滚

| 场景 | 做法 |
|---|---|
| 接口冻结 | 打 tag：`git tag -a v0.2-接口冻结 -m "..."` + `git push --tags` |
| 正式交付 | tag `v1.0-交付` + 在 GitHub 建 **Release**，附 `.hex`、`.tft`、交付清单 |
| 撤销某次改动 | `git revert <commit>`（**不要** reset 已经推送的历史） |
| 找回旧版本 | `git show v0.1-baseline:<路径>` |

## 9 出问题找谁

| 问题 | 找 |
|---|---|
| git/GitHub 操作、分支冲突 | 主控（你） |
| 硬件、接线、烧录、固件 | 主控 |
| 视觉识别、K230、类别表 | 视觉同学 |
| 串口屏、控件、波特率 | HMI 同学 |
