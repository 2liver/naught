# naught Windows「显」闪退修复 —— 交接报告

> 环境：Windows 11 / Visual Studio 18 (MSVC 14.50.35717) / Qt 6.9.3 msvc2022_64
> 仓库基线：`origin/main` @ `1442256`（merge: 启动台单版本文档）
> 修复提交：本地提交 `b230290`，补丁见 `naught-windows-fix.patch`

---

## TL;DR

Windows 版按 `Ctrl+T` 进「显」必崩，**根因是 `CrtView::ensureRhi()` 把 `nullptr` 当作 QRhi 的后端初始化参数传给 `QRhi::create()`**。QRhi 各后端构造函数无条件解引用该指针，Windows 上探测链第二站（Vulkan）即空指针访问违例。

已修复并实测通过；同一 bug 同样会让 **Linux** 闪退（探测链第一站就是 Vulkan）。macOS 因为 Metal 排第一且可用，从未走到崩溃点——所以这个 bug 只在 Windows 上暴露。

顺带修掉两处跨平台「显」缺陷（入场黑屏 1.5s + 每帧刷告警；重入「显」白做两次整管线重建），以及 **导致这次事故发版的 CI 漏洞**。

---

## 1. 根因

### 1.1 缺陷代码（修复前 `crt_view.cpp`）

```cpp
const struct { QRhi::Implementation impl; const char *name; } backends[] = {
    { QRhi::Metal, "metal" }, { QRhi::Vulkan, "vulkan" },
    { QRhi::D3D11, "d3d11" }, { QRhi::D3D12, "d3d12" },
    { QRhi::OpenGLES2, "gles2" }, { QRhi::Null, "null" },
};
for (const auto &b : backends) {
    if (skipSet.contains(QLatin1String(b.name))) continue;
    m_r = QRhi::create(b.impl, nullptr);   // ← 病根：params 恒为 nullptr
    ...
}
```

### 1.2 QRhi 侧：构造函数无条件解引用 `params`

Qt 6.9 源码（`src/gui/rhi/`）：

| 后端 | 构造函数第一行 | 传 nullptr 的后果 |
|---|---|---|
| Vulkan | `qrhivulkan.cpp`：`inst = params->inst;` | 空指针访问违例 |
| D3D11 | `qrhid3d11.cpp`：`debugLayer = params->enableDebugLayer;` | 空指针访问违例 |
| D3D12 | `qrhid3d12.cpp`：`debugLayer = params->enableDebugLayer;` | 空指针访问违例 |
| GLES2 | `qrhigles2.cpp`：`requestedFormat = params->format;` | 空指针访问违例 |
| Metal | `qrhimetal.cpp` | 无参可用，但 Windows 构建**没有** Metal |
| Null | `qrhinull.cpp`：`Q_UNUSED(params);` | 唯一安全的 |

### 1.3 为什么偏偏在 Windows 炸

官方 Qt Windows 构建的 `qtgui-config.h`：

```c
#define QT_FEATURE_opengl  1
#define QT_FEATURE_vulkan  1
#define QT_FEATURE_metal  -1     // ← 没有 Metal
```

于是探测链：

1. `QRhi::create(QRhi::Metal, nullptr)` → `#else qWarning("This platform has no Metal support")`，`r->d` 仍为空 → 返回 `nullptr`（**安全**）
2. `QRhi::create(QRhi::Vulkan, nullptr)` → `new QRhiVulkan(nullptr, nullptr)` → **`inst = params->inst;` → 崩**

macOS 上第 1 步就成功返回，永远走不到第 2 步，所以 mac 端开发/测试全程无感。
Linux 上第 1 步同样返回 `nullptr`，第 2 步照样崩 —— **Linux 版也是坏的**。

---

## 2. 证据链（可复现）

### 2.1 用户实机崩溃记录与本地复现逐字节一致

Windows 事件日志（`Application` / `Application Error`）中，用户自己那份分发包的崩溃：

```
错误应用程序名称: naught.exe，版本: 0.0.0.0，时间戳: 0x6aaf1d95
错误模块名称: Qt6Gui.dll，版本: 6.9.3.0
异常代码: 0xc0000005
错误偏移量: 0x00000000004d79e6
错误应用程序路径: C:\Users\...\Downloads\naught-windows\naught.exe
```

