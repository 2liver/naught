# 無 (naught)

*Write in vain.*

空白。打开即写，关闭即无。

- 右键 / 双指点按：**摹**（全选并复制）· **空**（清空，可撤销）── **阴** · **阳** ── **涂**（画）· **擦**（橡皮擦）· **消**（清空全部笔迹）── **编**（代码：等宽字体 + 行号 + 语法高亮，无运行无保存）── **显**（回到过去：琥珀磷光 CRT 质感）── **言**（选中行头尾批量加「」，空行跳过）· **隔**（选中的每一行上下补空行，幂等）
- `Ctrl/Cmd + S`：摹　`Ctrl/Cmd + N`：空　`Ctrl/Cmd + Shift + N`：消（N=消除=naught，对应 Cmd+N 清文字）　`Ctrl/Cmd + I`：阴　`Ctrl/Cmd + O`：阳　`Ctrl/Cmd + D`：涂　`Ctrl/Cmd + E`：擦　`Ctrl/Cmd + B`：编　`Ctrl/Cmd + T`：显（T = Tube / Time，回到过去）　`Ctrl/Cmd + L`：言（L 是「」折角）　`Ctrl/Cmd + F`：隔（F 是"分"的声母）　`Esc`：退出模式　`Ctrl/Cmd + Y`：重做
- 字体缩放与笔刷互不干扰：`Ctrl/Cmd + = / - / 0` 缩放字号（按住加速）；`Ctrl/Cmd + Shift + = / - / 0` 调节笔刷（初始 1.5 × 字号，之后独立）；涂/擦模式下按住 `Shift` 拖动 = 按住笔刷（触控板只负责移动）
- 画布层与文字分层：涂/擦只碰笔迹、空/撤销只碰文字；笔迹随文字滚动、每笔落笔时锁定笔宽（缩放不影响已画内容）；切换阴/阳/显时笔迹随文字变色；笔迹与文字统一撤销/重做（`Ctrl/Cmd + Z / Y`，上限 100 步）；打字为 I 形光标，涂/擦模式以画布足迹代替系统光标（实心墨点=笔刷直径，空心圆=擦除直径，随阴/阳变色，无尺寸上限）
- `Ctrl/Cmd + 滚轮`、触控板捏合：缩放；触控板横向平移 / `Shift + 滚轮`：横向滚动
- **显**是单一模式、零 UI：一键把屏幕变成 1982 年 Osborne Executive 那类琥珀磷光显像管——缝合怪像素字体（Fusion Pixel，OFL）、扫描线、文字辉光、磷粉余晖（打字留下短暂残影）、噪声灰尘、玻璃暗角与入场暖机脉冲。无选项、无声音、无频闪；与编、阴/阳正交可叠加，笔迹与行号都跟随磷色
- 言/隔是批量校对搭档：**言**给选区每一行头尾加「」（空行跳过、一步撤销、之后整段保持选中），**隔**让选中的**每一行**都像单选那样上下各隔出一个空行（逐行隔离，已是空行则不重复加、一步撤销）——先言后隔，逐行核对
- 启动跟随系统深浅色；选过阴/阳后本次会话生效，关闭即忘
- 光标：亮 750ms / 灭 750ms 的慢闪，完整闪烁一次后休眠（输入即唤醒，无残拍）；滚动条交互时淡入、闲置约 1.5 秒淡出（与光标同拍）、悬停把手由 10px 长满 18px
- 窗口边缘自由拉伸，系统原生全屏（macOS 绿灯 / Windows、Linux 最大化）

## 安装

**一条指令（macOS，无需开发环境）**：

    curl -fsSL https://raw.githubusercontent.com/2liver/naught/main/install/one.sh | sh

- 自定义安装位置：`… | sh -s -- <目录>`；只有默认的 `~/Applications` 会出现在启动台（macOS 只索引这个目录和 `/Applications`）。
- 无法访问 GitHub 时先设置代理，例如 `export https_proxy=http://127.0.0.1:7897`。

