# QA 方案：找 bug 机制与暴力测试设计（第十轮）

## 0. 原则

按用户要求设计的**机制化**找 bug 流程，全部落地在 `selftest` 内（环境变量门控），
每次发版前按 §4 矩阵跑一遍。三类探测器覆盖三类 bug：

| 机制 | 目标 bug 类 | 落地 |
|---|---|---|
| ① 连发风暴 + **时间预算** | 同步重活×连发 = 主线程排队卡死（⌘⇧M 连点卡死类） | `NAUGHT_QA` 段 `QA-STORM` |
| ② 状态矩阵 + 不变量 | 状态组合下的崩溃/失字/状态机失步 | `NAUGHT_QA` 段 `QA-MATRIX` |
| ③ 边界压力 + 混合 | 大文档/长行的性能悬崖、资源爆炸 | `NAUGHT_QA` 段 `QA-STRESS` |
| ④ 种子混沌（已有，保留） | 随机交叉的崩溃/内存错误 | `NAUGHT_FUZZ` |
| ⑤ 精确配方（已有，保留） | 用户报障的固定复现 | `NAUGHT_RECIPE` / `NAUGHT_FREEZE` |
| ⑥ 像素取证（已有，保留） | 光标/渲染的像素级核对 | `NAUGHT_CURPROBE` |

## 1. 时间预算机制（卡死类 bug 的机械探测器）

核心思想：**每个功能键按住 25 连发（≈自动重复），整组操作必须在 3 秒内完成**。
连发 = 每按一次都触发一次完整处理路径的放大器——凡是"每拍同步重活"的实现
（全文档重排、全量重印、逐拍重建）都会被预算当场抓住。

- 键表：全部 20 个功能键（⌘S/N/D/E/B/T/⇧M/I/O/L/F/⇧C/⇧G/⇧J/⇧K/=/−/0/Z/Y/A）。
- 三档文档：200 行 × CRT 开 / 2000 行 / 4000 字长行。
- 修复模式（已用于 ⌘⇧M）：**重活合并**——即时部分（调色板/标脏）每拍做，
  重活部分（全文档重排/画布重印）经 200ms 合并定时器只做一次。凡新键报
  预算超限，先找它的"每拍重活"再同样合并。

## 2. 状态矩阵（状态机审计）

每个功能键在四态下各打一次并验不变量（文档可访问、快照可画、无崩溃）：
空文档 / CJK 文档 / 带选区 / 大文档。状态轴（mode×codeMode×CRT×machine×dark×
selection）的完整笛卡尔积由 ④ 的混沌风暴以采样方式覆盖；矩阵段保证**每个键
在极端态**至少被点到。

## 3. 不变量

轻量但全覆盖：`characterCount ≥ 0`、`paintTextSnapshot` 非空、管线活性
（readback 落地）、看门狗零触发。重量不变量（撤销日志一致性、选区几何、
行号对齐）由各专项闸负责。

## 4. 发版矩阵（每次全跑）

```bash
# 主自检 ×3 + DPR2 ×3 + ASAN + QA + 混沌种子 1..N
for i in 1 2 3; do <selftest>; done
for i in 1 2 3; do QT_SCALE_FACTOR=2 <selftest>; done
ASAN_OPTIONS=detect_leaks=0 <build-asan --selftest>
NAUGHT_QA=1 <selftest>
NAUGHT_FUZZ=1 NAUGHT_FUZZ_SEED=<seed> <selftest>   # 种子越多越好，≥8
NAUGHT_RECIPE=1 / NAUGHT_FREEZE=1 / NAUGHT_CURPROBE=1   # 按需
```

## 5. 已知遗留（下轮）

- 画布烘焙缓存无上界（长文档多笔画 OOM 风险）→ 分块/tile 化或视口裁剪。
- agent 通配 `naught*.app` 全开（用户四轮拍板保留，但应改白名单）。
- `crt.frag.full/.probe` 黄金参考与主 shader 脱节 → 同步或删除。
- 黑帧拦截阈值对"顶部无字的合法暗帧"误拦 1.5s → 改对比上一帧。
