// crt.h —— 「显」显像管层：零 UI、单一琥珀磷光模式、纯画面拟真。
// 两层：CrtBackdrop（视口之下：磷底 + 辉光快照）与 CrtOverlay
// （最上：扫描线 / 噪声 / 暗角玻璃 / 暖机脉冲）。全部同生命周期可逆。
// 实现见 crt.cpp（需要 Editor 完整类型）。
#pragma once

#include <QColor>
#include <QImage>
#include <QTimer>
#include <QWidget>

class Editor;

namespace Crt {
inline const QColor kInk(0xFF, 0xB0, 0x00);      // 磷粉核心亮色（琥珀）
inline const QColor kInkDim(0x8C, 0x5E, 0x00);   // 暗磷（行号等次要元素）
inline const QColor kBg(0x0C, 0x09, 0x03);       // 近黑暖底
inline constexpr int kScanPeriod = 3;            // 扫描线周期 px
inline constexpr int kGlowMinScroll = 24;        // 滚动超过该位移才追辉光
inline constexpr qint64 kGlowMinIntervalMs = 80; // 辉光刷新最小间隔
} // namespace Crt

// 辉光层：视口快照降采样模糊成磷光辉光，垫在透明视口之下。
// 滚动小位移时辉光短暂滞后（磷粉惰性，正是要的味道），超位移或内容脏时追上。
class CrtBackdrop : public QWidget {
public:
    explicit CrtBackdrop(Editor *editor);
    void invalidateGlow();
    void refreshGlow();
    // 缩放等必须立即重拍的场景：绕过打字节流
    void forceGlow();
    QImage glowImage() const { return m_glow; }
    QPoint glowScroll() const { return m_glowScroll; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Editor *m_editor = nullptr;
    QImage m_glow;
    QPoint m_glowScroll;
    QElapsedTimer m_sinceRefresh;
    bool m_dirty = true;
    bool m_forceRefresh = false;
    // 残影：旧光晕留在原屏幕位置渐暗熄灭（灯泡慢慢灭，不是开关）
    QImage m_ghost;
    QPointF m_ghostPos;
    qreal m_ghostAlpha = 0.0;
    QTimer m_fadeTimer;
};

// 效果层：扫描线 + 噪声 + 暗角玻璃 + 暖机脉冲。事件穿透。
class CrtOverlay : public QWidget {
public:
    explicit CrtOverlay(Editor *editor);
    void warmUp();
    void stop();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuildGlass();

    Editor *m_editor = nullptr;
    QImage m_scanMask;
    QImage m_noise[2];
    int m_noiseFrame = 0;
    QTimer m_noiseTimer;
    QImage m_glass;
    qreal m_warm = 0.0;
    qreal m_bandPhase = 0.0; // 滚动刷新带相位（0..1，3 秒一周）
    QTimer m_warmTimer;
};