**Windows**（PowerShell 一条指令，装到 `%LOCALAPPDATA%\Naught`）：

    $d="$env:LOCALAPPDATA\Naught"; iwr -useb https://github.com/2liver/naught/releases/latest/download/naught-windows.zip -OutFile $env:TEMP\naught.zip; Expand-Archive $env:TEMP\naught.zip $d -Force; Start-Process "$d\naught.exe"

之后可自行把 `naught.exe` 的快捷方式放到桌面、开始菜单或任务栏。

**Linux**（AppImage，单文件）：

    curl -fsSL https://github.com/2liver/naught/releases/latest/download/naught-linux.AppImage -o ~/.local/bin/naught && chmod +x ~/.local/bin/naught

也可到 [Releases](../../releases) 手动下载 dmg / zip / AppImage。

## 从源码构建（需要 Qt 6 + CMake；语法高亮为可选依赖 KF6 SyntaxHighlighting）

    git clone --depth 1 https://github.com/KDE/extra-cmake-modules.git
    git clone --depth 1 https://github.com/KDE/syntax-highlighting.git
    # 两者按常规 CMake 安装到同一前缀（如 ~/kf6），构建时把该前缀加入 CMAKE_PREFIX_PATH

## 从源码构建（需要 Qt 6 + CMake）

    cmake -S . -B build
    cmake --build build

Qt 不在默认路径时：`-DCMAKE_PREFIX_PATH=<Qt 安装目录>/<版本>/<编译器>`。
macOS 装进启动台：`./install.sh`。

## 设计边界

- **桌面专用**（macOS / Windows / Linux）；不做移动端与 Web 端——无界面、无存储的空白纸在手机触屏上没有栖息地。
- **关闭即无**：不保存、不恢复、不记忆上一次会话；只有本次会话内的阴/阳与撤销历史。

## 性能

- `--selftest`：几何与行为回归门禁，CI 三平台强制通过。
- `--bench`：本地基准（离屏软件光栅；真机走 GPU 合成，数值更优）：
  - 多行文档（2000 行）打字 ≈ 0.5 ms/键；缩放 ≈ 0.2 ms/次；全行程滚动 ≈ 3 ms/步；编模式行号重绘 ≈ 3 ms；100 笔笔迹 ≈ 10 ms。
  - 显模式（离屏最坏值）：打字 ≈ 0.2 ms/键；缩放 ≈ 0.3 ms/次；滚动 ≈ 5 ms/步。
  - **已知边界**：单行十万字（巨型粘贴）时按键 ≈ 100 ms/次——QPlainTextEdit 的块级换行重排代价与行宽成正比，属 Qt 控件固有特性；正常换行的写作不受影响。

## 图标

图标为**思源宋体 Heavy（Source Han Serif SC Heavy）**渲染的「無」字，黑底白字
（SIL OFL 1.1 授权，见 `resources/icon/OFL.txt`）。已生成 `naught.icns`（macOS）、
`naught.ico`（Windows 可执行文件）、`icons/naught.png`（运行时窗口图标）。

重新生成：下载 `SourceHanSerifSC-Heavy.otf`（[adobe-fonts/source-han-serif](https://github.com/adobe-fonts/source-han-serif) 的 2.003R 发布包）后：

    cmake --build build --target naught_icon_tool
    ./build/naught_icon_tool <SourceHanSerifSC-Heavy.otf> build/icons

再把 `build/icons` 下的 `naught.icns`、`naught.ico`、`naught.png` 放回
`resources/` 与 `resources/icons/` 对应位置。

## 许可证

MIT © 2026 2liver（见 `LICENSE`）。图标所用字体的协议见 `resources/icon/OFL.txt`。
「显」模式内置的缝合怪像素字体（Fusion Pixel）以 SIL OFL 1.1 授权，
许可文本见 `resources/fonts/FusionPixel-OFL.txt`。
