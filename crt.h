// crt.h —— 「显」显像管层：零 UI、单一琥珀磷光模式、纯画面拟真。
// 架构（二期修复后）：视口不透明（Base=kBg），所有光效为文字上方
// 的纯 SourceOver 层。真机的窗口合成器对透明视口会产生未初始化
// 内存的"绿洞"（render/grab 亦全空），透明架构已整体废除。
// 文字快照由 Editor::paintTextSnapshot 自绘（块走查几何，引擎无关）。
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>

class Editor;

namespace Crt {
// 调色板（M2）：一台机器一套磷光——文字、行号、笔迹、块光标、底色，
// 以及 shader 侧的扫描线掺色、玻璃反光色、灰尘色。加机器 = 加预设。
struct Palette {
    QColor ink;         // 磷粉核心亮色
    QColor inkDim;      // 暗磷（行号等次要元素）
    QColor cursorBlock; // 炽磷块光标（满格激发）
    QColor bg;          // 近黑底
    QColor scanTint;    // 扫描线暗行掺色（归一化 RGB 送 shader）
    QColor refl;        // 玻璃反光色
    QColor dust;        // 灰尘点色
    qreal glowAlpha;    // 辉光叠加强度（1a7s39ge 实例参考）
    // 余晖实测标定：双指数快/慢分量的逐通道帧间残留（按各磷粉
    // P1/P3/P4/P22 的实测衰减时标换算到 80ms 快照帧率——真机余晖
    // 一两帧内散尽，旧版拖尾是风格化不是拟真）
    qreal persist1[3] = { 0.08f, 0.05f, 0.02f }; // 快分量（B,G,R 旧约定）
    qreal persist2[3] = { 0.12f, 0.08f, 0.03f }; // 慢分量
};

// 琥珀（Osborne Executive 1982）：出厂机器
inline const Palette kAmber{
    QColor(0xFF, 0xB0, 0x00), // ink
    QColor(0x8C, 0x5E, 0x00), // inkDim
    QColor(0xFF, 0xE2, 0xA0), // cursorBlock
    QColor(0x0C, 0x09, 0x03), // bg
    QColor(0x59, 0x66, 0x40), // scanTint (0.35,0.4,0.25)
    QColor(0xFF, 0xE0, 0x9E), // refl (1.0,0.88,0.62)
    QColor(0xE6, 0xCC, 0x99), // dust (0.9,0.8,0.6)
    0.42,                     // glowAlpha
    { 0.35f, 0.25f, 0.10f },  // P3 琥珀余晖：中余晖，10% 点 ~60ms
    { 0.15f, 0.10f, 0.04f },
};

// 绿磷（IBM 5100 1975）：按 1a7s39ge 实例「终端绿」主题校准
// （bg #0a0a0a / text #33ff33 / textDim #1a8a1a / cursor #00ff00 /
//  glow rgba(0,255,0,0.35)）
inline const Palette kGreen{
    QColor(0x33, 0xFF, 0x33), // ink
    QColor(0x1A, 0x8A, 0x1A), // inkDim（行号暗绿）
    QColor(0x00, 0xFF, 0x00), // cursorBlock（纯绿，实例同款）
    QColor(0x0A, 0x0A, 0x0A), // bg（中性近黑，实例同款）
    QColor(0x40, 0x66, 0x59), // scanTint (0.25,0.4,0.35)
    QColor(0xBF, 0xEA, 0xCC), // refl (0.75,0.92,0.8)
    QColor(0xB3, 0xE6, 0xBF), // dust (0.7,0.9,0.75)
    0.35,                     // glowAlpha（实例同款）
    { 0.18f, 0.22f, 0.12f },  // P1 绿磷余晖：中短，10% 点 ~30ms
    { 0.06f, 0.09f, 0.04f },
};

// C64（Commodore 64 1982）：真彩机型——蓝屏 + 16 色逐字符前景色
// （chrome 调色板按子代理调研：默认浅蓝字 #6F7FDC 于蓝屏 #2A1C6E）。
// 字符画的颜色来自 kC64Colors 的 16 色量化。
inline const Palette kC64{
    // 亮度校准（lemon64 社区 + 1702 实测讨论）：真机"浅蓝字"是电子束
    // 饱和后的近白亮蓝（名义 #6C6CEB 只是色度），亮度级接近琥珀/绿磷
    // 的激发水平；底是深蓝（#2114B9 系），对比强烈——旧版墨不够亮、
    // 底太浅，观感发闷
    QColor(0xE6, 0xF0, 0xFF), // ink（饱和亮蓝，≈白蓝的激发亮度）
    QColor(0x96, 0xA4, 0xD4), // inkDim
    QColor(0xE4, 0xEA, 0xFF), // cursorBlock（近白）
    QColor(0x18, 0x10, 0x70), // bg（深蓝 #2114B9 系：实测真机底 ≈ 亮度 8-12，旧值 30 过亮）
    QColor(0x18, 0x10, 0x4A), // scanTint
    QColor(0x60, 0x68, 0xE8), // refl
    QColor(0x6E, 0x78, 0x9E), // dust
    0.50,                     // glowAlpha（蓝磷提亮：辉光也是亮度的一部分）
    { 0.08f, 0.08f, 0.07f },  // P22 彩管余晖：快分量 µs 级（帧级几乎不可见），慢分量短
    { 0.16f, 0.16f, 0.12f },  // 慢分量：P22 实有可见慢余晖（整体亮度的一部分）
};

// C64 标准 16 色（字符画逐字符前景色的量化目标）
inline const QColor kC64Colors[16] = {
    QColor(0x00, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF), QColor(0x88, 0x39, 0x32),
    QColor(0x67, 0xB6, 0xBD), QColor(0x8B, 0x3F, 0x96), QColor(0x55, 0xA0, 0x49),
    QColor(0x40, 0x31, 0x8D), QColor(0xBF, 0xCE, 0x72), QColor(0x8B, 0x54, 0x29),
    QColor(0x57, 0x42, 0x00), QColor(0xB8, 0x69, 0x62), QColor(0x50, 0x50, 0x50),
    QColor(0x78, 0x78, 0x78), QColor(0x94, 0xE0, 0x89), QColor(0x78, 0x69, 0xC4),
    QColor(0x9F, 0x9F, 0x9F),
};

// IBM PC 5150（1981）：CGA 白字模式——白磷真机（80×25）。
// 字形用 Fixedsys Excelsior（CC0 公有领域，经典字库已捆绑）精确重绘。
inline const Palette kWhite{
    QColor(0xE8, 0xE8, 0xE0), // ink（暖白磷光）
    QColor(0x8A, 0x8A, 0x80), // inkDim
    QColor(0xFF, 0xFF, 0xFF), // cursorBlock（纯白满束流）
    QColor(0x05, 0x05, 0x05), // bg（CGA 黑）
    QColor(0x4A, 0x4A, 0x50), // scanTint（中性微冷）
    QColor(0xE0, 0xE0, 0xD8), // refl
    QColor(0xC0, 0xC0, 0xB8), // dust
    0.38,                     // glowAlpha
    { 0.22f, 0.22f, 0.20f },  // P4 白磷余晖：中短
    { 0.09f, 0.09f, 0.08f },
};

// 兼容别名（出厂琥珀；自检黄金参考沿用）
inline const QColor kInk = kAmber.ink;
inline const QColor kInkDim = kAmber.inkDim;
inline const QColor kCursorBlock = kAmber.cursorBlock;
inline const QColor kBg = kAmber.bg;
inline constexpr int kScanPeriod = 3;            // 扫描线周期 px（与 shader 注释对齐）
inline constexpr qreal kDiffAlpha = 0.30;        // 衍射彩边强度（与 shader 注释对齐）

// 竖直边差分：bright(x) − bright(x±1) > 0 处即竖直亮边。
// 磷粉三色栅是 N=3 的竖直二元光栅：只竖直边衍射，sinc 包络把彩边
// 压到 1~2px，R/B 错位 ±1px、色相相反（0/π 相位）。返回 alpha 掩膜。
// 注意：ARGB32 内存布局为 BGRA，直接用字节寻址（QRgb* 重解释会错位）。
QImage edgeDiff(const QImage &src, int dx);

// 分离盒式模糊一轮（水平）；三轮 H+V 近似高斯。同样用字节寻址。
void boxBlurH(const QImage &src, QImage &dst, int radius);

void boxBlurV(const QImage &src, QImage &dst, int radius);

QImage gaussianBlur(const QImage &src, int radius, int iterations);

// 磷粉余晖（一期·回接，M4 双指数）：上一帧与上上帧以快慢两个分量
// 加法混入新帧——快分量 = 1 帧内的亮回响，慢分量 = 长尾余热（真磷粉
// 的双指数衰减近似）。分通道权重：红磷拖尾最长、绿次之、蓝最快，
// 残影因此偏暖。静态画面微微增亮（磷粉永不完全熄灭）。
// 注：已整体 GPU 化（shaders/persist.frag，lighten-max + 按时间衰减
// pow(persist, dt/80ms)）。本实现保留为黄金参考（模型与系数同源）。
void phosphorPersistence(QImage &img, const QImage &prev1, const QImage &prev2, const Palette &pal);

// 磷光辉光（二期三件套·回接）：1/4 降采样往返 + 三轮分离盒式模糊
// （真高斯形状），再以 lighten（max）叠回——文字核心保持全亮、
// 四周长出磷粉光晕。整图字节直写，绕开 QImage 画笔的引擎层 DPR
// 二次缩放；只在快照重建（80ms 节流）时执行。
// 注：已整体 GPU 化（downsample.frag + blurH/blurV.frag + crt.frag 的
// 线性 max 叠加）。本实现保留为黄金参考（模型与系数同源）。
void phosphorBloom(QImage &img, qreal alpha = 0.42);
} // namespace Crt
