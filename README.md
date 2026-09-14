# 無 (naught)

*Write in vain.*

空白。打开即写，关闭即无。

- 右键 / 双指点按：**摹**（全选并复制）· **空**（清空，可撤销）── **阴** · **阳** ── **涂**（画）· **擦**（橡皮擦）· **消**（清空全部笔迹）── **编**（代码：等宽字体 + 行号 + 语法高亮，无运行无保存）
- `Ctrl/Cmd + S`：摹　`Ctrl/Cmd + N`：空　`Ctrl/Cmd + I`：阴　`Ctrl/Cmd + O`：阳　`Ctrl/Cmd + D`：涂　`Ctrl/Cmd + E`：擦　`Ctrl/Cmd + Shift + E`（或 `Shift + 空格`）：消　`Ctrl/Cmd + B`：编　`Esc`：退出模式　`Ctrl/Cmd + Y`：重做
- 字体缩放与笔刷互不干扰：`Ctrl/Cmd + = / - / 0` 缩放字号（按住加速）；`Ctrl/Cmd + Shift + = / - / 0` 调节笔刷（初始 1.5 × 字号，之后独立）
- 画布层与文字分层：涂/擦只碰笔迹、空/撤销只碰文字；笔迹随文字滚动、每笔落笔时锁定笔宽（缩放不影响已画内容）；切换阴/阳时笔迹随文字变色；笔迹无撤销；打字为 I 形光标，涂/擦模式以画布足迹代替系统光标（实心墨点=笔刷直径，空心圆=擦除直径，随阴/阳变色，无尺寸上限）
- `Ctrl/Cmd + 滚轮`、触控板捏合：缩放；触控板横向平移 / `Shift + 滚轮`：横向滚动
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