本地用同一份分发包复现，偏移量**完全相同**：

```
错误模块名称: Qt6Gui.dll      异常代码: 0xc0000005
错误偏移量: 0x00000000004d79e6
错误应用程序路径: C:\Dev\naught-windows\naught.exe
```

`0x4d79e6` 正是 `QRhiVulkan::QRhiVulkan` 里读 `params->inst` 的那条指令。

### 2.2 逐后端二分：每个真后端都崩，全跳过才活

| 环境变量 | 实际到达的后端 | 结果 |
|---|---|---|
| （未设） | Vulkan | `0xC0000005` @ Qt6Gui+0x4d79e6 |
| `NAUGHT_RHI_SKIP=vulkan` | D3D11 | `0xC0000005` @ Qt6Gui+0x3bf10d |
| `NAUGHT_RHI_SKIP=vulkan,d3d11` | D3D12 | `0xC0000005` @ Qt6Gui+0x3d8f3e |
| `NAUGHT_RHI_SKIP=vulkan,d3d11,d3d12` | GLES2 | `0xC0000005` @ Qt6Gui+0xdf79a |
| `NAUGHT_RHI_SKIP=vulkan,d3d11,d3d12,gles2` | **Null** | exit 0（假绿） |

### 2.3 真·GUI 实测（真实窗口 + 真实按键）

- **修复前**（用户分发包）：启动 → `SetForegroundWindow` → 合成 Ctrl+T → **进程当场消失**。
- **修复后**：进「显」→ 换机（Ctrl+Shift+M）→ 视角锁定（Ctrl+Shift+T）→ 退出 → 隔 8 秒重进 → 全部存活，窗口截图确认琥珀 CRT 正常出字。

---

## 3. 变更清单

| 文件 | 变更 |
|---|---|
| `crt_view.cpp` | +116 行：RHI 后端初始化参数工厂；探测链改平台原生优先；`showEvent` 开新会话；`paintEvent` 看门狗对称 `invalidate()`；黑帧拦截加语义闸 + 到期接受新基线 |
| `selftest.cpp` | +14 行：`NAUGHT_REQUIRE_GPU=1` 硬闸 |
| `.github/workflows/build.yml` | Windows 自检不再跳过 `d3d11/d3d12`，加硬闸，回显 `CRT-RHI` 后端名 |
| `.github/workflows/release.yml` | 同上（**这份才是真正产出 Windows zip 的工作流**） |
| `CHANGELOG.md` | 新增 `0.3.16（Windows「显」闪退修复）` |

### 3.1 核心修复：每个后端配自己的 InitParams

```cpp
// 后端 → 构造参数；nullptr = 本平台/本次构建拿不到参数 = 跳过该后端
QRhiInitParams *naughtRhiParams(QRhi::Implementation impl)
{
    switch (impl) {
    case QRhi::Metal: {
#if QT_CONFIG(metal)
        static QRhiMetalInitParams p;  return &p;
#else
        return nullptr;
#endif
    }
    case QRhi::D3D11: {
#if defined(Q_OS_WIN)
        static QRhiD3D11InitParams p;  return &p;
#else
        return nullptr;
#endif
    }
    /* … D3D12 / Vulkan / GLES2 / Null 同款 … */
    }
    return nullptr;
}
```

Vulkan 另外需要一个**活过 QRhi** 的 `QVulkanInstance`，GLES2 需要 `fallbackSurface`，
两者都用进程级单例承载（管线随重建反复走 `ensureRhi`，避免反复创建/销毁）：

```cpp
#if NAUGHT_RHI_HAVE_VULKAN   // = QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
QVulkanInstance *naughtVulkanInstance()
{
    static QVulkanInstance *inst = []() -> QVulkanInstance * {
        QVulkanInstance *v = new QVulkanInstance;
        if (v->create()) return v;
        delete v; return nullptr;   // 无 loader / 无驱动 → 本平台放弃 Vulkan
    }();
    return inst;
}
#endif
```

调用点：

```cpp
QRhiInitParams *params = naughtRhiParams(b.impl);
if (!params) {
    qWarning("CRT-RHI skip backend %s (no init params in this build)", b.name);
    continue;                     // ← 绝不把 nullptr 递给 create
}
m_r = QRhi::create(b.impl, params);
```

### 3.2 探测链改平台原生优先

