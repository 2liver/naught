# 路线图（roadmap）

> 状态：2026-09 已拍板版。本文件是**执行规格 + 继任者交接文档**：任何模型/人接手，
> 读完本文件即可知道"做到哪了、下一步做什么、怎么做、怎么验证、怎么部署"。
> 画面设计细节见 `docs/crt-filter.md`（互为表里）。

## 用户拍板记录（不可漂移）

- 快捷键：M1 视角锁定/解锁 = **Cmd/Ctrl+Shift+T**；M2 换机器 = **Cmd/Ctrl+Shift+M**；
  M6 重开 = **Cmd+Ctrl+Shift+N**。
- M1 语义：**进「显」即锁定**（即使上次解锁过）；解锁只在本次显会话内有效；
  退出再进 → 再次锁定。
- M6 自杀键 = **Ctrl+Cmd+N**（macOS 上与系统 Cmd+Q 并存；Windows/Linux 无系统级
  "退出即无"概念 → 在「项」菜单放一个 Ctrl+Cmd+N 条目）。
- 上述快捷键**只进「项」菜单，不进右键菜单**；「项」内分区归类由实现者安排。

## 里程碑（M0 已办）

| # | 内容 | 状态 |
|---|---|---|
| M0 | 「显」+「编」光标落点偏移 bug（cursorRect 视口坐标缺行号槽偏移） | ✅ 已修已部署（cdb2497） |
| M1 | 「显」追随视角锁定（复现鼠标离开窗口的完美视角，Cmd+Shift+T） | ✅ 已上线（5b40d31） |
| M2 | 切换计算机：IBM 5100 绿磷模式（调色板重构 + Cmd+Shift+M） | ✅ 已上线（4aecf65），绿磷参数待用户校准 |
| M3 | 图片 → ASCII 字符画（拖入即显，隐藏功能，零 UI） | ✅ 已上线（4cf3517）；v2 插入语义（a2ef62f） |
| M4 | CRT 微增量包：磷粉余晖快慢双指数、聚焦漂移（四角微散焦）、极弱桶形 | 待办 |
| M4.5 | 「实验」区（「项」内分两个可叠加开关）：屏幕实体（边框/微曲率/玻璃）与固定字符网格（80×24/16×64），均需不裁字优化 | 待办 |
| M5 | GitHub 里程碑：naught(無) 更名/概念/README EN/打包分发/推送（7897） | 待办 |
| M6 | 自杀与重生：Ctrl+Cmd+N 自杀 + Cmd+Ctrl+Shift+N OS 级重开（安装程序） | 待办 |
| M7 | 高尔夫化/压行/逻辑简化/深模块化/性能优化 | 待办 |

## 里程碑细节

### M1 追随视角锁定
- `Editor` 持 `m_viewLock`（默认 true）；`toggleCrt()` 进入显时**重置为 true**。
- 锁定 = view uniform 恒 **(-0.25, -0.12)**——复现鼠标离开窗口后的"完美视角"
  （观察者站在屏幕正前方，内容完整不被裁剪）；解锁 = 鼠标驱动（lastMouseViewport 路径）。
- 「项」菜单名：**「追随视角锁定」**（自释性，避免与未来图片功能的缩放语义
  冲突；Cmd/Ctrl+Shift+T，可勾选，仅显模式有意义）；Windows/Linux 键处理同步。
- 验收：进显 = 与鼠标离开窗口时同一画面；Cmd+Shift+T 后跟随鼠标；
  退出重进 → 重新锁定。

### M2 绿磷模式
- `Crt::Palette { ink, inkDim, cursorBlock, bg, scanTint, refl, dust }` +
  `Amber` / `Green` 预设；shader 染色项经 ubuf 传调色板（ubuf 扩至 64B）。
- 绿磷起始值：经典终端绿 #33FF33 系（P1 磷光），参照 IBM 5100（5" 单色 CRT、
  16×64 文本、绿磷）。
- **深链待办**：dk54wpr7 对话末尾 HTML 实例的默认参数（页面正文经 JS 内部 API
  已攻下 API（/api/v0/share/content）并全量审读：正确链接为 1a7s39ge（打字动效对话，含多主题 HTML 实例）——「终端绿」主题已作校准基准（bg #0a0a0a / text #33ff33 / textDim #1a8a1a / cursor #00ff00 / glow rgba(0,255,0,0.35)，6704a80 已对齐）。
  或由用户贴出关键参数做最终校准。
- 与编/阴/阳正交；行号、笔迹、块光标随调色板换色。

### M3 图片 → ASCII
- 拖放 QImage → 灰度/边缘密度 → 字符梯度（复用 AsciiTools 的 charset：
  ` .:*#@` 或 ` .,-~:;=!*#$@`，边缘密度决定梯度选择）。
- 内容即普通文本（可编辑/复制）；NoWrap 防重排；行列数随字号/窗口自适应。
- 快捷键入「项」；README 不提及（隐藏功能）。
- 参考实现：`~/Projects/AsciiTools/src/symbolrender.js`（img() 函数）。

### M4 CRT 微增量包
- 余晖快慢双指数：快分量 ~1-2 帧、慢分量 ~15-30 帧，替代单指数。
- 聚焦漂移：画面四角轻微散焦（shader 里按 |v_uv-0.5| 混入小核模糊）。
- 极弱桶形：曲率 0.05 → 0.02 级别的可选微调（铁律：不毁打字）。

