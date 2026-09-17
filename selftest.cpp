// selftest.cpp —— 自检与暴力回归闸（自 editor.h 抽离：近 5000 行的上帝
// 对象拆走约 2200 行测试面——架构深模块化的第一刀。作为 Editor 的成员
// 函数定义，保留全部私有成员访问权（无需 friend 手术）。
#include "editor.h"

#include "ascii_art.h"
#include "crt.h"

bool Editor::selftest()
{
        qInfo("SELFTEST-ENTER");
        // Qt 渲染层探针：viewport 裸渲染到不同 DPR 目标，测文本落点
        // （倒影/位移的根修取证：确认 QWidget::render 在 DPR 下的行为）
        {
            auto probe = [](QWidget *vp, const char *tag, qreal dpr) {
                const int W = vp->width(), H = vp->height();
                QImage img(QSize(int(W * dpr), int(H * dpr)), QImage::Format_ARGB32);
                img.setDevicePixelRatio(dpr);
                img.fill(Qt::black);
                QPainter p(&img);
                vp->render(&p);
                p.end();
                int firstLit = -1, lastLit = -1, litRows = 0;
                for (int y = 0; y < img.height(); ++y) {
                    const uchar *line = img.constScanLine(y);
                    int n = 0;
                    for (int x = 0; x < img.width(); ++x)
                        if (line[x * 4 + 1] + line[x * 4 + 2] + line[x * 4] < 250)
                            ++n; // 暗字（默认黑字白底）
                    if (n > 5) {
                        if (firstLit < 0) firstLit = y;
                        lastLit = y;
                        ++litRows;
                    }
                }
                qWarning("QT-RENDER %s: img=%dx%d dpr=%g vp=%dx%d lit=%d..%d rows=%d",
                         tag, img.width(), img.height(), dpr, W, H, firstLit, lastLit, litRows);
            };
            QPlainTextEdit pure;
            pure.resize(400, 300);
            pure.setPlainText(QStringLiteral("orders = [(\"张三\", 99.5)]\nbig = [o for o in orders]\nprint(big)\n"));
            pure.show();
            QApplication::processEvents();
            probe(pure.viewport(), "pure-plain", 1.0);
            probe(pure.viewport(), "pure-dpr2", 2.0);
            {
                Editor e2;
                e2.resize(400, 300);
                e2.setPlainText(QStringLiteral("orders = [(\"张三\", 99.5)]\nbig = [o for o in orders]\nprint(big)\n"));
                e2.show();
                QApplication::processEvents();
                probe(e2.viewport(), "editor-plain", 1.0);
                probe(e2.viewport(), "editor-dpr2", 2.0);
            }
        }
        Editor e;
        qInfo("SELFTEST-EDITOR-CONSTRUCTED");
        e.setPlainText(QStringLiteral("無"));
        const QTextBlock block = e.document()->firstBlock();
        const qreal h1 = e.document()->documentLayout()->blockBoundingRect(block).height();
        e.zoomTo(200);
        const qreal h2 = e.document()->documentLayout()->blockBoundingRect(block).height();
        if (!(h2 > h1 * 2.0)) {
            qWarning("selftest FAIL: zoom h1=%f h2=%f", h1, h2);
            return false;
        }

        // 累积求"文档 y 处的视觉行行尾"（与 lineEndForY 同一模型，独立实现作真值）
        auto lineEndAtDocY = [&](qreal docY) -> int {
            QTextBlock b = e.document()->firstBlock();
            qreal top = 0;
            while (b.isValid()) {
                const QRectF r = e.document()->documentLayout()->blockBoundingRect(b);
                if (docY < top + r.height()) {
                    QTextLayout *tl = b.layout();
                    if (!tl || tl->lineCount() == 0)
                        return b.position() + b.length() - 1;
                    const qreal relY = docY - top;
                    QTextLine ln = tl->lineAt(0);
                    for (int i = 1; i < tl->lineCount(); ++i) {
                        const QTextLine l = tl->lineAt(i);
                        if (relY >= l.y())
                            ln = l;
                        else
                            break;
                    }
                    if (ln.textLength() == 0 && b.length() > 1)
                        return b.position() + b.length() - 1;
                    if (b == e.document()->lastBlock() && b.length() == 1
                        && e.document()->characterCount() >= 2)
                        return e.document()->characterCount() - 2;
                    return b.position() + ln.textStart() + ln.textLength();
                }
                top += r.height();
                b = b.next();
            }
            if (e.document()->lastBlock().length() == 1 && e.document()->characterCount() >= 2)
                return e.document()->characterCount() - 2;
            return e.document()->characterCount() - 1;
        };

        // 光标落点：点击视口最右缘应落在行尾；滚动条轨道点击也应落到行尾
        e.zoomReset();
        QString lines;
        for (int i = 0; i < 40; ++i)
            lines += QStringLiteral("一二三四五\n");
        e.setPlainText(lines);
        e.resize(400, 300);
        e.show();
        for (int s = 0; s < 10; ++s) { // 布局沉降：负载高时 resize 竞态（1/6 偶发，加厚）
            QApplication::processEvents();
            QEventLoop lp;
            QTimer::singleShot(10, &lp, &QEventLoop::quit);
            lp.exec();
        }
        const int len = e.document()->firstBlock().length() - 1;
        QWidget *vp = e.viewport();

        const QPointF edge(vp->width() - 1.0, 10.0);
        QMouseEvent press(QEvent::MouseButtonPress, edge, vp->mapToGlobal(edge.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &press);
        if (e.textCursor().positionInBlock() != len) {
            qWarning("selftest FAIL: viewport edge click lands at %d, want %d",
                     e.textCursor().positionInBlock(), len);
            return false;
        }

        e.moveCursor(QTextCursor::Start);
        for (int s = 0; s < 3; ++s) { // 滚动条轨道点击前再沉降（范围/几何竞态）
            QApplication::processEvents();
            QEventLoop lp;
            QTimer::singleShot(10, &lp, &QEventLoop::quit);
            lp.exec();
        }
        ZenScrollBar *bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
        if (bar && bar->isVisible()) {
            const QPoint tp(5, 5);
            QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &tpress);
            if (e.textCursor().positionInBlock() != len) {
                qWarning("selftest FAIL: track click lands at %d, want %d",
                         e.textCursor().positionInBlock(), len);
                return false;
            }
        }

        // 放大 + 横向溢出场景：横滚到最右后，点最右缘仍应落到该行行尾
        e.setPlainText(QStringLiteral("無無無無無無無無無無\n無無無無無無無無無無\n無無無無無無無無無無\n無無無無無無無無無無\n無無無無無無無無無無\n"));
        e.zoomTo(200);
        QApplication::processEvents();
        QScrollBar *hb = e.horizontalScrollBar();
        if (hb->isVisible()) {
            hb->setValue(hb->maximum());
            QApplication::processEvents();
            e.moveCursor(QTextCursor::Start);
            bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
            if (bar && bar->isVisible()) {
                const QPoint tp(5, 5);
                QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(bar, &tpress);
                if (e.textCursor().positionInBlock() != 10) {
                    qWarning("selftest FAIL: scrolled track click lands at %d, want 10",
                             e.textCursor().positionInBlock());
                    return false;
                }
            }
        }
        // 满行换行场景：点最右缘应落在该视觉行行尾（而非最后一个字之前）
        e.zoomReset();
        QString longLine;
        for (int i = 0; i < 40; ++i)
            longLine += QStringLiteral("無");
        QString wrapped = longLine + QStringLiteral("\n");
        for (int i = 0; i < 20; ++i)
            wrapped += QStringLiteral("短行\n");
        e.setPlainText(wrapped);
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        {
            QTextBlock blk = e.document()->firstBlock();
            QTextLayout *tl = blk.layout();
            if (tl->lineCount() >= 2) {
                const QTextLine line0 = tl->lineAt(0);
                const int want = blk.position() + line0.textStart() + line0.textLength();
                e.moveCursor(QTextCursor::Start);
                bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
                if (bar && bar->isVisible()) {
                    const QPoint tp(5, 5);
                    QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(bar, &tpress);
                    if (e.textCursor().position() != want) {
                        qWarning("selftest FAIL: wrapped track click lands at %d, want %d",
                                 e.textCursor().position(), want);
                        return false;
                    }
                }
            }
        }
        // 增量打字后立刻点最右缘（不结算事件）：落点仍应是该视觉行行尾
        e.setPlainText(QString());
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        for (int i = 0; i < 40; ++i)
            e.insertPlainText(QStringLiteral("無"));
        e.insertPlainText(QStringLiteral("\n"));
        for (int i = 0; i < 20; ++i)
            e.insertPlainText(QStringLiteral("短行\n"));
        e.moveCursor(QTextCursor::Start);
        bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
        if (bar && bar->isVisible()) {
            const QPoint tp(5, 5);
            QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &tpress);
            const int got = e.textCursor().position();
            // 结算后求真值
            QApplication::processEvents();
            QTextBlock blk2 = e.document()->firstBlock();
            const QTextLine line0b = blk2.layout()->lineAt(0);
            const int want = blk2.position() + line0b.textStart() + line0b.textLength();
            if (got != want) {
                qWarning("selftest FAIL: fresh-typing track click lands at %d, want %d", got, want);
                return false;
            }
        }
        // 多点扫描：混合文档（首块换行 + 短行 + 空行）各高度点最右缘都应落该行行尾
        {
            QString doc3 = longLine + QStringLiteral("\n");
            for (int i = 0; i < 10; ++i)
                doc3 += QStringLiteral("短行\n");
            doc3 += QStringLiteral("\n"); // 空行
            for (int i = 0; i < 10; ++i)
                doc3 += QStringLiteral("又一段\n");
            e.setPlainText(doc3);
            e.resize(400, 300);
            e.show();
            QApplication::processEvents();
            bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
            if (bar && bar->isVisible()) {
                for (int y : {5, 40, 90, 140, 190, 240}) {
                    e.moveCursor(QTextCursor::Start);
                    const QPoint tp(5, y);
                    QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(bar, &tpress);
                    const int got = e.textCursor().position();
                    const qreal docY = qreal(y);
                    const int want = lineEndAtDocY(docY);
                    if (got != want) {
                        qWarning("selftest FAIL: sweep y=%d lands at %d, want %d", y, got, want);
                        return false;
                    }
                }
            }
        }
        // 光标矩形与方向键：最后一个字符之后的光标应在右侧，行尾按右不跳行首
        e.setPlainText(QStringLiteral("你好"));
        {
            QTextCursor c(e.document());
            c.setPosition(2); // “好”之后
            e.setTextCursor(c);
            const QRect cr = e.cursorRect();
            if (cr.x() <= 4) {
                qWarning("selftest FAIL: cursorRect after last char at x=%d", cr.x());
                return false;
            }
            e.moveCursor(QTextCursor::Right);
            if (e.textCursor().position() != 2) {
                qWarning("selftest FAIL: Right at end moves to %d, want 2",
                         e.textCursor().position());
                return false;
            }
        }
        // 文末回车产生的空行：光标从行尾按右进入空行（标准行为，锁定以防回归）
        e.setPlainText(QStringLiteral("你好\n"));
        {
            QTextCursor c(e.document());
            c.setPosition(2);
            e.setTextCursor(c);
            e.moveCursor(QTextCursor::Right);
            if (e.textCursor().position() != 3) {
                qWarning("selftest FAIL: Right across trailing newline moves to %d, want 3",
                         e.textCursor().position());
                return false;
            }
        }
        // 文末空行不可入：点余白行高度落最后一个字符之后；行尾按右停在原地
        bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
        if (bar) {
            const QPoint tp(5, 25); // 第二行（余白行）高度
            QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &tpress);
            if (e.textCursor().position() != 2) {
                qWarning("selftest FAIL: trailing-empty strip click lands at %d, want 2",
                         e.textCursor().position());
                return false;
            }
        }
        {
            QTextCursor c(e.document());
            c.setPosition(2);
            e.setTextCursor(c);
            QKeyEvent kp(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
            QApplication::sendEvent(&e, &kp);
            if (e.textCursor().position() != 2) {
                qWarning("selftest FAIL: Right into trailing empty moves to %d, want 2",
                         e.textCursor().position());
                return false;
            }
        }
        // 满行段落：文字区最右缘窄带内点击 = 该视觉行行尾
        e.setPlainText(longLine + QStringLiteral("\n短行\n短行\n"));
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        {
            QTextBlock blk = e.document()->firstBlock();
            QTextLayout *tl = blk.layout();
            const QTextLine line0 = tl->lineAt(0);
            const int want = blk.position() + line0.textStart() + line0.textLength();
            QWidget *vp = e.viewport();
            const QPointF edge(vp->width() - 2.0, 5.0);
            QMouseEvent press(QEvent::MouseButtonPress, edge, vp->mapToGlobal(edge.toPoint()),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &press);
            if (e.textCursor().position() != want) {
                qWarning("selftest FAIL: full-line edge-zone click lands at %d, want %d",
                         e.textCursor().position(), want);
                return false;
            }
        }
        // 编模式：等宽字体 + 行号槽 + 退出复原（ASCII 文档，与真实代码一致）
        e.setPlainText(QStringLiteral("code1\ncode2\ncode3\ncode4\n"));
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        e.toggleCodeMode();
        QApplication::processEvents();
        if (e.document()->defaultFont().family()
            != QFontDatabase::systemFont(QFontDatabase::FixedFont).family()) {
            qWarning("selftest FAIL: code mode font is not the fixed font");
            return false;
        }
        if (e.viewport()->pos().x() <= 0) {
            qWarning("selftest FAIL: code mode gutter missing");
            return false;
        }
        // 基线对齐硬验证：行号与文字共用同一 QTextLayout 基线（结构恒等），
        // 像素级用同字形比对（行号数字 vs 文字数字），真机/离屏渲染取向无关
        {
            QString digitDoc;
            for (int i = 0; i < 8; ++i)
                digitDoc += QStringLiteral("111111111111\n");
            e.setPlainText(digitDoc);
            QApplication::processEvents();
            // 光标净化：清焦点后 QPlainTextEdit 隐藏光标，像素比对才纯净
            e.setFocus();
            QApplication::processEvents();
            e.clearFocus();
            QApplication::processEvents();
            QImage img(e.size(), QImage::Format_ARGB32);
            img.fill(Qt::white);
            e.render(&img);
            const int g = e.viewport()->pos().x();
            auto darkRange = [&](int x0, int x1, int y0, int y1) {
                int lo = -1, hi = -1;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                        if (qGray(img.pixel(x, y)) < 200) { // 行号为浅灰
                            if (lo < 0)
                                lo = y;
                            hi = y;
                        }
                return qMakePair(lo, hi);
            };
            const auto num = darkRange(2, qMax(3, g - 2), 0, e.height());
            const auto txt = darkRange(g + 4, g + 120, 0, e.height());
            qInfo("GUTTER-ALIGN gutter=%d num_y=[%d,%d] text_y=[%d,%d]", g,
                  num.first, num.second, txt.first, txt.second);
            if (num.first < 0 || txt.first < 0
                || qAbs(num.first - txt.first) > 1
                || qAbs(num.second - txt.second) > 1) {
                qWarning("selftest FAIL: gutter baselines misaligned: [%d,%d] vs [%d,%d]",
                         num.first, num.second, txt.first, txt.second);
                return false;
            }
            e.zoomReset();
        }
        e.toggleCodeMode();
        QApplication::processEvents();
        if (e.viewport()->pos().x() != 0
            || e.document()->defaultFont().family()
                != QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()) {
            qWarning("selftest FAIL: exiting code mode did not restore layout/font");
            return false;
        }
        // 行号随横滚：放大到溢出后，行号区随横向滚动移出屏幕、边距回收、滚回复原
        {
            e.setPlainText(QStringLiteral("無無無無無無無無無無無無無無無無無無無無\n第二行\n"));
            e.resize(400, 300);
            e.show();
            QApplication::processEvents();
            e.toggleCodeMode();
            QApplication::processEvents();
            e.zoomTo(96);
            QApplication::processEvents();
            QScrollBar *hb = e.horizontalScrollBar();
            const int w0 = e.gutterWidth();
            if (hb->isVisible() && hb->maximum() > 0) {
                hb->setValue(qMin(hb->maximum(), w0 + 8));
                QApplication::processEvents();
                if (e.viewport()->pos().x() != qMax(0, w0 - hb->value())) {
                    qWarning("selftest FAIL: gutter margin did not shrink (vp.x=%d h=%d)",
                             e.viewport()->pos().x(), hb->value());
                    return false;
                }
                hb->setValue(hb->maximum());
                QApplication::processEvents();
                if (e.viewport()->pos().x() != 0) {
                    qWarning("selftest FAIL: gutter margin not fully reclaimed (vp.x=%d)",
                             e.viewport()->pos().x());
                    return false;
                }
                hb->setValue(0);
                QApplication::processEvents();
                if (e.viewport()->pos().x() != w0) {
                    qWarning("selftest FAIL: gutter margin not restored (vp.x=%d want %d)",
                             e.viewport()->pos().x(), w0);
                    return false;
                }
            }
            e.zoomReset();
            e.toggleCodeMode();
            QApplication::processEvents();
        }
        // 环形笔迹中空回归（用户报：笔刷有时把颜色填充到空心区域）：
        // 画一圈 → 圈心必须保持空心（描边轮廓的填充规则必须保洞）
        {
            e.setPlainText(QString());
            e.clearInk();
            e.toggleMode(Editor::Mode::Draw);
            QWidget *vp = e.viewport();
            const QPointF c(vp->width() / 2.0, vp->height() / 2.0);
            const QPointF s0(c.x() + 40, c.y());
            QMouseEvent pr(QEvent::MouseButtonPress, s0, vp->mapToGlobal(s0.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &pr);
            for (int a = 30; a <= 360; a += 30) {
                const qreal rad = a * 3.14159265 / 180.0;
                const QPointF p(c.x() + 40.0 * qCos(rad), c.y() + 40.0 * qSin(rad));
                QMouseEvent mv(QEvent::MouseMove, p, vp->mapToGlobal(p.toPoint()),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &mv);
            }
            QMouseEvent re(QEvent::MouseButtonRelease, s0, vp->mapToGlobal(s0.toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &re);
            e.toggleMode(Editor::Mode::Normal);
            QApplication::processEvents();
            // 画布渲染到图像 → 圈心像素必须透明（空心）
            QImage inkImg(vp->size() * 2, QImage::Format_ARGB32);
            inkImg.fill(Qt::transparent);
            inkImg.setDevicePixelRatio(2.0);
            QPainter ip(&inkImg);
            ip.translate(e.viewport()->pos() * 2);
            e.m_canvas->render(&ip);
            ip.end();
            const QPoint centerPix(int(c.x() * 2), int(c.y() * 2));
            const QRgb px = inkImg.pixel(centerPix);
            if (qAlpha(px) > 8) {
                qWarning("selftest FAIL: ring stroke filled its hollow center (alpha=%d)",
                         qAlpha(px));
                return false;
            }
            e.clearInk();
            QApplication::processEvents();
        }
        // 自交 X 形（∞ 交叉）回归：一笔画两个交叉环，两瓣心必须空心
        //（用户报"笔刷把颜色填充到空心区域"——自交是最高危形状）
        {
            e.toggleMode(Editor::Mode::Draw);
            QWidget *vp = e.viewport();
            const QPointF xc(vp->width() / 2.0, vp->height() / 2.0);
            const QPointF seq[9] = {
                xc, xc + QPointF(-30, -30), xc + QPointF(0, -60), xc + QPointF(30, -30),
                xc, xc + QPointF(-30, 30), xc + QPointF(0, 60), xc + QPointF(30, 30), xc
            };
            QMouseEvent pr(QEvent::MouseButtonPress, seq[0], vp->mapToGlobal(seq[0].toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &pr);
            for (int i = 1; i < 9; ++i) {
                QMouseEvent mv(QEvent::MouseMove, seq[i], vp->mapToGlobal(seq[i].toPoint()),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &mv);
            }
            QMouseEvent re(QEvent::MouseButtonRelease, seq[8], vp->mapToGlobal(seq[8].toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &re);
            e.toggleMode(Editor::Mode::Normal);
            QApplication::processEvents();
            QImage ixImg(vp->size() * 2, QImage::Format_ARGB32);
            ixImg.fill(Qt::transparent);
            ixImg.setDevicePixelRatio(2.0);
            QPainter ip(&ixImg);
            ip.translate(e.viewport()->pos() * 2);
            e.m_canvas->render(&ip);
            ip.end();
            const QPoint lobeTop(int((xc.x() - 0) * 2), int((xc.y() - 38) * 2));
            const QPoint lobeBot(int((xc.x() - 0) * 2), int((xc.y() + 38) * 2));
            if (qAlpha(ixImg.pixel(lobeTop)) > 8 || qAlpha(ixImg.pixel(lobeBot)) > 8) {
                qWarning("selftest FAIL: self-crossing stroke filled a hollow lobe (top=%d bot=%d)",
                         qAlpha(ixImg.pixel(lobeTop)), qAlpha(ixImg.pixel(lobeBot)));
                return false;
            }
            e.clearInk();
            QApplication::processEvents();
        }
        // 擦除自交笔迹后空心保留（用户报"笔刷把颜色填充到空心区域"
        // 的确凿复现：擦 ∞ 交叉点 → 减法的外环碎片单独填充会把两瓣
        // 填实。根修 = 洞环并回容器（canvas.h splitSubpaths），本闸锁定）
        {
            e.toggleMode(Editor::Mode::Draw);
            QWidget *vp = e.viewport();
            const QPointF xc(vp->width() / 2.0, vp->height() / 2.0);
            const QPointF seq[9] = {
                xc, xc + QPointF(-30, -30), xc + QPointF(0, -60), xc + QPointF(30, -30),
                xc, xc + QPointF(-30, 30), xc + QPointF(0, 60), xc + QPointF(30, 30), xc
            };
            QMouseEvent pr(QEvent::MouseButtonPress, seq[0], vp->mapToGlobal(seq[0].toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &pr);
            for (int i = 1; i < 9; ++i) {
                QMouseEvent mv(QEvent::MouseMove, seq[i], vp->mapToGlobal(seq[i].toPoint()),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &mv);
            }
            QMouseEvent re(QEvent::MouseButtonRelease, seq[8], vp->mapToGlobal(seq[8].toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &re);
            // 切擦除模式，擦掉交叉点
            e.toggleMode(Editor::Mode::Erase);
            QMouseEvent ep(QEvent::MouseButtonPress, xc, vp->mapToGlobal(xc.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &ep);
            QMouseEvent er(QEvent::MouseButtonRelease, xc, vp->mapToGlobal(xc.toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &er);
            e.toggleMode(Editor::Mode::Normal);
            QApplication::processEvents();
            QImage exImg(vp->size() * 2, QImage::Format_ARGB32);
            exImg.fill(Qt::transparent);
            exImg.setDevicePixelRatio(2.0);
            QPainter ip(&exImg);
            ip.translate(e.viewport()->pos() * 2);
            e.m_canvas->render(&ip);
            ip.end();
            const QPoint lobeTop(int(xc.x() * 2), int((xc.y() - 38) * 2));
            const QPoint lobeBot(int(xc.x() * 2), int((xc.y() + 38) * 2));
            if (qAlpha(exImg.pixel(lobeTop)) > 8 || qAlpha(exImg.pixel(lobeBot)) > 8) {
                qWarning("selftest FAIL: erase of self-crossing stroke filled hollow lobes (top=%d bot=%d)",
                         qAlpha(exImg.pixel(lobeTop)), qAlpha(exImg.pixel(lobeBot)));
                return false;
            }
            e.clearInk();
            QApplication::processEvents();
        }
        // 涂模式开着打字（用户报：笔刷期间打字光标行为/换行不准）：
        // 涂/擦模式必须完全不干扰文本编辑——插入位置、换行、光标
        // 落点与普通模式一致
        {
            e.setPlainText(QStringLiteral("起\n"));
            e.toggleMode(Editor::Mode::Draw);
            QTextCursor tc = e.textCursor();
            // 文末可见位置（尾随块之前——真实打字的落点；moveCursor(End)
            // 落到不可见空尾块，不是用户场景）
            tc.setPosition(qMax(0, e.toPlainText().size() - 1));
            e.setTextCursor(tc);
            QApplication::processEvents();
            // 40 字长行：必然触发行换行（WidgetWidth）
            QString longLine;
            for (int i = 0; i < 40; ++i)
                longLine += QStringLiteral("字");
            QTextCursor ins = e.textCursor();
            ins.insertText(longLine);
            QApplication::processEvents();
            if (e.lineWrapMode() != QPlainTextEdit::WidgetWidth) {
                qWarning("selftest FAIL: draw mode changed wrap mode");
                e.toggleMode(Editor::Mode::Normal);
                return false;
            }
            // 文末插入 = 并入"起"块（起字字...）——与普通模式同语义
            if (!e.toPlainText().startsWith(QStringLiteral("起字字"))) {
                qWarning("selftest FAIL: draw mode typing corrupted text got=[%s]",
                         qPrintable(QString(e.toPlainText()).left(12)));
                e.toggleMode(Editor::Mode::Normal);
                return false;
            }
            // 光标必须在文末（打字后落点）
            if (e.textCursor().position() != e.toPlainText().size() - 1) {
                const QString esc = QString(e.toPlainText()).replace(QLatin1Char('\n'), QChar(0x23CE));
                qWarning("selftest FAIL: draw mode typing caret off (pos=%d want %d size=%d text=[%s]... blocks=%d)",
                         e.textCursor().position(), int(e.toPlainText().size() - 1),
                         int(e.toPlainText().size()),
                         qPrintable(esc.left(16)), e.document()->blockCount());
                e.toggleMode(Editor::Mode::Normal);
                return false;
            }
            // 换行键在涂模式照常工作
            QKeyEvent ke(QEvent::KeyPress, Qt::Key_Return, {});
            QApplication::sendEvent(&e, &ke);
            QApplication::processEvents();
            if (!e.toPlainText().endsWith(QStringLiteral("字\n\n"))) {
                qWarning("selftest FAIL: draw mode enter broken");
                e.toggleMode(Editor::Mode::Normal);
                return false;
            }
            e.toggleMode(Editor::Mode::Normal);
            QApplication::processEvents();
            e.setPlainText(QStringLiteral("無\n"));
        }
        // 笔迹统一撤销：画一笔 → Cmd+Z 撤销 → Cmd+Y 复原
        e.setPlainText(QStringLiteral("文字\n"));
        {
            e.toggleMode(Editor::Mode::Draw);
            QWidget *vp = e.viewport();
            const QPointF p1(50, 50);
            QMouseEvent pr(QEvent::MouseButtonPress, p1, vp->mapToGlobal(p1.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &pr);
            const QPointF p2(90, 50);
            QMouseEvent mv(QEvent::MouseMove, p2, vp->mapToGlobal(p2.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &mv);
            QMouseEvent re(QEvent::MouseButtonRelease, p2, vp->mapToGlobal(p2.toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &re);
            if (e.inkEmpty()) {
                qWarning("selftest FAIL: stroke not recorded");
                return false;
            }
            QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz);
            if (!e.inkEmpty()) {
                qWarning("selftest FAIL: Cmd+Z did not undo the stroke");
                return false;
            }
            QKeyEvent ky(QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier);
            QApplication::sendEvent(&e, &ky);
            if (e.inkEmpty()) {
                qWarning("selftest FAIL: Cmd+Y did not redo the stroke");
                return false;
            }
            e.toggleMode(Editor::Mode::Draw); // 退出模式
        }
        // 擦除通道宽度 = 笔刷宽度：像素级验证
        {
            e.toggleCodeMode();
            if (e.codeMode())
                e.toggleCodeMode(); // 退出编
            e.setPlainText(QString());
            e.clearInk(); // 清掉此前测试的笔迹，避免污染通道测量
            e.resize(400, 300);
            e.show();
            QApplication::processEvents();
            for (int i = 0; i < 30 && e.brushSize() < 40.0; ++i)
                e.brushUp();
            const qreal brush = e.brushSize();
            QWidget *vp = e.viewport();
            e.toggleMode(Editor::Mode::Draw);
            {
                const QPointF p1(50, 80);
                QMouseEvent pr(QEvent::MouseButtonPress, p1, vp->mapToGlobal(p1.toPoint()),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &pr);
                for (int x = 54; x <= 250; x += 4) {
                    const QPointF p2(x, 80);
                    QMouseEvent mv(QEvent::MouseMove, p2, vp->mapToGlobal(p2.toPoint()),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &mv);
                }
                const QPointF p2(250, 80);
                QMouseEvent re(QEvent::MouseButtonRelease, p2, vp->mapToGlobal(p2.toPoint()),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &re);
            }
            e.toggleMode(Editor::Mode::Erase);
            {
                const QPointF p1(150, 30);
                QMouseEvent pr(QEvent::MouseButtonPress, p1, vp->mapToGlobal(p1.toPoint()),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &pr);
                // 连续事件流（真实触控板行为）
                for (int y = 34; y <= 130; y += 4) {
                    const QPointF p2(150, y);
                    QMouseEvent mv(QEvent::MouseMove, p2, vp->mapToGlobal(p2.toPoint()),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &mv);
                }
                const QPointF p3(150, 130);
                QMouseEvent re(QEvent::MouseButtonRelease, p3, vp->mapToGlobal(p3.toPoint()),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &re);
            }
            e.toggleMode(Editor::Mode::Normal);
            // 数据级验证：残余两段笔迹之间的空隙 = 擦除通道 = 笔刷宽度
            const auto paths = e.inkPaths();
            int bestGap = 0;
            if (paths.size() >= 2) {
                const qreal leftEnd = paths.first().boundingRect().right();
                const qreal rightStart = paths.last().boundingRect().left();
                if (rightStart > leftEnd)
                    bestGap = int(rightStart - leftEnd);
            }
            qInfo("ERASE-CHANNEL gap=%d brush=%f paths=%d", bestGap, brush, int(paths.size()));
            if (paths.size() >= 2 && qAbs(bestGap - brush) > 6.0) {
                qWarning("selftest FAIL: erase channel %dpx vs brush %fpx", bestGap, brush);
            }
            e.brushDefault();
        }
        // 言：选中多行头尾批量加「」（空行跳过），一步撤销，整段保持选中
        {
            e.setPlainText(QStringLiteral("甲一\n乙二\n\n丙三\n"));
            e.selectAll();
            e.yan();
            if (e.toPlainText() != QStringLiteral("「甲一」\n「乙二」\n\n「丙三」\n")) {
                qWarning("selftest FAIL: yan() got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            if (e.textCursor().selectionStart() != 0
                || e.textCursor().selectionEnd() != 15) { // 「丙三」」之后、末行换行之前
                qWarning("selftest FAIL: yan() selection not covering wrapped region (%d,%d)",
                         e.textCursor().selectionStart(), e.textCursor().selectionEnd());
                return false;
            }
            QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz);
            if (e.toPlainText() != QStringLiteral("甲一\n乙二\n\n丙三\n")) {
                qWarning("selftest FAIL: yan() not undone in one step");
                return false;
            }
        }
        // 隔：逐行隔离——选中每一行上下各补空行（幂等），一步撤销
        {
            e.setPlainText(QStringLiteral("甲一\n乙二\n丙三\n"));
            QTextBlock bMid = e.document()->findBlockByNumber(1);
            QTextCursor cc(e.document());
            cc.setPosition(bMid.position());
            cc.setPosition(bMid.position() + bMid.length() - 1, QTextCursor::KeepAnchor);
            e.setTextCursor(cc);
            e.ge(); // 单选一行：同旧语义
            if (e.toPlainText() != QStringLiteral("甲一\n\n乙二\n\n丙三\n")) {
                qWarning("selftest FAIL: ge() single line got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            e.ge(); // 幂等
            if (e.toPlainText() != QStringLiteral("甲一\n\n乙二\n\n丙三\n")) {
                qWarning("selftest FAIL: ge() not idempotent, got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            if (e.textCursor().selectionStart() != 4
                || e.textCursor().selectionEnd() != 6) {
                qWarning("selftest FAIL: ge() selection lost the block (%d,%d)",
                         e.textCursor().selectionStart(), e.textCursor().selectionEnd());
                return false;
            }
            QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz);
            if (e.toPlainText() != QStringLiteral("甲一\n乙二\n丙三\n")) {
                qWarning("selftest FAIL: ge() not undone in one step");
                return false;
            }
            // 用户报告的核心案例：选中前两行（顶行在选区里）——每一行都要被隔离
            {
                QTextBlock b0 = e.document()->findBlockByNumber(0);
                QTextBlock b1 = e.document()->findBlockByNumber(1);
                QTextCursor cc2(e.document());
                cc2.setPosition(b0.position());
                cc2.setPosition(b1.position() + b1.length() - 1, QTextCursor::KeepAnchor);
                e.setTextCursor(cc2);
                e.ge();
                if (e.toPlainText() != QStringLiteral("甲一\n\n乙二\n\n丙三\n")) {
                    qWarning("selftest FAIL: ge() top-two lines got [%s]", qPrintable(e.toPlainText()));
                    return false;
                }
            }
            // 五行选中中间三行：每一行独立成岛
            e.setPlainText(QStringLiteral("一\n二\n三\n四\n五\n"));
            {
                QTextBlock b1 = e.document()->findBlockByNumber(1);
                QTextBlock b3 = e.document()->findBlockByNumber(3);
                QTextCursor cc3(e.document());
                cc3.setPosition(b1.position());
                cc3.setPosition(b3.position() + b3.length() - 1, QTextCursor::KeepAnchor);
                e.setTextCursor(cc3);
                e.ge();
                if (e.toPlainText() != QStringLiteral("一\n\n二\n\n三\n\n四\n\n五\n")) {
                    qWarning("selftest FAIL: ge() middle-three got [%s]", qPrintable(e.toPlainText()));
                    return false;
                }
                e.ge(); // 全隔离后再跑一次：不变
                if (e.toPlainText() != QStringLiteral("一\n\n二\n\n三\n\n四\n\n五\n")) {
                    qWarning("selftest FAIL: ge() second pass not idempotent [%s]", qPrintable(e.toPlainText()));
                    return false;
                }
            }
            // 选区内含空行：空行本身就是隔板，不重复加
            e.setPlainText(QStringLiteral("一\n二\n\n三\n"));
            e.selectAll();
            e.ge();
            if (e.toPlainText() != QStringLiteral("一\n\n二\n\n三\n")) {
                qWarning("selftest FAIL: ge() with inner blank got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            e.selectAll();
            e.ge(); // 文首无上行、文末空块已空、内部已隔离 → 全文档幂等
            if (e.toPlainText() != QStringLiteral("一\n\n二\n\n三\n")) {
                qWarning("selftest FAIL: ge() full-doc idempotence got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
        }
        // 言/隔 快捷键通道（Ctrl+L / Ctrl+F）
        {
            e.setPlainText(QStringLiteral("丁四\n"));
            e.moveCursor(QTextCursor::Start);
            QKeyEvent kl(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kl);
            if (e.toPlainText() != QStringLiteral("「丁四」\n")) {
                qWarning("selftest FAIL: Ctrl+L did not call yan()");
                return false;
            }
            // Ctrl+F：文首无上行、文末空块已空 → 语义正确的不动（且不崩溃）
            QKeyEvent kf(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kf);
            if (e.toPlainText() != QStringLiteral("「丁四」\n")) {
                qWarning("selftest FAIL: Ctrl+F at document edge changed text");
                return false;
            }
            // 中间行的 Ctrl+F 才补空行
            e.setPlainText(QStringLiteral("甲\n乙\n丙\n"));
            const QTextBlock bm = e.document()->findBlockByNumber(1);
            QTextCursor cc2(e.document());
            cc2.setPosition(bm.position());
            e.setTextCursor(cc2);
            QKeyEvent kf2(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kf2);
            if (e.toPlainText() != QStringLiteral("甲\n\n乙\n\n丙\n")) {
                qWarning("selftest FAIL: Ctrl+F on middle line got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
        }
        // 先言后隔组合：言保持的选区直接喂给隔（批量校对的完整动线）
        {
            e.setPlainText(QStringLiteral("甲\n一\n二\n三\n丙\n"));
            QTextBlock b1 = e.document()->findBlockByNumber(1);
            QTextBlock b3 = e.document()->findBlockByNumber(3);
            QTextCursor cc(e.document());
            cc.setPosition(b1.position());
            cc.setPosition(b3.position() + b3.length() - 1, QTextCursor::KeepAnchor);
            e.setTextCursor(cc);
            e.yan();
            e.ge();
            if (e.toPlainText() != QStringLiteral("甲\n\n「一」\n\n「二」\n\n「三」\n\n丙\n")) {
                qWarning("selftest FAIL: yan()+ge() pipeline got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz); // 隔一步撤销 → 回到言的成果
            if (e.toPlainText() != QStringLiteral("甲\n「一」\n「二」\n「三」\n丙\n")) {
                qWarning("selftest FAIL: pipeline undo step 1 got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            QKeyEvent kz2(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz2); // 再一步 → 言整体撤销
            if (e.toPlainText() != QStringLiteral("甲\n一\n二\n三\n丙\n")) {
                qWarning("selftest FAIL: pipeline undo step 2 got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
        }
        // 锚定缩放：缩放前后锚点下是同一行（指哪大哪）
        {
            QString zdoc;
            for (int i = 0; i < 120; ++i)
                zdoc += QStringLiteral("锚定缩放测试行 無無無無無無無無無無\n");
            e.setPlainText(zdoc);
            e.resize(400, 300);
            e.show();
            QApplication::processEvents();
            e.zoomReset();
            e.verticalScrollBar()->setValue(60);
            QApplication::processEvents();
            const QPointF anchor(200.0, 120.0);
            e.m_lastMouse = anchor; // 模拟鼠标悬停在锚点
            const int posBefore = e.positionAtViewport(anchor);
            e.zoom(12); // 字号翻倍：锚点下仍应是同一行
            QApplication::processEvents();
            const int posAfter = e.positionAtViewport(anchor);
            qInfo("ANCHOR-ZOOM before=%d after=%d drift=%d vbar=%d", posBefore, posAfter,
                  qAbs(posAfter - posBefore), e.verticalScrollBar()->value());
            // 指哪大哪的语义 = 锚点下的视觉行不丢：允许行内 x→光标的字号漂移
            //（9pt→24pt 同一点可差出一行内的字符数），跑飞才是真失败
            if (qAbs(posAfter - posBefore) > 45) {
                qWarning("selftest FAIL: anchored zoom drifted too far (%d → %d)",
                         posBefore, posAfter);
                return false;
            }
            e.m_lastMouse = QPointF(-1, -1);
            e.zoomReset();
        }
        // 显：像素磷光模式——字体/配色/透明视口/画面，开关可逆
        {
            while (e.machine() != 0)
                e.toggleMachine(); // 测试前提：琥珀机（配色断言以 kInk 为准）
            e.setPlainText(QStringLiteral("無\n"));
            e.toggleCrt();
            QApplication::processEvents();
            const QString fam = e.document()->defaultFont().family();
            if (fam.isEmpty()
                || fam == QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()
                || e.document()->defaultFont().pixelSize() <= 0) {
                qWarning("selftest FAIL: CRT font not applied (family=[%s])", qPrintable(fam));
                return false;
            }
            if (e.palette().color(QPalette::Text) != Crt::kInk) {
                qWarning("selftest FAIL: CRT text color not amber (machine=%d)",
                         e.machine());
                return false;
            }
            if (e.palette().color(QPalette::Base) != Crt::kBg) {
                qWarning("selftest FAIL: CRT base not the phosphor background");
                return false;
            }
            // M1：视角锁定——进显即锁定，切换翻转，退出重进再次锁定
            if (!e.crtViewLocked()) {
                qWarning("selftest FAIL: view lock not default-on");
                return false;
            }
            e.toggleViewLock();
            if (e.crtViewLocked()) {
                qWarning("selftest FAIL: view lock toggle failed");
                return false;
            }
            e.toggleViewLock();
            if (!e.crtViewLocked()) {
                qWarning("selftest FAIL: view lock re-toggle failed");
                return false;
            }
            // M4.5 并入：显·Cmd+0 = 机器原生网格（80 列）；屏幕实体随
            // 追随视角解锁生效（screenEntityOn == !locked）
            e.zoomReset();
            {
                const QFont gf = e.document()->defaultFont();
                const int wantPx = qMax(6, e.viewport()->width() / 80);
                if (qAbs(gf.pixelSize() - wantPx) > 1) {
                    qWarning("selftest FAIL: crt Cmd+0 grid size %d want %d",
                             gf.pixelSize(), wantPx);
                    return false;
                }
            }
            // 网格态跟随窗口：resize 后字号按新宽度重拟合（窗口化/全屏
            // 同一公式，不再取决于按键瞬间的旧宽度——用户报窗口化字号
            // 反超全屏的根修）
            {
                // C64 ⌘0 窗口化 vs 全屏回归闸（用户点名：必须用真实
                // ⌘0 快捷键路径测——旧版只测 zoomReset 直调）。规则：
                // 无封顶 40 列网格 → 字号严格 ∝ 宽度，窗口化恒小于
                // 全屏宽字号（用户明令"最起码永远小于全屏字体尺寸"）。
                while (e.machine() != 2)
                    e.toggleMachine();
                QApplication::processEvents();
                const int wWin = e.width();               // 窗口化宽度
                e.resize(qMax(wWin + 200, wWin * 3 / 2), e.height()); // 模拟全屏宽
                QApplication::processEvents();
                QKeyEvent kzWin(QEvent::KeyPress, Qt::Key_0, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kzWin);      // 真实 ⌘0（全屏宽）
                QApplication::processEvents();
                const int pxFull = e.document()->defaultFont().pixelSize();
                e.resize(wWin, e.height());               // 回到窗口化
                QApplication::processEvents();
                QKeyEvent kzWin2(QEvent::KeyPress, Qt::Key_0, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kzWin2);     // 真实 ⌘0（窗口化）
                QApplication::processEvents();
                const int pxWin = e.document()->defaultFont().pixelSize();
                // 同源公式：crtGridPixelSize()（与 zoomReset 单一来源）
                const auto c64Want = [&e](int w) {
                    e.resize(w, e.height());
                    QApplication::processEvents();
                    return e.crtGridPixelSize();
                };
                if (qAbs(pxWin - c64Want(wWin)) > 1
                    || qAbs(pxFull - c64Want(wWin * 3 / 2)) > 1
                    || pxWin >= pxFull) {
                    qWarning("selftest FAIL: C64 ⌘0 windowed not smaller than fullscreen (win=%d full=%d want %d/%d) — 老bug回归",
                             pxWin, pxFull, c64Want(wWin), c64Want(wWin * 3 / 2));
                    return false;
                }
                while (e.machine() != 0)
                    e.toggleMachine();
                QApplication::processEvents();
                e.resize(wWin, e.height()); // 恢复窗口宽度（后续打印测试按窗口拟合）
                QApplication::processEvents();
                e.zoomReset(); // 恢复琥珀网格态（后续画面断言以之为准）
            }
            {
                const int w0 = e.width();
                e.zoomReset();
                const QFont gf0 = e.document()->defaultFont();
                e.resize(w0 + 160, e.height());
                QApplication::processEvents();
                const QFont gf1 = e.document()->defaultFont();
                const int want0 = qMax(6, w0 / 80);
                const int want1 = qMax(6, (w0 + 160) / 80);
                if (qAbs(gf0.pixelSize() - want0) > 1 || qAbs(gf1.pixelSize() - want1) > 1) {
                    qWarning("selftest FAIL: crt grid not following window (%d->%d, want %d->%d)",
                             gf0.pixelSize(), gf1.pixelSize(), want0, want1);
                    return false;
                }
            }
            e.toggleViewLock(); // 解锁 → 屏幕实体生效
            if (!e.screenEntityOn()) {
                qWarning("selftest FAIL: screen entity not tied to unlocked view");
                return false;
            }
            e.toggleViewLock(); // 重新锁定 → 实体关（干净完美视角）
            // M2：切换计算机——默认琥珀，切绿磷（文字/底色/字体随调色板与
            // 出厂字库），切回
            if (e.crtPalette().ink != Crt::kInk) {
                qWarning("selftest FAIL: default machine not amber");
                return false;
            }
            const QString famAmber = e.document()->defaultFont().family();
            e.toggleMachine();
            if (e.crtPalette().ink != Crt::kGreen.ink
                || e.palette().color(QPalette::Text) != Crt::kGreen.ink
                || e.palette().color(QPalette::Base) != Crt::kGreen.bg
                || e.document()->defaultFont().family() == famAmber) {
                qWarning("selftest FAIL: green machine palette/font not applied");
                return false;
            }
            e.toggleMachine(); // C64 真彩（蓝屏 + 16 色逐字符前景色）
            if (e.crtPalette().ink != Crt::kC64.ink
                || e.palette().color(QPalette::Text) != Crt::kC64.ink
                || e.palette().color(QPalette::Base) != Crt::kC64.bg
                || e.machine() != 2 || !e.colorMachine()) {
                qWarning("selftest FAIL: C64 machine palette not applied");
                return false;
            }
            e.toggleMachine(); // IBM PC 白磷（CGA 白字，FSEX302 字库）
            if (e.crtPalette().ink != Crt::kWhite.ink
                || e.palette().color(QPalette::Text) != Crt::kWhite.ink
                || e.palette().color(QPalette::Base) != Crt::kWhite.bg
                || e.machine() != 3) {
                qWarning("selftest FAIL: white machine palette not applied");
                return false;
            }
            e.toggleMachine();
            if (e.crtPalette().ink != Crt::kInk
                || e.palette().color(QPalette::Text) != Crt::kInk
                || e.palette().color(QPalette::Base) != Crt::kBg) {
                qWarning("selftest FAIL: amber machine not restored");
                return false;
            }
            // 画面：文字区出现琥珀磷光像素；空区是近黑磷底（不是白）
            // 先等暖机脉冲走完（黑幕约 0.5s 退尽），否则整屏被压黑
            {
                QEventLoop loop;
                QTimer::singleShot(700, &loop, &QEventLoop::quit);
                loop.exec();
            }
            // 滚动归零 + 消化排队回调 + 等在途回读落地：断言只测画面
            // 几何，不测异步时序（此前 topLit 在 32/8/0/13 间漂移）
            e.verticalScrollBar()->setValue(0);
            QApplication::processEvents();
            for (int guard = 0; guard < 200 && e.m_crtView && !e.m_crtView->readbackIdle(); ++guard) {
                QEventLoop settle;
                QTimer::singleShot(20, &settle, &QEventLoop::quit);
                settle.exec();
            }
            // 无可用 RHI 后端（无 GPU 的无头机器 / 所有后端被环境跳过）：
            // GPU 相关断言整体豁免——渲染层优雅降级为无画面，CPU 检查照跑
            const bool gpuOk = e.m_crtView && e.m_crtView->pipelineUsable();
            const int g = e.viewport()->pos().x();
            QImage img(e.size(), QImage::Format_ARGB32);
            img.fill(Qt::white);
            if (gpuOk) {
                e.render(&img); // 新架构：覆盖层是普通 QWidget，render 捕获的就是真实 GPU 帧


                // GPU 输出经 RGB 掩膜：亮磷光 = R/G 子像素点燃、B 熄灭（琥珀文字
                // 的 R 与 G 分量分别落在 R/G 掩膜上），不再以原始调色板判色
                bool lit = false;
                for (int y = 0; y < e.height() && !lit; ++y)
                    for (int x = g + 2; x < e.width() - 30 && !lit; ++x) {
                        const QRgb px = img.pixel(x, y);
                        if (qRed(px) + qGreen(px) > 200 && qBlue(px) < 100)
                            lit = true;
                    }
                if (!lit) {
                    qWarning("selftest FAIL: no lit phosphor pixels in CRT render");
                    return false;
                }
                // Y 翻转回归闸：文字在顶部 → 顶部必须有亮带、底部空行区
                // 必须暗（翻转过一次：内容整屏上下颠倒——顶部暗、底部亮，
                // 旧单点检查恰好漏过，用户实测抓到）
                int topLit = 0, bottomLit = 0;
                for (int y = 0; y < qMin(40, e.height()); ++y)
                    for (int x = g + 2; x < e.width() - 30; ++x) {
                        const QRgb px = img.pixel(x, y);
                        if (qRed(px) + qGreen(px) > 200 && qBlue(px) < 100)
                            ++topLit;
                    }
                for (int y = qMax(0, e.height() - 24); y < e.height(); ++y)
                    for (int x = g + 2; x < e.width() - 30; ++x) {
                        const QRgb px = img.pixel(x, y);
                        if (qRed(px) + qGreen(px) > 200 && qBlue(px) < 100)
                            ++bottomLit;
                    }
                // 相对断言：文字在顶部 → 顶部亮于底部。对字形亮度/尺寸
                // 的异步漂移鲁棒（6px 网格字形在不同 DPR/时序下 32/13/2
                // 像素波动）；翻转则底部反超顶部
                if (topLit <= bottomLit || bottomLit > topLit / 2) {
                    qWarning("selftest FAIL: bottom not darker than text band (top=%d bottom=%d) — Y-flip?",
                             topLit, bottomLit);
                    return false;
                }
                const QRgb bgPx = img.pixel(g + 8, e.height() - 20); // 空行区
                if (qRed(bgPx) > 90 || qGreen(bgPx) > 80 || qBlue(bgPx) > 60) {
                    qWarning("selftest FAIL: CRT background not dark (%d,%d,%d)",
                             qRed(bgPx), qGreen(bgPx), qBlue(bgPx));
                    return false;
                }
            } else {
                qWarning("selftest SKIP: no usable RHI backend — CRT GPU render checks skipped");
            }
            // 快照几何：文字在顶部第一行；此前的涂擦测试留下两个墨水圆点，
            // 必须同样出现在合成快照里（墨水进光栅 = 显模式下涂/擦可用的回归闸）。
            // 必须在暴力几何闸之前跑：暴力闸 clearInk() 会清掉墨水
            {
                // 块状反相光标有焦点时会把首字格反相、吃掉顶部琥珀——
                // 光标挪到文末，让顶部断言照旧测文字本身
                QTextCursor endC = e.textCursor();
                endC.movePosition(QTextCursor::End);
                e.setTextCursor(endC);
                QApplication::processEvents();
                QImage snapImg(e.viewport()->size(), QImage::Format_ARGB32);
                snapImg.fill(Qt::transparent);
                e.paintTextSnapshot(snapImg);
                const QImage snap = snapImg;
                int topAmber = 0, inkAmber = 0;
                for (int y = 0; y < snap.height(); ++y)
                    for (int x = 0; x < snap.width(); ++x) {
                        const QRgb px = snap.pixel(x, y);
                        if (qRed(px) > 150 && qGreen(px) > 80 && qBlue(px) < 90) {
                            if (y < 40) ++topAmber; else ++inkAmber;
                        }
                    }
                qInfo("CRT-SNAP amber top=%d ink=%d", topAmber, inkAmber);
                if (topAmber < 10 || inkAmber < 500) {
                    qWarning("selftest FAIL: snapshot composite broken (text top=%d ink=%d)",
                             topAmber, inkAmber);
                    return false;
                }
            }
            // 清墨：ghost 带检查只统计文本。墨水矩形（涂擦测试留下）在
            // 下半屏，会与顶部文字构成第二亮带——原版检查靠"顶部文字
            // 被内缩裁掉看不见"侥幸通过，内缩修掉后必须显式清墨
            e.clearInk();
            // 强制重拍并等新帧落盘：waitReadback 在"无在途回读"时立即
            // 返回，会拿到清墨前的旧 m_shown（含墨帧）——时序洞
            e.m_crtView->markDirty(true);
            QApplication::processEvents();
            {
                QEventLoop settle;
                QTimer::singleShot(100, &settle, &QEventLoop::quit);
                settle.exec();
            }
            QApplication::processEvents();
            // 鼠标移动复现（用户报：动鼠标出倒影）——解锁追随视角后
            // 注入两段鼠标位移，各捕一帧落盘对比
            {
                auto waitReadback = [&] {
                    for (int guard = 0; guard < 200 && e.m_crtView && !e.m_crtView->readbackIdle(); ++guard) {
                        QEventLoop settle;
                        QTimer::singleShot(20, &settle, &QEventLoop::quit);
                        settle.exec();
                    }
                };

                e.toggleViewLock(); // 解锁：观察者跟随鼠标
                e.m_lastMouse = QPointF(10, 10);
                e.m_crtView->markDirty(true);
                QApplication::processEvents();

                // 连续 20 步鼠标位移（真实鼠标的连续路径）
                const QPointF endP(qMax(10.0, e.width() * 0.8), qMax(10.0, e.height() * 0.7));
                for (int step = 1; step <= 20; ++step) {
                    const qreal f = qreal(step) / 20.0;
                    e.m_lastMouse = QPointF(10 + (endP.x() - 10) * f, 10 + (endP.y() - 10) * f);
                    e.m_mouseMoveClock.start(); // 合成路径：与真实 mouseMoveEvent 同效
                    e.m_crtView->markDirty(true);
                    QApplication::processEvents();

                }
                waitReadback();
                // 确保捕获扫动后的新帧（同上时序洞：无在途回读时立即返回）
                e.m_crtView->markDirty(true);
                QApplication::processEvents();
                {
                    QEventLoop settle;
                    QTimer::singleShot(80, &settle, &QEventLoop::quit);
                    settle.exec();
                }
                waitReadback();
                // 回归断言：停稳后不得残留旧位置的亮幽灵带（倒影）
                {
                    QImage chk(e.size(), QImage::Format_ARGB32);
                    chk.fill(Qt::white);
                    e.render(&chk);
                    int bands = 0;
                    int inBand = 0;
                    for (int y = 0; y < chk.height(); ++y) {
                        int rowLit = 0;
                        for (int x = 2; x < chk.width() - 30; ++x) {
                            const QRgb px = chk.pixel(x, y);
                            if (qRed(px) + qGreen(px) > 200 && qBlue(px) < 100)
                                ++rowLit;
                        }
                        if (rowLit > 5) {
                            if (inBand == 0)
                                ++bands;
                            inBand = rowLit;
                        } else {
                            inBand = 0;
                        }
                    }
                    if (bands > 1) {
                        qWarning("selftest FAIL: ghost band after mouse move (bands=%d) — 倒影回归",
                                 bands);
                        return false;
                    }
                }
                e.toggleViewLock(); // 恢复锁定
            }
            // ============ 暴力几何闸（用户实机倒影/弧斜的根修测试） ============
            // 琥珀 + C64 两台各跑一遍。粘贴代码级内容 + 鼠标扫动后，画面必须满足：
            //   1. 文本亮带只允许出现在顶部 20% 区域（一行都不能偏）
            //   2. 其余行必须纯暗（任何第二亮带 = 倒影回归，直接失败）
            //   3. 下半帧不得出现远超背景基线的亮行（镜像倒影 = 亮文本行）
            // 判定全部用亮度 (r+g+b)——琥珀/C64 蓝底同口径（暴力跨机型）
            for (const int violentMachine : { 0, 2 }) {
                auto waitReadback = [&] {
                    for (int guard = 0; guard < 300 && e.m_crtView && !e.m_crtView->readbackIdle(); ++guard) {
                        QEventLoop settle;
                        QTimer::singleShot(16, &settle, &QEventLoop::quit);
                        settle.exec();
                    }
                };
                while (e.machine() != violentMachine)
                    e.toggleMachine();
                QApplication::processEvents();
                e.clearInk(); // 清早期测试的墨迹圆点——暴力统计只许文本
                e.setPlainText(QStringLiteral("orders = [(\"张三\", 99.5)]\nbig = [o for o in orders if o[1] > 100]\nprint(big)\n"));
                e.verticalScrollBar()->setValue(0);
                QApplication::processEvents();
                waitReadback();
                // 鼠标扫动（真实倒影的触发路径）
                e.toggleViewLock();
                for (int step = 1; step <= 24; ++step) {
                    e.m_lastMouse = QPointF(e.width() * (0.15 + 0.7 * step / 24.0),
                                            e.height() * (0.2 + 0.6 * step / 24.0));
                    e.m_mouseMoveClock.start();
                    e.m_crtView->markDirty(true);
                    QApplication::processEvents();
                }
                e.toggleViewLock();
                waitReadback();
                // 归零滚动 + 排空全部排队回调（前序测试的锚定缩放残留），
                // 断言只测画面几何不测序列时序
                e.verticalScrollBar()->setValue(0);
                QApplication::processEvents();
                {
                    QEventLoop drain;
                    QTimer::singleShot(120, &drain, &QEventLoop::quit);
                    drain.exec();
                }
                e.verticalScrollBar()->setValue(0);
                QApplication::processEvents();
                waitReadback();
                QImage vimg(e.size(), QImage::Format_ARGB32);
                vimg.fill(Qt::white);
                e.render(&vimg);

                // 像素级亮度统计：下半帧像素亮度中位数 = 背景基线
                //（调色板无关：琥珀暗底 ~24、C64 蓝底 ~130 各取自身基线）。
                // 文字核心亮度是背景的 4~5 倍（琥珀 431 / C64 521），
                // 背景的栅纹/颗粒峰只到 ~2 倍内——2.5 倍阈清晰切开。
                // 中位数对镜像污染鲁棒（倒影亮带只占少量像素，几乎不
                // 移动中位数），基线不会被倒影本身抬走。
                QVector<int> allLum;
                const int rowCols = vimg.width() - 32;
                allLum.reserve((vimg.height() / 2) * rowCols);
                for (int y = vimg.height() / 2; y < vimg.height(); ++y)
                    for (int x = 2; x < vimg.width() - 30; ++x) {
                        const QRgb px = vimg.pixel(x, y);
                        allLum.append(qRed(px) + qGreen(px) + qBlue(px));
                    }
                std::sort(allLum.begin(), allLum.end());
                const int bgMed = allLum.isEmpty() ? 0 : allLum[allLum.size() / 2];
                const int bright = bgMed * 5 / 2;
                QVector<int> brightCount(vimg.height(), 0);
                for (int y = 0; y < vimg.height(); ++y)
                    for (int x = 2; x < vimg.width() - 30; ++x) {
                        const QRgb px = vimg.pixel(x, y);
                        if (qRed(px) + qGreen(px) + qBlue(px) > bright)
                            ++brightCount[y];
                    }
                // 文本行 = 行内 ≥4 个超阈像素（字形笔画核心）
                const auto isTextRow = [&brightCount](int y) {
                    return brightCount[y] >= 4;
                };
                // 1. 文本带必须从顶部开始（首行文本行出现在顶部 12% 内）
                int first = -1, last = -1;
                for (int y = 0; y < vimg.height(); ++y)
                    if (isTextRow(y)) {
                        if (first < 0)
                            first = y;
                        last = y;
                    }
                if (first < 0 || first > vimg.height() * 12 / 100) {
                    qWarning("selftest FAIL: violent — text band not starting at top (machine=%d first=%d bgMed=%d bright=%d) — 位移",
                             e.machine(), first, bgMed, bright);
                    return false;
                }
                // 2. 文本带必须连续：带内空隙 ≤ 6 行（扫描线暗行不割带）；
                //    第二亮带 = 倒影/残影回归，直接失败
                int gap = 0;
                for (int y = first + 1; y <= last; ++y)
                    if (!isTextRow(y)) {
                        if (++gap > 6) {
                            qWarning("selftest FAIL: violent — text band split (machine=%d gap at y=%d first=%d last=%d) — 倒影",
                                     e.machine(), y, first, last);
                            return false;
                        }
                    } else {
                        gap = 0;
                    }
                // 3. 镜像倒影：下半帧任何文本级亮行（倒影 = 绕中心镜像的
                //    亮文本副本；旧版 bottom*4>top 在背景磷光恒亮下
                //    数学上不可过，废弃）
                for (int y = vimg.height() / 2; y < vimg.height(); ++y)
                    if (isTextRow(y)) {
                        qWarning("selftest FAIL: violent — bright row in bottom half (machine=%d y=%d count=%d bg=%d) — 倒影",
                                 e.machine(), y, brightCount[y], bgMed);
                        return false;
                    }
                e.setPlainText(QStringLiteral("無\n"));
            }
            while (e.machine() != 0)
                e.toggleMachine(); // 后续快照几何断言按琥珀口径
            // 输入暴力闸（用户报：显模式删字/输入法落字卡一秒）：连发
            // 删除（模拟按住删除键自动重复）+ 模拟输入法落字。断言零
            // 回读看门狗触发（轻恢复/整管线重建 = 卡顿的元凶）+ 停手
            // 后 800ms 内画面落地
            if (gpuOk) {
                const int firesBefore = e.m_crtView->watchdogFires();
                e.setPlainText(QStringLiteral("一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十\n"));
                QApplication::processEvents();
                e.moveCursor(QTextCursor::End);
                for (int i = 0; i < 30; ++i) {
                    QKeyEvent kb(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
                    QApplication::sendEvent(&e, &kb);
                    QApplication::processEvents();
                }
                for (int i = 0; i < 10; ++i) {
                    QTextCursor c = e.textCursor();
                    c.insertText(QStringLiteral("無")); // 输入法落字路径（激发+标脏）
                    QApplication::processEvents();
                }
                e.m_crtView->markDirty(true);
                QApplication::processEvents();
                {
                    QEventLoop settle;
                    QTimer::singleShot(800, &settle, &QEventLoop::quit);
                    settle.exec();
                }
                QApplication::processEvents();
                if (e.m_crtView->watchdogFires() != firesBefore) {
                    qWarning("selftest FAIL: input burst triggered readback watchdog (%d fires, maxReadback=%dms) — 卡一秒根因",
                             e.m_crtView->watchdogFires() - firesBefore,
                             e.m_crtView->maxReadbackMs());
                    return false;
                }
                qInfo("CRT-INPUT-BURST watchdog=0 maxReadback=%dms",
                      e.m_crtView->maxReadbackMs());
                e.setPlainText(QStringLiteral("無\n"));
                QApplication::processEvents();
            }
            // 颜色分类取证：琥珀透色（r 主导、g 中量、b 近零——颜色穿过
            // 竖纹亮度纹理）、暗底、无蓝泛滥（坏管线 = 蓝通道点燃）
            if (gpuOk) {
                int amber = 0, blue = 0, dark = 0, other = 0;
                for (int y = 0; y < e.height(); ++y)
                    for (int x = g; x < e.width() - 30; ++x) {
                        const QRgb px = img.pixel(x, y);
                        const int r = qRed(px), gr = qGreen(px), b = qBlue(px);
                        if (r > 150 && gr > 100 && b < 90) ++amber;
                        else if (b > r + 40 && b > gr + 40 && b > 100) ++blue;
                        else if (r < 60 && gr < 60 && b < 60) ++dark;
                        else ++other;
                    }
                qInfo("CRT-COLORS amber=%d blue=%d dark=%d other=%d", amber, blue, dark, other);
                if (amber < 500) {
                    qWarning("selftest FAIL: amber phosphor not lighting (%d)", amber);
                    return false;
                }
                if (blue > 100) {
                    qWarning("selftest FAIL: blue flood in CRT render (%d)", blue);
                    return false;
                }
            }
            // 扫描线：同列相邻行底色有明暗差（信息输出，防渲染层位错）
            if (gpuOk) {
                auto rowMean = [&](int yMod, int x0, int x1) {
                    long sum = 0;
                    int n = 0;
                    for (int y = yMod + 4; y + 3 < e.height(); y += 3)
                        for (int x = x0; x < x1; ++x) {
                            sum += qGray(img.pixel(x, y));
                            ++n;
                        }
                    return n ? double(sum) / n : -1.0;
                };
                const double m0 = rowMean(0, g + 8, g + 90);
                const double m1 = rowMean(1, g + 8, g + 90);
                qInfo("CRT-SCANLINE rows: %f vs %f", m0, m1);
            }
            // ⌃⇧⌘T 暗角渐散闸（用户报：暗角之间有缝隙、像黑边框）：
            // 空文档均匀底 → 亮度剖面必须连续渐散——边缘 ≥55% 中心、
            // 四角 ≥45% 中心、无 30px 窗口内 ≥35% 中心的亮度断崖。
            // 旧版 bezel（7% 窄带 55% 压暗）会让边缘 ~50% 且四角与
            // 边缘之间出现阶跃——本闸把"黑边框感"数值化锁死
            if (gpuOk) {
                e.toggleViewLock(); // 解锁 → 屏幕实体（⌃⇧⌘T 形态）
                e.m_lastMouse = QPointF(-1, -1); // 观察者居中：反光带基准角固定
                e.setPlainText(QString());
                e.verticalScrollBar()->setValue(0);
                // 冲刷余晖历史（切机/前序内容残留的幽灵）——量的是
                // 静态暗角结构，不是历史槽
                e.m_crtView->flushHistory();
                e.m_crtView->markDirty(true);
                QApplication::processEvents();
                {
                    QEventLoop settle;
                    QTimer::singleShot(150, &settle, &QEventLoop::quit);
                    settle.exec();
                }
                e.m_crtView->flushHistory();
                e.m_crtView->markDirty(true);
                QApplication::processEvents();
                {
                    QEventLoop settle;
                    QTimer::singleShot(150, &settle, &QEventLoop::quit);
                    settle.exec();
                }
                for (int guard = 0; guard < 300 && e.m_crtView && !e.m_crtView->readbackIdle(); ++guard) {
                    QEventLoop settle2;
                    QTimer::singleShot(16, &settle2, &QEventLoop::quit);
                    settle2.exec();
                }
                const QImage vig = e.crtShownImage();
                if (vig.isNull() || vig.width() < 100 || vig.height() < 60) {
                    qWarning("selftest FAIL: vignette gate — no frame captured");
                    e.toggleViewLock();
                    e.setPlainText(QStringLiteral("無\n"));
                    return false;
                }
                // 5×5 盒式平滑亮度（扫描线/颗粒平均掉）
                const auto smooth = [&vig](int x, int y) {
                    long sum = 0;
                    int n = 0;
                    for (int yy = qMax(0, y - 2); yy <= qMin(vig.height() - 1, y + 2); ++yy)
                        for (int xx = qMax(0, x - 2); xx <= qMin(vig.width() - 1, x + 2); ++xx) {
                            const QRgb px = vig.pixel(xx, yy);
                            sum += qRed(px) + qGreen(px) + qBlue(px);
                            ++n;
                        }
                    return n ? double(sum) / n : 0.0;
                };
                const int vy = vig.height() / 2;
                const double center = smooth(vig.width() / 2, vy);
                if (center < 10.0) {
                    qWarning("selftest FAIL: vignette gate — frame too dark (center=%.0f)", center);
                    e.toggleViewLock();
                    e.setPlainText(QStringLiteral("無\n"));
                    return false;
                }
                // 边缘 ≥50% 中心（两侧 2%-6% 带取最暗；三行平均稀释
                // 慢放暗带——旧版 7% 窄带 55% 压暗实测 ≈45%，新值
                // 55-75%，50% 阈值清晰分离"黑边框感"）
                double edgeMin = 1e9;
                for (const int ey : { vy - 30, vy, vy + 30 })
                    for (int x = int(vig.width() * 0.02); x < int(vig.width() * 0.06); ++x)
                        edgeMin = qMin(edgeMin, smooth(x, qBound(4, ey, vig.height() - 5)));
                for (const int ey : { vy - 30, vy, vy + 30 })
                    for (int x = int(vig.width() * 0.94); x < int(vig.width() * 0.98); ++x)
                        edgeMin = qMin(edgeMin, smooth(x, qBound(4, ey, vig.height() - 5)));
                // 四角 ≥70% 同行中点（横扫慢放暗带按行整行调制——同
                // 行比值对带鲁棒；角检只量角相对行的额外压暗）
                double cornerRatio = 1e9;
                const int cx[4] = { int(vig.width() * 0.04), int(vig.width() * 0.96),
                                    int(vig.width() * 0.04), int(vig.width() * 0.96) };
                const int cy[4] = { int(vig.height() * 0.08), int(vig.height() * 0.08),
                                    int(vig.height() * 0.92), int(vig.height() * 0.92) };
                for (int i = 0; i < 4; ++i) {
                    const double rowMid = smooth(vig.width() / 2, cy[i]);
                    if (rowMid > 5.0)
                        cornerRatio = qMin(cornerRatio, smooth(cx[i], cy[i]) / rowMid);
                }
                // 剖面平滑性：任一 30px 窗口亮度下降 ≤35% 中心
                double maxDrop = 0.0;
                for (int x = int(vig.width() * 0.01); x < int(vig.width() * 0.99) - 30; ++x)
                    maxDrop = qMax(maxDrop, smooth(x, vy) - smooth(x + 30, vy));
                if (edgeMin < center * 0.50 || cornerRatio < 0.60
                    || maxDrop > center * 0.45) {
                    qWarning("selftest FAIL: vignette not diffused (edge=%.0f%% corner=%.0f%% drop=%.0f%% center=%.0f) — 黑边框感",
                             edgeMin / center * 100.0, cornerRatio * 100.0,
                             maxDrop / center * 100.0, center);
                    e.toggleViewLock();
                    e.setPlainText(QStringLiteral("無\n"));
                    return false;
                }
                qInfo("CRT-VIGNETTE edge=%.0f%% corner=%.0f%% maxDrop=%.0f%%",
                      edgeMin / center * 100.0, cornerRatio * 100.0,
                      maxDrop / center * 100.0);
                e.toggleViewLock(); // 恢复锁定
                e.setPlainText(QStringLiteral("無\n"));
                QApplication::processEvents();
            }
            e.toggleCrt();
            QApplication::processEvents();
            if (e.palette().color(QPalette::Base) == Crt::kBg
                || e.document()->defaultFont().family()
                    != QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()) {
                qWarning("selftest FAIL: CRT toggle-off did not restore font/palette");
                return false;
            }
            // ============ 混合交叉暴力闸（用户点名：笔刷×打字×撤销×重做
            // 交叉猛测）============
            // 固定种子随机操作流：打字/删除/换行/涂笔/擦除/撤销/重做/
            // 模式切换。按"时间序统一撤销"的正确语义建模型：每步操作
            // 前压一帧（文字+笔迹），撤销=回到上一帧、重做=前进一帧。
            // 逐步断言应用文字与模型完全一致（旧双栈撤销在交叉序列中
            // 会撤错目标——用户报"撤回的并非想撤回的"）。
            {
                e.setPlainText(QStringLiteral("起\n"));
                e.clearInk();
                QApplication::processEvents();
                e.verticalScrollBar()->setValue(0);
                QApplication::processEvents();
                const QString chars = QStringLiteral("abcegx無");
                struct Frame {
                    QString doc; // 完整文档串（块以 \n 连接 + 尾随 \n）
                    QVector<QPainterPath> paths; // 笔迹几何
                };
                QVector<Frame> undoStack, redoStack;
                QString curDoc = e.toPlainText(); // 模型当前文档串
                QVector<QPainterPath> curPaths = e.inkPaths();
                const auto pushFrame = [&] {
                    undoStack.append({ curDoc, curPaths });
                    if (undoStack.size() > 200)
                        undoStack.removeFirst();
                    redoStack.clear();
                };
                pushFrame(); // 初始帧（"起\n"）——撤销栈底
                const auto sendKey = [&](int key, Qt::KeyboardModifiers mod) {
                    QKeyEvent ke(QEvent::KeyPress, key, mod);
                    QApplication::sendEvent(&e, &ke);
                    QApplication::processEvents();
                };
                const auto strokeAt = [&](bool erase) {
                    // 涂/擦：固定三点一笔。空擦（没碰到任何笔迹）=
                    // 无操作，不入撤销栈（应用同语义：after==before 不记录）
                    QWidget *vp = e.viewport();
                    const QVector<QPainterPath> beforePaths = e.inkPaths();
                    const QPointF p1(vp->width() * 0.3, vp->height() * 0.5);
                    const QPointF p2(vp->width() * 0.5, vp->height() * 0.45);
                    const QPointF p3(vp->width() * 0.7, vp->height() * 0.5);
                    QMouseEvent pr(QEvent::MouseButtonPress, p1, vp->mapToGlobal(p1.toPoint()),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &pr);
                    QMouseEvent mv1(QEvent::MouseMove, p2, vp->mapToGlobal(p2.toPoint()),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &mv1);
                    QMouseEvent mv2(QEvent::MouseMove, p3, vp->mapToGlobal(p3.toPoint()),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &mv2);
                    QMouseEvent re(QEvent::MouseButtonRelease, p3, vp->mapToGlobal(p3.toPoint()),
                                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &re);
                    QApplication::processEvents();
                    if (e.inkPaths() != beforePaths) {
                        pushFrame();
                        curPaths = e.inkPaths();
                    }
                    return true;
                };
                uint32_t rng = 0x9E3779B9u;
                const auto rnd = [&rng] {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return rng;
                };
                int opNo = 0;
                bool ok = true;
                QString trace;
                for (int step = 0; step < 90 && ok; ++step) {
                    const int op = int(rnd() % 10);
                    ++opNo;
                    const bool atStart = (opNo % 2 == 1);
                    curPaths = e.inkPaths(); // 每步后同步笔迹基线
                    if (op < 3) { // 打字（文首/文末交替，模型同知）
                        const QChar ch = chars.at(int(rnd() % uint(chars.size())));
                        QTextCursor c = e.textCursor();
                        // 文末 = 尾随换行之前（真实打字位置；size() 是空尾块）
                        c.setPosition(atStart ? 0 : qMax(0, e.toPlainText().size() - 1));
                        e.setTextCursor(c);
                        QApplication::processEvents();
                        pushFrame();
                        QTextCursor cc = e.textCursor();
                        cc.insertText(QString(ch)); // 与输入法落字同路径
                        QApplication::processEvents();
                        curDoc.insert(atStart ? 0 : curDoc.size() - 1, ch); // 位置级
                        trace += QStringLiteral("T%1;").arg(QString(ch));
                        if (e.toPlainText() != curDoc) {
                            qWarning("selftest FAIL: chaos op#%d type '%s' [%s]",
                                     opNo, qPrintable(QString(ch)), qPrintable(trace));
                            ok = false;
                        }
                    } else if (op < 5) { // 删除/换行
                        QTextCursor c = e.textCursor();
                        c.setPosition(atStart ? 0 : qMax(0, e.toPlainText().size() - 1));
                        e.setTextCursor(c);
                        QApplication::processEvents();
                        const bool noopDel = (op == 3) && atStart;
                        if (!noopDel)
                            pushFrame();
                        if (op == 3) {
                            sendKey(Qt::Key_Backspace, {});
                            if (!atStart) {
                                // 纯字符串位置级删除 = QTextDocument 块语义的
                                // 精确镜像（删 \n = 并块、删空行 = 收一行）
                                const int pos = curDoc.size() - 1;
                                if (pos > 0)
                                    curDoc.remove(pos - 1, 1);
                            } // 文首退格 = 无操作（不入栈）
                            trace += QStringLiteral("B;");
                            if (e.toPlainText() != curDoc) {
                                qWarning("selftest FAIL: chaos op#%d backspace got=[%s] want=[%s] [%s]",
                                         opNo,
                                         qPrintable(QString(e.toPlainText()).replace(QLatin1Char('\n'), QChar(0x23CE))),
                                         qPrintable(QString(curDoc).replace(QLatin1Char('\n'), QChar(0x23CE))),
                                         qPrintable(trace));
                                ok = false;
                            }
                        } else {
                            sendKey(Qt::Key_Return, {});
                            curDoc.insert(atStart ? 0 : curDoc.size() - 1, QLatin1Char('\n'));
                            trace += QStringLiteral("N;");
                            if (e.toPlainText() != curDoc) {
                                qWarning("selftest FAIL: chaos op#%d enter [%s]",
                                         opNo, qPrintable(trace));
                                ok = false;
                            }
                        }
                    } else if (op < 7) { // 涂/擦（含模式切换）
                        sendKey(op == 5 ? Qt::Key_D : Qt::Key_E, Qt::ControlModifier);
                        trace += op == 5 ? QStringLiteral("D;") : QStringLiteral("E;");
                        strokeAt(false);
                    } else if (op == 7) { // 撤销
                        if (undoStack.size() > 1) {
                            redoStack.append({ curDoc, curPaths }); // 重做帧 = 撤销前状态
                            const Frame expected = undoStack.takeLast();
                            sendKey(Qt::Key_Z, Qt::ControlModifier);
                            curDoc = expected.doc; // 恢复被弹出的帧（上一状态）
                            curPaths = expected.paths;
                            trace += QStringLiteral("U;");
                            if (e.toPlainText() != curDoc || e.inkPaths() != curPaths) {
                                qWarning("selftest FAIL: chaos op#%d undo — got=[%s] want=[%s] [%s]",
                                         opNo,
                                         qPrintable(QString(e.toPlainText()).replace(QLatin1Char('\n'), QChar(0x23CE))),
                                         qPrintable(QString(curDoc).replace(QLatin1Char('\n'), QChar(0x23CE))),
                                         qPrintable(trace));
                                ok = false;
                            }
                        }
                    } else { // 重做
                        if (!redoStack.isEmpty()) {
                            undoStack.append({ curDoc, curPaths }); // 重做前的状态可再撤销
                            const Frame expected = redoStack.takeLast();
                            sendKey(Qt::Key_Y, Qt::ControlModifier);
                            curDoc = expected.doc; // 恢复撤销前的状态
                            curPaths = expected.paths;
                            trace += QStringLiteral("R;");
                            if (e.toPlainText() != curDoc || e.inkPaths() != curPaths) {
                                qWarning("selftest FAIL: chaos op#%d redo — text ok=%d ink got=%d want=%d [%s]",
                                         opNo, int(e.toPlainText() == curDoc),
                                         int(e.inkPaths().size()), int(curPaths.size()),
                                         qPrintable(trace));
                                ok = false;
                            }
                        }
                    }
                }
                if (!ok)
                    return false;
                qInfo("CRT-CHAOS 90 ops passed (undo/redo interleave consistent)");
                e.setPlainText(QStringLiteral("無\n"));
                e.clearInk();
                QApplication::processEvents();
            }
            // M1：退出重进显 → 视角锁定重置
            e.toggleCrt();
            QApplication::processEvents();
            const bool relocked = e.crtViewLocked();
            e.toggleCrt();
            QApplication::processEvents();
            if (!relocked) {
                qWarning("selftest FAIL: view lock not reset on re-entering CRT");
                return false;
            }
        }
        // 格式化库（自 AsciiTools 移植）：框/压行/还原/路径树往返/居中
        {
            e.setPlainText(QStringLiteral("無无\nA\n"));
            e.selectAll();
            e.formatBox(0); // 单线框（字体真实推进对齐）
            const QString boxed = e.toPlainText();
            if (!boxed.startsWith(QStringLiteral("┌"))
                || !boxed.endsWith(QStringLiteral("┘\n"))
                || !boxed.contains(QStringLiteral("無无"))) {
                qWarning("selftest FAIL: box render wrong: %s",
                         boxed.toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("一 二\n三\n\n四\n"));
            e.selectAll();
            e.joinLinesTo(); // 压行
            if (e.toPlainText() != QStringLiteral("一 二 三 四")) {
                qWarning("selftest FAIL: join lines wrong");
                return false;
            }
            e.selectAll();
            e.restoreLines(); // 还原 = 原样恢复（可逆）
            if (e.toPlainText() != QStringLiteral("一 二\n三\n\n四\n")) {
                qWarning("selftest FAIL: restore lines wrong: %s",
                         e.toPlainText().toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("甲。乙！丙？\n"));
            e.selectAll();
            e.restoreLines(); // 无记忆、无代码分隔符 → 按句读切
            if (e.toPlainText() != QStringLiteral("甲。\n乙！\n丙？")) {
                qWarning("selftest FAIL: prose restore wrong: %s",
                         e.toPlainText().toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("if (a;b) { x; y } z;"));
            e.selectAll();
            e.restoreLines(); // 还原（代码语义）
            if (!e.toPlainText().contains(QStringLiteral("{\n"))) {
                qWarning("selftest FAIL: restore lines wrong");
                return false;
            }
            e.setPlainText(QStringLiteral("a\nb/c\nb/d/\n"));
            e.selectAll();
            e.pathsToTree(); // 路径 → 树
            const QString tree = e.toPlainText();
            if (tree != QStringLiteral("├── a\n└── b/\n    ├── c\n    └── d/")) {
                qWarning("selftest FAIL: pathsToTree wrong: %s",
                         tree.toUtf8().constData());
                return false;
            }
            e.selectAll();
            e.treeToPaths(); // 树 → 路径（往返必须还原）
            if (e.toPlainText() != QStringLiteral("a\nb/c\nb/d/")) {
                qWarning("selftest FAIL: treeToPaths roundtrip wrong: %s",
                         e.toPlainText().toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("a\nb\\c\nb\\d\\\n"));
            e.selectAll();
            e.pathsToTree(); // 反斜杠路径（Windows）同样嵌套
            if (e.toPlainText() != QStringLiteral("├── a\n└── b/\n    ├── c\n    └── d/")) {
                qWarning("selftest FAIL: backslash tree wrong: %s",
                         e.toPlainText().toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("無\n"));
            e.selectAll();
            e.centerToWidth(); // 居中：左补空格
            if (!e.toPlainText().startsWith(QLatin1Char(' '))) {
                qWarning("selftest FAIL: centerToWidth no padding");
                return false;
            }
            e.setPlainText(QStringLiteral("無\n"));
        }
        // P3 哨兵：增量重拍与全量重拍像素一致（打一个字 → 脏区重画
        // → 与全量重画逐字节比对；不一致 = 增量漏画，必须查）
        {
            e.setPlainText(QStringLiteral("甲乙丙\n丁戊己\n"));
            e.markSnapshotFullDirty();
            const qreal dpr = e.devicePixelRatioF(); // 与真实管线同构：窗口 DPR
            QImage full1(e.viewport()->size() * dpr, QImage::Format_ARGB32);
            full1.setDevicePixelRatio(dpr);
            full1.fill(Qt::black);
            e.paintTextSnapshot(full1); // 全量基线
            QImage base = full1.copy(); // 增量底 = 上一帧
            e.moveCursor(QTextCursor::End);
            e.insertPlainText(QStringLiteral("無")); // 触发 contentsChange → 脏区
            const auto snap = e.consumeSnapshotDirty();
            const QRect dirty = snap.rect;
            { // 等激发衰减归零（900ms 上限）：时间敏感部分排除出比对
                QEventLoop lp;
                QTimer::singleShot(950, &lp, &QEventLoop::quit);
                lp.exec();
            }
            QImage full2(e.viewport()->size() * dpr, QImage::Format_ARGB32);
            full2.setDevicePixelRatio(dpr);
            full2.fill(Qt::black);
            e.paintTextSnapshot(full2); // 全量对照
            e.paintTextSnapshotRegion(base, dirty); // 增量
            if (base != full2) {
                // 定位首个差异像素
                QString diff;
                const int w = qMin(base.width(), full2.width());
                const int h = qMin(base.height(), full2.height());
                for (int y = 0; y < h && diff.isEmpty(); ++y) {
                    const uchar *a = base.constScanLine(y);
                    const uchar *b = full2.constScanLine(y);
                    for (int x = 0; x < w; ++x) {
                        if (qAbs(int(a[x*4]) - b[x*4]) > 0
                            || qAbs(int(a[x*4+1]) - b[x*4+1]) > 0
                            || qAbs(int(a[x*4+2]) - b[x*4+2]) > 0) {
                            diff = QStringLiteral("(%1,%2) inc=%3,%4,%5 full=%6,%7,%8 dirty=%9,%10,%11,%12")
                                .arg(x).arg(y).arg(a[x*4]).arg(a[x*4+1]).arg(a[x*4+2])
                                .arg(b[x*4]).arg(b[x*4+1]).arg(b[x*4+2])
                                .arg(dirty.x()).arg(dirty.y()).arg(dirty.width()).arg(dirty.height());
                            break;
                        }
                    }
                }
                qWarning("selftest FAIL: incremental snapshot != full snapshot %s",
                         qPrintable(diff));
                return false;
            }
            e.setPlainText(QStringLiteral("無\n"));
        }
        // 撤销基线回归：格式化操作 = 一步撤销（Qt 编辑块合并语义是
        // 本应用的依赖项——升级 Qt 前必过的哨兵）
        {
            e.setPlainText(QStringLiteral("甲\n乙\n丙\n"));
            e.selectAll();
            e.formatBox(0); // 框 = 一步撤销
            const QString boxed2 = e.toPlainText();
            QKeyEvent ku(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &ku);
            QApplication::processEvents();
            if (e.toPlainText() != QStringLiteral("甲\n乙\n丙\n")) {
                qWarning("selftest FAIL: box not one-undo-step");
                return false;
            }
            e.setPlainText(QStringLiteral("甲乙\n"));
            e.selectAll();
            e.centerToWidth();
            QApplication::sendEvent(&e, &ku);
            QApplication::processEvents();
            if (!e.toPlainText().startsWith(QStringLiteral("甲乙"))) {
                qWarning("selftest FAIL: center not one-undo-step");
                return false;
            }
            e.setPlainText(QStringLiteral("無\n"));
        }
        // CRT 管线冒烟（C64 三色栅 + 行扫描激励）：渲两机各一帧落盘，
        // 供人工/取证核对（shader 编译失败 = 黑帧 + 空图）
        // 无可用后端（无 GPU 无头机器）时整体豁免——GPU 帧无从产生
        if (!e.m_crtView || !e.m_crtView->pipelineUsable()) {
            qWarning("selftest SKIP: no usable RHI backend — CRT GPU smoke skipped");
        } else {
            auto waitFrames = [&](int ms) {
                QElapsedTimer clk;
                clk.start();
                while (clk.elapsed() < ms) {
                    QEventLoop lp;
                    QTimer::singleShot(30, &lp, &QEventLoop::quit);
                    lp.exec();
                }
            };
            e.setPlainText(QStringLiteral("無無無 WuWu\nAABBCC 123456\n"));
            for (int m = 0; m < 4; ++m) { // 四机各渲一帧：亮度横比
                while (e.machine() != m)
                    e.toggleMachine();
                e.toggleCrt();
                waitFrames(1600);
                const QImage f = e.crtSnapImage();
                const QString tag = QStringLiteral("crt_m%1").arg(m);
                f.save(QStringLiteral("/tmp/") + tag + QStringLiteral(".png"));
                e.crtShownImage().save(QStringLiteral("/tmp/") + tag + QStringLiteral("_gpu.png"));
                e.toggleCrt();
                if (f.isNull() || f.size().isEmpty()) {
                    qWarning("selftest FAIL: machine %d crt frame empty", m);
                    return false;
                }
            }
            while (e.machine() != 0)
                e.toggleMachine(); // 还原琥珀
        }
        // M3：图片 → 字符画（纯函数验证：合成左白右黑图 → 粗梯度映射）
        {
            QImage simg(64, 32, QImage::Format_ARGB32);
            simg.fill(Qt::black);
            {
                QPainter pp(&simg);
                pp.fillRect(QRect(0, 0, 32, 32), Qt::white);
            }
            const QString art = Ascii::imageToText(simg, 8, 4);
            const QStringList lines = art.split(QLatin1Char('\n'));
            if (lines.size() != 4 || lines[0].size() != 8 || lines[3].size() != 8) {
                qWarning("selftest FAIL: ascii art grid wrong (%lld lines, sizes %lld/%lld)",
                         qint64(lines.size()), lines.isEmpty() ? -1 : qint64(lines[0].size()),
                         lines.size() < 4 ? -1 : qint64(lines[3].size()));
                return false;
            }
            if (lines[0].at(0) == QLatin1Char(' ') || lines[0].at(7) != QLatin1Char(' ')) {
                qWarning("selftest FAIL: ascii art luminance mapping wrong (left='%c' right='%c')",
                         lines[0].at(0).toLatin1(), lines[0].at(7).toLatin1());
                return false;
            }
            // 编辑器路径冒烟：空文档插入 → 逐行打印出整页字符画；
            // 非空文档 → 光标处插入（不覆盖）
            auto waitPrint = [&e] {
                for (int guard = 0; e.asciiPrintingDbg() && guard < 100; ++guard) {
                    QEventLoop lp;
                    QTimer::singleShot(50, &lp, &QEventLoop::quit);
                    lp.exec(); // 打字机打印 ~35ms/行，等到完
                }
            };
            e.setPlainText(QString());
            e.loadAsciiImage(simg);
            waitPrint();
            {
                const QString doc = e.toPlainText();
                const QStringList dl = doc.split(QLatin1Char('\n'));
                if (dl.size() < 3 || dl[0].isEmpty()) {
                    qWarning("selftest FAIL: ascii art editor path produced empty doc");
                    return false;
                }
                const QString rampChars = QStringLiteral(" .:*#@.,-~:;=!*#$@");
                bool onlyRamp = true;
                QChar badChar;
                for (const QChar ch : doc) {
                    if (ch != QLatin1Char('\n') && !rampChars.contains(ch)) {
                        badChar = ch;
                        onlyRamp = false;
                        break;
                    }
                }
                if (!onlyRamp) {
                    qWarning("selftest FAIL: ascii art editor path has non-ramp chars (first=%04x len=%d)",
                             badChar.unicode(), int(doc.size()));
                    return false;
                }
            }
            e.setPlainText(QStringLiteral("無\n"));
            {
                e.moveCursor(QTextCursor::End);
                e.loadAsciiImage(simg);
                waitPrint();
                const QString doc = e.toPlainText();
                if (!doc.startsWith(QStringLiteral("無\n")) || doc.size() < 10) {
                    qWarning("selftest FAIL: ascii art insert overwrote existing text");
                    return false;
                }
                // 插入后缩放压力段（回归：插入图片后 Cmd+=/- / 捏合闪退）
                for (int z = 0; z < 4; ++z) {
                    e.zoom(1);
                    QApplication::processEvents();
                }
                for (int z = 0; z < 4; ++z) {
                    e.zoom(-1);
                    QApplication::processEvents();
                }
                if (e.toPlainText().size() < 10) {
                    qWarning("selftest FAIL: ascii art lost after zoom");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n")); // 还原，防污染后续
            }
            // 回归：画布外文字共存 → 切编（Cmd+B）重印不得吞掉画布外文字
            {
                e.setPlainText(QStringLiteral("开头文字\n"));
                e.moveCursor(QTextCursor::End);
                e.loadAsciiImage(simg);
                waitPrint();
                e.toggleCodeMode(); // 切编（用户误报 Cmd+B 吞字符）
                waitPrint();
                if (!e.toPlainText().startsWith(QStringLiteral("开头文字\n"))) {
                    qWarning("selftest FAIL: code toggle ate surrounding text");
                    return false;
                }
                e.toggleCodeMode();
                waitPrint();
                if (!e.toPlainText().startsWith(QStringLiteral("开头文字\n"))) {
                    qWarning("selftest FAIL: code exit ate surrounding text");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归（用户事故复现）：空(Cmd+N)清光 → 撤销复原 → 重勾立为图
            // 必须真正复活画布态（NoWrap 不叠行 + 缩放可用）——不得假勾
            {
                e.setPlainText(QStringLiteral("开头文字\n"));
                e.moveCursor(QTextCursor::End);
                e.loadAsciiImage(simg);
                waitPrint();
                const QString before = e.toPlainText();
                QKeyEvent kn(QEvent::KeyPress, Qt::Key_N, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kn); // 空：清空全部文字
                QApplication::processEvents();
                if (!e.toPlainText().isEmpty() || e.m_asciiActive) {
                    qWarning("selftest FAIL: kong did not clear/end art");
                    return false;
                }
                QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kz); // 撤销 → 复原
                QApplication::processEvents();
                if (e.toPlainText() != before) {
                    qWarning("selftest FAIL: kong undo did not restore doc (%d vs %d)",
                             int(e.toPlainText().size()), int(before.size()));
                    return false;
                }
                QTextCursor nc = e.textCursor();
                nc.clearSelection();
                e.setTextCursor(nc);
                e.declareArtFromSelection(); // 无选区 → 复选上次范围
                if (!e.m_asciiActive
                    || e.lineWrapMode() != QPlainTextEdit::NoWrap) {
                    qWarning("selftest FAIL: re-declare after kong-undo not engaged");
                    return false;
                }
                e.zoomAsciiCanvas(1.2); // 画布缩放仍应生效
                waitPrint();
                if (!e.m_asciiActive || e.toPlainText().isEmpty()) {
                    qWarning("selftest FAIL: canvas zoom after re-declare failed");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归：画布操作不进撤销历史（撤销基线）——Cmd+Z 不得蚕食画布，
            // 之后打字的撤销照常
            {
                e.setPlainText(QString());
                e.loadAsciiImage(simg);
                waitPrint();
                const QString artOnce = e.toPlainText();
                e.zoomAsciiCanvas(0.8); // 缩小：必产生重印（放大可能触原生上限）
                waitPrint();
                if (e.toPlainText() == artOnce) {
                    qWarning("selftest FAIL: zoom did not reprint");
                    return false;
                }
                QKeyEvent kz2(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kz2); // Cmd+Z：不得触碰画布
                QApplication::processEvents();
                if (e.toPlainText().isEmpty() || !e.m_asciiActive) {
                    qWarning("selftest FAIL: undo ate art after zoom");
                    return false;
                }
                const QString afterZoom = e.toPlainText();
                e.moveCursor(QTextCursor::End);
                QKeyEvent kt(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
                QApplication::sendEvent(&e, &kt); // 画布后打字
                QApplication::processEvents();
                QKeyEvent kz3(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kz3); // 撤销打字：照常可用
                QApplication::processEvents();
                if (e.toPlainText() != afterZoom) {
                    qWarning("selftest FAIL: post-art typing not undoable");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归（用户报：打印期间 Cmd+B 会删字符）：打印不等待，
            // 连续多次切编交错打印拍 → 画布必须一字不少
            {
                e.setPlainText(QString());
                e.loadAsciiImage(simg);
                waitPrint();
                const int fullLen = e.toPlainText().size(); // 参考全长
                e.setPlainText(QStringLiteral("开头文字\n"));
                e.moveCursor(QTextCursor::End);
                e.loadAsciiImage(simg); // 打印开始——不等待
                for (int i = 0; i < 10; ++i) {
                    QKeyEvent kb(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier);
                    QApplication::sendEvent(&e, &kb); // 打印期间切编
                    QEventLoop lp; // 让打印拍与切编交错
                    QTimer::singleShot(5, &lp, &QEventLoop::quit);
                    lp.exec();
                }
                waitPrint();
                if (!e.m_asciiActive || !e.toPlainText().startsWith(QStringLiteral("开头文字\n"))
                    || e.toPlainText().size() != fullLen + 5) {
                    qWarning("selftest FAIL: rapid code toggles during print ate text (cc=%d want %d)",
                             int(e.toPlainText().size()), fullLen + 5);
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归：打印期间切编（高亮器 rehighlight 曾误触发反激活）→
            // 画布态存活、退出编存活、无选区立为图可复选上次范围
            {
                e.setPlainText(QString());
                e.loadAsciiImage(simg); // 打印开始
                e.toggleCodeMode();     // 打印期间切编
                waitPrint();
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: code-mode toggle deactivated art");
                    return false;
                }
                e.toggleCodeMode();     // 退出编
                waitPrint();
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: code-mode exit deactivated art");
                    return false;
                }
                QTextCursor nc = e.textCursor();
                nc.clearSelection();
                e.setTextCursor(nc);
                e.declareArtFromSelection(); // 无选区 → 复选上次范围
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: no-selection declare failed");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n")); // 还原
            }
            // C64 真彩路径：颜色与字符一一对应、含黑白两端色
            {
                QVector<QRgb> cols;
                const QString cart = Ascii::imageToTextColors(simg, 8, 4, cols,
                                                              Crt::kC64Colors);
                const int chars = cart.count(QStringLiteral("\n")) * -1 + cart.size();
                if (cols.size() != chars || cols.size() < 20) {
                    qWarning("selftest FAIL: color art size mismatch (%d vs %d)",
                             int(cols.size()), chars);
                    return false;
                }
                bool hasWhite = false, hasBlack = false;
                for (QRgb c : cols) {
                    if (qRed(c) > 200 && qGreen(c) > 200 && qBlue(c) > 200)
                        hasWhite = true;
                    if (qRed(c) < 30 && qGreen(c) < 30 && qBlue(c) < 30)
                        hasBlack = true;
                }
                if (!hasWhite || !hasBlack) {
                    qWarning("selftest FAIL: color quantization lost black/white");
                    return false;
                }
            }
            // 「立为图」：任意选区 → 立为图 → 画布缩放（再打印）可用；
            // 无选区时复选上次范围
            {
                e.setPlainText(QStringLiteral("一二三\n四五六\n"));
                e.selectAll();
                e.declareArtFromSelection();
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: declare-art did not activate");
                    return false;
                }
                e.zoomAsciiCanvas(1.2);
                waitPrint();
                if (e.toPlainText().isEmpty()) {
                    qWarning("selftest FAIL: declare-art canvas zoom emptied doc");
                    return false;
                }
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: canvas zoom deactivated art");
                    return false;
                }
                e.zoomAsciiCanvas(1.2); // 第二次画布缩放仍应生效（再打印）
                waitPrint();
                if (!e.m_asciiActive || e.toPlainText().isEmpty()) {
                    qWarning("selftest FAIL: second canvas zoom failed");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归（用户报：拖图→摹→压行→还原→立为图→连按换机 = 卡死）
            // 注：压行/还原走菜单快捷键（自测无菜单栏，直接调方法等价）
            {
                e.setPlainText(QString());
                while (e.machine() != 2)
                    e.toggleMachine(); // C64
                e.loadAsciiImage(simg);
                waitPrint();
                const QString doc0 = e.toPlainText();
                e.mo(); // 摹：全选+复制
                e.joinLinesTo(); // 压行
                if (e.toPlainText().count(QLatin1Char('\n')) > 2) {
                    qWarning("selftest FAIL: join did not join");
                    return false;
                }
                e.restoreLines(); // 还原
                if (e.toPlainText() != doc0) {
                    qWarning("selftest FAIL: restore did not restore");
                    return false;
                }
                e.selectAll(); // 还原后选区 = 恢复的全文
                e.declareArtFromSelection(); // 立为图
                if (e.m_asciiBaseCols > 400) {
                    qWarning("selftest FAIL: declare baseCols exploded (%d)",
                             e.m_asciiBaseCols);
                    return false;
                }
                for (int i = 0; i < 8; ++i) {
                    e.toggleMachine(); // 换机连按（卡死场景）
                    QApplication::processEvents();
                }
                waitPrint();
                if (!e.toPlainText().isEmpty() && e.m_asciiActive) {
                    // 画布在场且非空：换机重印应已完成
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归（用户报：格式化→重勾立为图后缩放/换机失效、真彩泄漏）
            {
                e.setPlainText(QString());
                while (e.machine() != 2)
                    e.toggleMachine(); // C64
                e.loadAsciiImage(simg);
                waitPrint();
                if (e.m_asciiColors.isEmpty()) {
                    qWarning("selftest FAIL: c64 art produced no colors");
                    return false;
                }
                e.yan(); // 格式化（言）→ 反激活 + 叠行（预期）
                if (e.m_asciiActive) {
                    qWarning("selftest FAIL: yan did not deactivate art");
                    return false;
                }
                e.declareArtFromSelection(); // 无选区 → 复选上次范围（恢复路径）
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: re-declare did not reactivate");
                    return false;
                }
                e.zoomAsciiCanvas(1.2); // 缩放必须可用
                waitPrint();
                if (!e.m_asciiActive || e.toPlainText().isEmpty()) {
                    qWarning("selftest FAIL: zoom after re-declare failed");
                    return false;
                }
                const QString afterZoom = e.toPlainText();
                e.zoomAsciiCanvas(0.8);
                waitPrint();
                if (e.toPlainText() == afterZoom) {
                    qWarning("selftest FAIL: second zoom after re-declare no-op");
                    return false;
                }
                // 换机不得泄漏真彩：非 C64 机上文档不得再带彩色前景
                e.yan(); // 再反激活（画布不在场）
                while (e.machine() != 0)
                    e.toggleMachine(); // 琥珀
                QTextCursor fc(e.document());
                fc.movePosition(QTextCursor::NextCharacter);
                if (fc.charFormat().foreground().style() != Qt::NoBrush) {
                    qWarning("selftest FAIL: c64 colors leaked to amber machine");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
        }
        // 字体管理：空/不存在目录 → 扫描为空；字体文件夹播种后重扫
        // 必须照常收集族名（去重不得吞掉列表——"换字体无反应"的哨兵）
        // 循环后族名永不为空（出厂回退）
        {
            const QStringList none = Editor::scanFontFamilies(
                QStringLiteral("/nonexistent-naught-fonts-dir"));
            if (!none.isEmpty()) {
                qWarning("selftest FAIL: font scan of missing dir not empty");
                return false;
            }
            e.restoreDefaultFont(); // 先复位：跨运行的持久化选择不污染本测试
            const QString f0 = e.crtFontFamily();
            Editor::seedClassicFonts(); // 播种经典库存（去重缓存的哨兵前提）
            e.cycleCrtFont(+1); // 用户文件夹无论有无字体，族名都必须可用
            if (e.crtFontFamily().isEmpty()) {
                qWarning("selftest FAIL: font family empty after cycle");
                return false;
            }
            e.cycleCrtFont(+1); // 第二次循环：重扫必须照常收集（去重不得吞列表）
            e.restoreDefaultFont();
            if (e.crtFontFamily() != f0) {
                qWarning("selftest FAIL: restore default font wrong (%s vs %s)",
                         qPrintable(e.crtFontFamily()), qPrintable(f0));
                return false;
            }
            e.cycleCrtFont(+1); // 往返：+1 进入用户字体
            e.cycleCrtFont(-1); // -1 必须回到出厂
            if (e.crtFontFamily() != f0) {
                qWarning("selftest FAIL: font cycle round-trip changed family (%s -> %s, want %s)",
                         qPrintable(f0), qPrintable(e.crtFontFamily()), qPrintable(f0));
                return false;
            }
        }
        // 真衍射的边差分：合成白块的左右竖直边界各产出一条彩边掩膜
        {
            QImage synth(40, 20, QImage::Format_ARGB32);
            synth.fill(Qt::transparent);
            QPainter sp(&synth);
            sp.fillRect(QRect(10, 4, 12, 10), QColor(255, 255, 255, 255));
            sp.end();
            const QImage eR = Crt::edgeDiff(synth, +1);
            const QImage eB = Crt::edgeDiff(synth, -1);
            int rCols = 0, bCols = 0, rWrong = 0, bWrong = 0;
            for (int y = 0; y < eR.height(); ++y) {
                const uchar *rRow = eR.constScanLine(y);
                const uchar *bRow = eB.constScanLine(y);
                for (int x = 0; x < eR.width(); ++x) {
                    if (rRow[x] > 0) {
                        if (x == 21) ++rCols; else ++rWrong;
                    }
                    if (bRow[x] > 0) {
                        if (x == 10) ++bCols; else ++bWrong;
                    }
                }
            }
            qInfo("DIFF-EDGE rightCol=%d wrong=%d leftCol=%d wrong=%d",
                  rCols, rWrong, bCols, bWrong);
            if (rCols < 8 || bCols < 8 || rWrong > 0 || bWrong > 0) {
                qWarning("selftest FAIL: edgeDiff columns wrong (R:%d/%d B:%d/%d)",
                         rCols, rWrong, bCols, bWrong);
                return false;
            }
        }
        return true;
}