```cpp
#ifdef Q_OS_MACOS
    { QRhi::Metal, "metal" }, { QRhi::Null, "null" },          // gles2 在 mac 会崩在 Qt 内部，不进候选
#elif defined(Q_OS_WIN)
    { QRhi::D3D11, "d3d11" }, { QRhi::D3D12, "d3d12" },
    { QRhi::Vulkan, "vulkan" }, { QRhi::OpenGLES2, "gles2" }, { QRhi::Null, "null" },
#else
    { QRhi::Vulkan, "vulkan" }, { QRhi::OpenGLES2, "gles2" }, { QRhi::Null, "null" },
#endif
```

macOS 原先靠 `skipSet.insert("gles2")` 打的补丁改为从候选表里去掉（等价且更清楚）。
`NAUGHT_RHI_SKIP` 逃生阀保留，语义不变。

### 3.3 「显」入场黑屏 1.5s + 每帧刷告警

黑帧拦截器原本只按亮度阈值判"从亮突然变暗"：

```cpp
const bool suddenDark = m_prevTopLum >= 60 && mx < 60;
```

实测（Windows 真实 GUI，空文档）触发**天天都会遇到的两类误报**：

1. 默认是「阳」（白底），进「显」会换肤成暗底 → 被判成"视口渲染坏了"；
2. 空文档下**块光标眨眼**让顶部采样在亮/暗之间跳。

后果：入场先冻结 1.5s（用户看到黑屏一会儿），随后 `m_prevTopLum` 基线永不回落 →
**此后每帧命中、每帧刷一条 `CRT dark snapshot` 告警**（实测一次会话刷 17 条）。

修复：

```cpp
// 文档真没字时不存在"失字"这回事
const bool suddenDark = m_prevTopLum >= 60 && mx < 60
                        && m_source->sourceHasText();
...
qWarning("CRT dark snapshot for %lld ms — uploading as-is ...");
m_prevTopLum = int(mx);      // 时限到了就认这份暗为新的基线
m_darkSince.invalidate();    // 拦截器回到正常态，不再每帧重报
```

### 3.4 重入「显」白做两次整管线重建

活性看门狗的 `m_lastLanded` 跨会话残留：退出「显」超过 6s 再进来，`show()` 后第一帧就撞上
"回读 14100 ms 未落地" → 全量重建；而 `paintEvent` 与 `renderFrame` 两处看门狗**各触发一次** = 重建两遍。

```cpp
void CrtView::showEvent(QShowEvent *)
{
    m_lastLanded.invalidate();
    m_readbackClock.invalidate();
    m_readbackInFlight = false;   // 上一会话若被隐藏打断，在途标志不得拖住新会话
    ensureRhi();
    ...
}
```

`paintEvent` 的看门狗补上与 `renderFrame` 同款的 `m_lastLanded.invalidate();`。

### 3.5 CI 补洞（本次事故的真正成因）

`build.yml` **和** `release.yml` 的 Windows 自检都是：

```powershell
$env:NAUGHT_RHI_SKIP = 'vulkan,d3d11,d3d12,gles2'   # ← 所有真后端全跳过
```

→ 探测链只能落到 Null。而 Null **对一切说"成功"却不产出真帧**，所有断言照过。
于是"Windows 进「显」闪退"在 CI 全绿的情况下发到了用户手里。现已：

```powershell
$env:NAUGHT_RHI_SKIP   = 'vulkan,gles2'   # 只跳过跑者上真拿不到的
$env:NAUGHT_REQUIRE_GPU = '1'             # 后端悄悄退回 Null 即判失败
```

配套在 `selftest.cpp` 加了硬闸：

```cpp
if (qEnvironmentVariableIsSet("NAUGHT_REQUIRE_GPU")
    && e.m_crtView && !e.m_crtView->pipelineUsable()) {
    qWarning("selftest FAIL: NAUGHT_REQUIRE_GPU is set but the CRT pipeline "
             "is not usable (backend fell back to Null or pipeline creation "
             "failed) — see the 'CRT-RHI backend:' line above");
    return false;
}
```

`NAUGHT_CI=1` 仍然跳过像素调校闸（无 GPU 跑者的字体/像素差异），**只**新增"后端必须是真的"这一条——
粒度刚好：既不引入 CI 抖动，又能拦住这次的静默降级。