### M5 GitHub
- 概念：naught /nɔːt/ 单音节古英语"无/零"（come to naught = 化为乌有）；
  品牌 "naught(無)"；图标/头像仍用「無」。
- 更名：项目/文件/文件夹统一 naught（本地工作目录 wu → naught）。
- README 英文版 + 概念述说；CHANGELOG 归并；CI 三平台打包复核。
- 推送走 Clash 代理 7897（`export https_proxy=http://127.0.0.1:7897`）。

### M6 自杀与重生
- 自杀：Ctrl+Cmd+N 立即退出（无保存提示，"关闭即无"的极致）；macOS 与 Cmd+Q 并存。
- 重开：安装程序布署极小登录项代理（持 Cmd+Ctrl+Shift+N 全局快捷键），
  应用死亡时代理 `open naught.app`。Windows/Linux 等价物另议（先 macOS）。
- 安装程序替代"zip 解压安装"。

### M7 重构与性能
- crt 管线出 `editor.h` → `crt.{h,cpp}`；CrtView 与 Editor 解耦为接口。
- bench 驱动优化：crt-scroll ≤ 5ms/步、打字 ≤ 0.5ms/键。
- 高尔夫化/语义统合最后做（模块边界清晰之后）。

## 开工流程（每步必做，写入提交信息）

1. **实现**（遵循 `docs/crt-filter.md` 铁律：零 UI、单一模式、纯画面、不毁打字）。
2. **构建**：cmake 不在 PATH！用
   `/Users/2liver/Library/Python/3.9/lib/python/site-packages/cmake/data/bin/cmake --build build`
3. **验证**：`build/naught.app/Contents/MacOS/naught --selftest`（连续 3 次全绿）；
   性能敏感改动跑 `--bench`（约 8 分钟，预算：滚动 ≤5ms/步、打字 ≤0.5ms/键）。
4. **提交**：本地 git（仓库 /Users/2liver/Documents/Dev/wu，分支 main）。
5. **部署**（顺序不可省，缺 macdeployqt 会 dyld 崩溃）：
   ```sh
   PREFIX="$HOME/Applications"
   cmake --install build --prefix "$PREFIX"
   /Users/2liver/Qt/6.9.3/macos/bin/macdeployqt "$PREFIX/naught.app"
   cp /Users/2liver/kf6/lib/libKF6SyntaxHighlighting.6.31.0.dylib \
      "$PREFIX/naught.app/Contents/Frameworks/"
   LOAD=$(otool -L "$PREFIX/naught.app/Contents/MacOS/naught" | grep KF6Syntax | awk '{print $1}')
   install_name_tool -change "$LOAD" \
     "@executable_path/../Frameworks/libKF6SyntaxHighlighting.6.31.0.dylib" \
     "$PREFIX/naught.app/Contents/MacOS/naught"
   install_name_tool -id "@executable_path/../Frameworks/libKF6SyntaxHighlighting.6.31.0.dylib" \
     "$PREFIX/naught.app/Contents/Frameworks/libKF6SyntaxHighlighting.6.31.0.dylib"
   LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
   "$LSREG" -f "$PREFIX/naught.app"
   env -i HOME="$HOME" "$PREFIX/naught.app/Contents/MacOS/naught" --selftest   # 必须 exit 0
   osascript -e 'tell application id "com.2liver.naught" to quit'  # 无实例可忽略报错
   open "$PREFIX/naught.app"
   ```
6. **取证工具**（像素级验证，位于 /tmp/sbprobe/，环境变量 DYLD_FRAMEWORK_PATH 需指向 Qt）：
   ascii（亮度字符画）、probe/row/px/px2（采样）、find/hot/halo（颜色统计）、
   diff（逐像素差异）、cols/edge（列剖面）、cur（块光标验证）。

## 继任者交接（若会话中断）

- 仓库：`/Users/2liver/Documents/Dev/wu`（分支 main，最近提交依次：
  7f16a78 路线图 → cdb2497 光标偏移修复 → 23eb0f5 光栅调制批次 → 2c8ac03 物理拟真批次）。
- 已部署到 `~/Applications/naught.app`（启动台「無」，自包含 Qt+KF6）。
- **下一步 = M4 CRT 微增量包**（细节见上文）。
- 已额外落地：字体管理（「项」字体区：打开字体文件夹/上一/下一，出厂 Fusion Pixel + VT323，删除回退）与编模式中性白光标（a2ef62f）。
- 已知环境坑（别再踩）：
  - Qt 6.8+ QImage 画笔引擎层自动乘图像 DPR——快照里**禁止**手动 `p.scale(dpr)`；
  - Qt 6.9 滚动条住在私有容器 QWidget（`pos()` 恒 (0,0)）——快照用 `mapTo`；
  - `cursorRect()` 是视口坐标——编辑器坐标需加 `viewport()->pos()`；
  - `boxBlurH/V` 边缘钳制初始化必须计 (r+1) 次 0 号像素，否则 uchar 回绕 253；
  - 暖机等逐像素 CPU 循环曾冻结帧循环——时间类效果一律放 shader（timeInfo uniform）；
  - 自检对亮度敏感：束斑/辉光用加法（不压暗核心），否则 amber 检查不过。
- 设计文档：`docs/crt-filter.md`（配方与物理模型）；本文件（执行与部署）。