---

## 4. 验证矩阵（Windows 实测，全部本地跑过）

```
================= FINAL REGRESSION (Windows / MSVC 14.50 / Qt 6.9.3) =================
selftest-full-d3d11        PASS  exit=0  backend=[D3D11]  watchdog=0 darkwarn=0
selftest-ci-d3d11          PASS  exit=0  backend=[D3D11]  watchdog=0 darkwarn=0
selftest-full-d3d12        PASS  exit=0  backend=[D3D12]  watchdog=0 darkwarn=0
selftest-dpr1.5            PASS  exit=0  backend=[D3D11]  watchdog=0 darkwarn=0
selftest-dpr2.0            PASS  exit=0  backend=[D3D11]  watchdog=0 darkwarn=0
stress-d3d11               PASS  exit=0  backend=[D3D11]  watchdog=0 darkwarn=0
stress-d3d12               PASS  exit=0  backend=[D3D12]  watchdog=0 darkwarn=0
bench-d3d11                PASS  exit=0  backend=[D3D11]  watchdog=0 darkwarn=0
gate-null-must-fail        FAIL  exit=1  backend=[Null]  ← 硬闸按预期拦住（这条"红"是正确行为）
```

`--selftest` 是**完整模式**（不带 `NAUGHT_CI`），即所有 GPU 像素断言真跑：

```
CRT-RHI backend: d3d11
CRT-SNAP amber top=13 ink=7984
CRT-INPUT-BURST watchdog=0 maxReadback=14ms
CRT-COLORS amber=7978 blue=0 dark=150874 other=148
CRT-SCANLINE rows: 22.10 vs 22.05
CRT-VIGNETTE edge=61% corner=74% maxDrop=31%
CRT-CHAOS 90 ops passed (undo/redo interleave consistent)
CRT-SHIFT-STORM watchdog=0 compositor text ok
CRT-SCROLL-GHOST gray=0
CRT-CURSOR-GATE cursorCell=641 block=641
DIFF-EDGE rightCol=10 wrong=0 leftCol=10 wrong=0
```

真·GUI 实测（`SetForegroundWindow` + `keybd_event` 合成真实按键，非 `--selftest`）：

| 场景 | 结果 |
|---|---|
| 空文档 Ctrl+T 进「显」 | 存活，CRT 正常 |
| 打字后 Ctrl+T | 存活，琥珀字正确出屏 |
| Ctrl+Shift+M 换机 | 存活 |
| Ctrl+Shift+T 视角锁定 | 存活 |
| 退出「显」→ 隔 8s 重进 | 存活，**且不再有看门狗重建**（修复前此处重建两遍） |
| 拖窗口改尺寸（缩小/放大） | 存活，CRT 全程覆盖客户区 |
| 150% DPI（`QT_SCALE_FACTOR=1.5`） | 存活 |
| 右键菜单（Windows 唯一功能入口） | 正常弹出（摹/空/阴/阳/涂/擦/消/编/显/言/隔） |
| WM_CLOSE 关窗 | 干净退出 |

---

## 5. 请你（mac 端）做的事

### 5.1 确认 macOS / Linux 构建

我这边**只有 Windows 工具链**，macOS / Linux 没编译过。改动都做了平台宏隔离，
但请务必在你的机器上重新跑一遍三平台 CI：

- macOS：探测链 `Metal → Null`，行为与修复前等价（Metal 永远优先）。
  `NAUGHT_RHI_SKIP=vulkan` 现在是空操作（候选表里已无 vulkan），可以删掉也可以留着。
- Linux：这个是**真修复**——修复前 `libvulkan` 存在时按 Ctrl+T 必崩（Vulkan 排第一）。
  现在 Vulkan 只有在构建期能拿到 `<vulkan/vulkan.h>` 时才会进候选，
  并配了真正的 `QVulkanInstance`；拿不到就退到 GLES2 → Null。
  ⚠️ 这一支我**没能编译验证**（本机无 Vulkan SDK 头），请重点看 Ubuntu 那条 CI。

### 5.2 重新分发

```bash
# 版本号（可选，若你要出 0.3.16）
#   CMakeLists.txt: project(naught VERSION 0.3.16 ...)
git am naught-windows-fix.patch      # 或直接 merge 我这边的提交
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>
cmake --build build --parallel --config Release
```

打 Windows zip 时记住 `release.yml` 里已改好的自检会**真的跑 D3D11**——
如果它红了，先看 `::notice::CRT-RHI backend: ...` 那行回显的后端名再判断。

### 5.3 已经打好的可运行包（Windows，立即可测）

`C:\Dev\naught-windows-fixed\`（32.7 MB）—— 直接双击 `naught.exe` 即可，
或整目录覆盖用户手上那份坏的 `naught-windows\`。内容与旧包逐项对齐
（`Qt6*.dll` / `platforms\qwindows.dll` / `styles` / `imageformats` /
`iconengines` / `tls` / `networkinformation` / `D3Dcompiler_47.dll`）。

⚠️ **两点差异，分发时请以你的 CI 产物为准**：

1. `naught_icon_tool.exe` 未收录（图标重生成的开发工具，运行期不需要）。
2. **`KF6SyntaxHighlighting.dll` 缺失** —— 我这边没有 KF6，所以这个 exe 是
   **不带 `NAUGHT_WITH_HIGHLIGHT`** 编译的，「编」（`Ctrl+B`）会退化成
   等宽字体 + 行号（设计内的可选依赖降级）。你的 `release.yml` 会从源码
   构建 KF6 并装进包里，正式分发请务必走那条路径，否则用户会丢掉语法高亮。

### 5.4 建议补一条 CI（可选）

macOS 那条自检目前也没开硬闸。若你要同样的保护：

```yaml
NAUGHT_RHI_SKIP: ""          # 候选表里已无 vulkan，可留可删
NAUGHT_REQUIRE_GPU: "1"      # 跑者 Metal 可用时才加
```

GitHub 的 macOS 跑者一般有 Metal，但我无法验证，所以没替你改。

---

## 6. 其他发现（未改，供你判断）

1. **Linux 版同样崩溃** —— 见 1.3 / 5.1。这次修复顺带治好了它，但请实测确认。
2. **`m_readbackGen` 在 `resetPipeline()` 里没有自增**：只有"800ms 在途看门狗 ×3"那条路径会
   `++m_readbackGen`。管线被 `resetPipeline()` 换掉后，上一代的迟到回读仍会落地一次
   （写 `m_shown` / 刷 `m_lastLanded`）。目前无害（只是画一帧旧内容），但如果以后
   回读要携带更多状态，这里是隐患。
3. **回读回调里的 `QRhiReadbackResult *rb` 在"回调永不触发"时会泄漏**（如管线中途销毁）。
   实测没触发，属于防御性观察。
4. **`d3dcompiler_47.dll` 是 Windows 运行期硬依赖**：QRhi D3D11 用它编译 HLSL。
   现有分发包里已经带了，`windeployqt` 也会带；但如果你手工拼包，别忘了它
   （Windows 10/11 的 System32 里也有一份兜底）。

---

## 7. 复现/验证命令速查

```powershell
# 修复前（用你手上的旧分发包）：真实窗口按 Ctrl+T → 进程消失
powershell -File C:\Dev\tools\gui-test.ps1 -Exe <旧包>\naught.exe -QtBin <旧包>

# 修复后
$env:PATH="C:\Qt\6.9.3\msvc2022_64\bin;$env:PATH"
$env:QT_PLUGIN_PATH="C:\Qt\6.9.3\msvc2022_64\plugins"

# 完整自检（真 D3D11 + 全部 GPU 像素断言）
$env:QT_QPA_PLATFORM="offscreen"
.\build\Release\naught.exe --selftest

# 模拟 CI（等价于改好的 build.yml / release.yml）
$env:NAUGHT_CI="1"; $env:NAUGHT_RHI_SKIP="vulkan,gles2"; $env:NAUGHT_REQUIRE_GPU="1"
.\build\Release\naught.exe --selftest

# 崩溃取证（Windows 事件日志里的错误模块 / 偏移量）
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'} -MaxEvents 5 |
  ForEach-Object { $_.Message }
```

---

## 附：一句话总结给未来的自己

**QRhi 的 `QRhi::create(impl, params)` 不接受 `nullptr` —— 每个后端都必须配自己的
`InitParams`，拿不到就跳过这个后端。任何"后端探测链"都必须真跑一遍真后端，
否则 Null 后端会用"全绿"骗过所有人。**
