// 「無」(naught) —— 空白。打开即写，关闭即无。
// 右键 / 双指点按：摹 · 空 ── 阴 · 阳
// Ctrl/Cmd+S：摹（全选并复制）   Ctrl/Cmd+N：空（清空，可撤销）
// Ctrl/Cmd+= / -：字号缩放（按住加速，步长随字号等比增长）  Ctrl/Cmd+0：复位
// Ctrl/Cmd+滚轮、触控板捏合：缩放；触控板横向平移 / Shift+滚轮：横向滚动

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <functional>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLockFile>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QProcess>
#include <QScrollBar>
#include <QString>
#include <QTextCursor>
#include <QTimer>

#include <sys/types.h>
#include <unistd.h>

#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>        // RegisterEventHotKey（无需辅助功能授权）
#include <CoreServices/CoreServices.h> // LSOpenCFURLRef
#include <AppKit/AppKit.h>        // NSApplication：Accessory 激活策略（会话 GUI 进程）
#endif

#include "editor.h"

// ============ M6 自杀与重生 ============
// 自杀：⌃⌘N 立即退出（无保存提示，"关闭即无"的极致）。
// 重生：独立的纯 Cocoa 登录项应用 naught-agent.app（LSUIElement，
// 系统登录项列表注册）持 ⌃⇧⌘N 全局热键——应用死亡时代理
// LSOpen naught.app（无中生有）。
// 注：launchd 裸进程收不到系统级热键投递（此前两轮失败的根因），
// 必须作为真正的登录项应用运行。

#ifdef Q_OS_MACOS
// 安装程序（替代 zip 解压安装）：布署 naught-agent.app 登录项 +
// LaunchServices 注册主应用 + 清理旧 launchd 代理。幂等：重跑 = 重装。
static int installNaught()
{
    const QString bundle = QFileInfo(QDir(QCoreApplication::applicationDirPath())
                                         .absoluteFilePath(QStringLiteral("../..")))
                               .canonicalFilePath();
    // 1) 清理旧版 launchd 代理（迁移）
    QProcess::execute(QStringLiteral("launchctl"),
                      { QStringLiteral("bootout"),
                        QStringLiteral("gui/%1/com.2liver.naught.agent").arg(getuid()) });
    QFile::remove(QDir::homePath()
                  + QStringLiteral("/Library/LaunchAgents/com.2liver.naught.agent.plist"));
    // 2) 清理旧版"并排"代理（迁移）
    const QString agentOld = QFileInfo(bundle).dir().filePath(QStringLiteral("naught-agent.app"));
    QDir(agentOld).removeRecursively();
    // 3) 代理 = 嵌套在 Resources 内（不进启动台）；直接注册登录项
    const QString agentPath = bundle + QStringLiteral("/Contents/Resources/naught-agent.app");
    CFURLRef agentUrl = CFURLCreateWithFileSystemPath(
        nullptr, agentPath.toCFString(), kCFURLPOSIXPathStyle, true);
    bool loginItemOk = false;
    if (agentUrl) {
        LSSharedFileListRef list = LSSharedFileListCreate(
            nullptr, kLSSharedFileListSessionLoginItems, nullptr);
        if (list) {
            LSSharedFileListItemRef item = LSSharedFileListInsertItemURL(
                list, kLSSharedFileListItemLast, nullptr, nullptr, agentUrl,
                nullptr, nullptr);
            loginItemOk = (item != nullptr);
            if (item)
                CFRelease(item);
            CFRelease(list);
        }
        CFRelease(agentUrl);
    }
    if (!loginItemOk) {
        qWarning("naught install: login item registration failed");
        return 1;
    }
    // 4) 注册主应用 + 立刻启动代理（LSOpen = 完整登录项应用启动路径）
    QProcess::execute(QStringLiteral("/System/Library/Frameworks/CoreServices.framework/"
                                     "Frameworks/LaunchServices.framework/Support/lsregister"),
                      { QStringLiteral("-f"), bundle });
    CFURLRef launchUrl = CFURLCreateWithFileSystemPath(
        nullptr, agentPath.toCFString(), kCFURLPOSIXPathStyle, true);
    if (launchUrl) {
        LSOpenCFURLRef(launchUrl, nullptr);
        CFRelease(launchUrl);
    }
    qInfo("naught install: login-item agent installed + app registered");
    return 0;
}

// 登录项是否已指向本代理（启动自检：缺失则静默补装——打包分发
// 后首启即具备重生能力，无需手工跑安装程序）
static bool naughtAgentLoginItemInstalled()
{
    const QString agentPath = QFileInfo(QDir(QCoreApplication::applicationDirPath())
                                            .absoluteFilePath(QStringLiteral("../Resources/naught-agent.app")))
                                   .canonicalFilePath();
    bool found = false;
    LSSharedFileListRef list = LSSharedFileListCreate(
        nullptr, kLSSharedFileListSessionLoginItems, nullptr);
    if (!list)
        return false;
    CFArrayRef snapshot = LSSharedFileListCopySnapshot(list, nullptr);
    if (snapshot) {
        for (CFIndex i = 0; i < CFArrayGetCount(snapshot) && !found; ++i) {
            LSSharedFileListItemRef item =
                (LSSharedFileListItemRef)CFArrayGetValueAtIndex(snapshot, i);
            CFURLRef url = nullptr;
            if (LSSharedFileListItemResolve(item, kLSSharedFileListNoUserInteraction,
                                            &url, nullptr) == noErr && url) {
                if (QUrl::fromCFURL(url).toLocalFile() == agentPath)
                    found = true;
                CFRelease(url);
            }
        }
        CFRelease(snapshot);
    }
    CFRelease(list);
    return found;
}
#endif // Q_OS_MACOS

// 性能基准（--bench）：大文档装载 / 行尾·行中打字 / 缩放 / 滚动 / 行号重绘 / 笔迹。
// 离屏为软件光栅（真机走 GPU 合成），数值作回归基线，不直接代表真机帧时间。
static bool benchmark()
{
    Editor e;
    e.resize(900, 600);
    e.show();
    QApplication::processEvents();

    QElapsedTimer c;
    const auto ms = [&c] { return c.nsecsElapsed() / 1e6; };

    // 1) 装载：10 万字符（2000 行 × 50 字）
    QString big;
    for (int i = 0; i < 2000; ++i)
        big += QStringLiteral("一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十\n");
    c.start();
    e.setPlainText(big);
    qInfo("BENCH load-100k: %.1f ms (%d chars, %d blocks)", ms(),
          e.document()->characterCount(), e.document()->blockCount());

    // 2) 行尾打字：单行 10 万字（强制整块重排换行）末尾追加
    QString longLine(100000, QChar(0x7121)); // 無
    e.setPlainText(longLine);
    QApplication::processEvents();
    c.start();
    for (int i = 0; i < 500; ++i) {
        e.moveCursor(QTextCursor::End);
        e.insertPlainText(QStringLiteral("無"));
    }
    qInfo("BENCH type-end-long-line x500: %.3f ms/op", ms() / 500.0);

    // 3) 行中打字：同文档中段插入
    c.start();
    for (int i = 0; i < 500; ++i) {
        QTextCursor cc = e.textCursor();
        cc.setPosition(50000);
        e.setTextCursor(cc);
        e.insertPlainText(QStringLiteral("無"));
    }
    qInfo("BENCH type-middle-long-line x500: %.3f ms/op", ms() / 500.0);

    // 3b) 对照：禁换行后同一文档（整块无需换行重排）
    e.setLineWrapMode(QPlainTextEdit::NoWrap);
    QApplication::processEvents();
    c.restart();
    for (int i = 0; i < 500; ++i) {
        QTextCursor cc = e.textCursor();
        cc.setPosition(50000);
        e.setTextCursor(cc);
        e.insertPlainText(QStringLiteral("無"));
    }
    qInfo("BENCH type-middle-long-line-nowrap x500: %.3f ms/op", ms() / 500.0);
    e.setLineWrapMode(QPlainTextEdit::WidgetWidth);

    // 4) 多行文档行尾追加
    e.setPlainText(big);
    QApplication::processEvents();
    c.start();
    for (int i = 0; i < 500; ++i) {
        e.moveCursor(QTextCursor::End);
        e.insertPlainText(QStringLiteral("無"));
    }
    qInfo("BENCH type-end-multiline x500: %.3f ms/op", ms() / 500.0);

    // 5) 缩放：O(1) 字号重排 ×100（大字←→小字交替）
    c.start();
    for (int i = 0; i < 100; ++i)
        e.zoomTo(i % 2 ? 12.0 : 96.0);
    QApplication::processEvents();
    qInfo("BENCH zoom x100: %.3f ms/op", ms() / 100.0);

    // 6) 滚动：竖向全行程 200 步 + 每步结算重绘
    QScrollBar *vb = e.verticalScrollBar();
    c.start();
    for (int i = 0; i <= 200; ++i) {
        vb->setValue(vb->maximum() * i / 200);
        QApplication::processEvents();
    }
    qInfo("BENCH scroll-v x200: %.3f ms/step", ms() / 200.0);

    // 7) 编模式行号重绘：滚到中段（行号 1000+）后整窗渲染
    e.toggleCodeMode();
    QApplication::processEvents();
    vb->setValue(vb->maximum() / 2);
    QApplication::processEvents();
    QImage img(e.size(), QImage::Format_ARGB32);
    c.start();
    e.render(&img);
    qInfo("BENCH gutter-render-2000lines: %.1f ms", ms());
    e.toggleCodeMode();
    QApplication::processEvents();

    // 8) 笔迹：100 笔 × 10 点真实事件管道，再整窗渲染
    e.setPlainText(QString());
    e.toggleMode(Editor::Mode::Draw);
    QWidget *vp = e.viewport();
    c.start();
    for (int s = 0; s < 100; ++s) {
        const QPointF p0(30.0 + s * 4.0, 40.0 + (s % 8) * 30.0);
        QMouseEvent pr(QEvent::MouseButtonPress, p0, vp->mapToGlobal(p0.toPoint()),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &pr);
        for (int k = 1; k <= 10; ++k) {
            const QPointF p2 = p0 + QPointF(k * 12.0, k * 2.0);
            QMouseEvent mv(QEvent::MouseMove, p2, vp->mapToGlobal(p2.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &mv);
        }
        const QPointF pEnd = p0 + QPointF(120.0, 20.0);
        QMouseEvent re(QEvent::MouseButtonRelease, pEnd, vp->mapToGlobal(pEnd.toPoint()),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &re);
    }
    qInfo("BENCH ink-build-100-strokes: %.1f ms", ms());
    c.start();
    e.render(&img);
    qInfo("BENCH ink-render-100-strokes: %.1f ms", ms());
    e.toggleMode(Editor::Mode::Normal);
    e.clearInk();

    // 9) 显：带全套滤镜的打字/滚动/缩放开销（预算：打字≤1.5ms/键、滚动≤5ms/步）
    e.setPlainText(big);
    e.toggleCrt();
    QApplication::processEvents();
    c.restart();
    for (int i = 0; i < 200; ++i) {
        e.moveCursor(QTextCursor::End);
        e.insertPlainText(QStringLiteral("無"));
    }
    qInfo("BENCH crt-type-end x200: %.3f ms/op", ms() / 200.0);
    c.restart();
    for (int i = 0; i <= 100; ++i) {
        vb->setValue(vb->maximum() * i / 100);
        QApplication::processEvents();
    }
    qInfo("BENCH crt-scroll x100: %.3f ms/step", ms() / 100.0);
    c.restart();
    for (int i = 0; i < 50; ++i)
        e.zoomTo(i % 2 ? 12.0 : 96.0);
    QApplication::processEvents();
    qInfo("BENCH crt-zoom x50: %.3f ms/op", ms() / 50.0);
    e.toggleCrt();
    QApplication::processEvents();
    return true;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("無"));
    app.setApplicationDisplayName(QStringLiteral("無"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/naught.png")));
    app.setCursorFlashTime(0); // 关闭原生闪烁器：闪烁与休眠由 Editor 自驱，保证完整对称无残拍

#ifdef Q_OS_MACOS
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--install"))
        return installNaught(); // 安装程序：布署登录项 + 注册应用
#endif
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--selftest"))
        return Editor::selftest() ? 0 : 1;
#ifdef Q_OS_MACOS
    if (!naughtAgentLoginItemInstalled()) // 首启/代理丢失：静默补装（重生能力随包装分发）
        installNaught();
#endif
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--bench"))
        return benchmark() ? 0 : 1;

    Editor editor;
    editor.setWindowTitle(QString());
    editor.resize(960, 720); // 默认 4:3（真机画幅；开后可自由调整）
    editor.show();

#ifdef Q_OS_MACOS
    // 系统菜单栏上的「法」：快捷键说明随原生 key equivalent 显示。
    // Windows/Linux 不设菜单栏（无），其右键菜单为 Qt 自绘、自带快捷键列。
    {
        // 原生全局菜单栏（nullptr 父）。快捷键即时生效靠下方
        // ApplicationShortcut 上下文——不靠父窗口（带父菜单栏在 macOS
        // 上会退化为非原生、不可见的栏）
        QMenuBar *menuBar = new QMenuBar(nullptr);
        QMenu *fa = menuBar->addMenu(QStringLiteral("项"));
        // ── 视图区（父级收纳）：编/显/图 ──
        QMenu *mBian = fa->addMenu(QStringLiteral("编"));
        QAction *bBian = mBian->addAction(QStringLiteral("编程高亮"));
        bBian->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
        bBian->setCheckable(true);
        QAction *bEsc = mBian->addAction(QStringLiteral("Esc＝退出模式"));
        bEsc->setEnabled(false);
        QMenu *mXian = fa->addMenu(QStringLiteral("显"));
        QAction *bXian = mXian->addAction(QStringLiteral("模拟显像管"));
        bXian->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
        bXian->setCheckable(true);
        QAction *bGreen = mXian->addAction(QStringLiteral("换机"));
        bGreen->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+M")));
        QAction *bLock = mXian->addAction(QStringLiteral("追随视角锁定"));
        bLock->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
        bLock->setCheckable(true);
        bLock->setChecked(true);
        QMenu *mTu = fa->addMenu(QStringLiteral("图"));
        QAction *bDeclare = mTu->addAction(QStringLiteral("立为图"));
        bDeclare->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
        bDeclare->setCheckable(true);
        QAction *bAscii = mTu->addAction(QStringLiteral("拖入图片 → 字符画"));
        bAscii->setEnabled(false); // 自释性提示：隐藏功能，README 不写
        fa->addSeparator();
        // ── 格式化区：言/隔/文本处理 ──
        QAction *bYan = fa->addAction(QStringLiteral("言"));
        bYan->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
        QAction *bGe = fa->addAction(QStringLiteral("隔"));
        bGe->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
        QMenu *mFmt = fa->addMenu(QStringLiteral("文本处理"));
        QAction *bBoxSingle = mFmt->addAction(QStringLiteral("单线框"));
        bBoxSingle->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+G")));
        QAction *bBoxDouble = mFmt->addAction(QStringLiteral("双线框"));
        bBoxDouble->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+H")));
        QAction *bBoxRound = mFmt->addAction(QStringLiteral("圆角框"));
        bBoxRound->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+U")));
        QAction *bBoxBold = mFmt->addAction(QStringLiteral("粗线框"));
        bBoxBold->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
        mFmt->addSeparator();
        QAction *bJoin = mFmt->addAction(QStringLiteral("压成一行"));
        bJoin->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+J")));
        QAction *bSplit = mFmt->addAction(QStringLiteral("还原为多行"));
        bSplit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
        mFmt->addSeparator();
        QAction *bPathsToTree = mFmt->addAction(QStringLiteral("路径列表 → 树"));
        bPathsToTree->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
        QAction *bTreeToPaths = mFmt->addAction(QStringLiteral("树 → 路径列表"));
        bTreeToPaths->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
        mFmt->addSeparator();
        QAction *bCenter = mFmt->addAction(QStringLiteral("居中"));
        bCenter->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
        fa->addSeparator();
        // ── 工具区：摹/空、阴/阳、涂/擦/消 ──
        QAction *bMo = fa->addAction(QStringLiteral("摹"));
        bMo->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
        QAction *bKong = fa->addAction(QStringLiteral("空"));
        bKong->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
        fa->addSeparator();
        QAction *bYin = fa->addAction(QStringLiteral("阴"));
        QAction *bYang = fa->addAction(QStringLiteral("阳"));
        bYin->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
        bYang->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
        bYin->setCheckable(true);
        bYang->setCheckable(true);
        fa->addSeparator();
        QAction *bTu = fa->addAction(QStringLiteral("涂"));
        bTu->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
        QAction *bCa = fa->addAction(QStringLiteral("擦"));
        bCa->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
        QAction *bXiao = fa->addAction(QStringLiteral("消"));
        bXiao->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
        bTu->setCheckable(true);
        bCa->setCheckable(true);
        QAction *bHold = fa->addAction(QStringLiteral("按住 Shift 拖动＝按住笔刷"));
        bHold->setEnabled(false);
        fa->addSeparator();
        // ── 字符缩放/笔刷缩放（不做成真键等效：系统接管会毁掉按住加速）──
        QMenu *mCharZoom = fa->addMenu(QStringLiteral("字符缩放"));
        QAction *bZoomIn = mCharZoom->addAction(QStringLiteral("字号放大 ⌘="));
        QAction *bZoomOut = mCharZoom->addAction(QStringLiteral("字号缩小 ⌘-"));
        QAction *bZoom0 = mCharZoom->addAction(QStringLiteral("字号复位 ⌘0"));
        QMenu *mBrushZoom = fa->addMenu(QStringLiteral("笔刷缩放"));
        QAction *bBrushIn = mBrushZoom->addAction(QStringLiteral("笔刷加粗 ⇧⌘="));
        QAction *bBrushOut = mBrushZoom->addAction(QStringLiteral("笔刷变细 ⇧⌘-"));
        QAction *bBrush0 = mBrushZoom->addAction(QStringLiteral("笔刷复位 ⇧⌘0"));
        fa->addSeparator();
        // ── 字体区（子菜单）──
        QMenu *mFont = fa->addMenu(QStringLiteral("字体"));
        QAction *bFontDir = mFont->addAction(QStringLiteral("打开字体文件夹"));
        QAction *bFontPrev = mFont->addAction(QStringLiteral("上一字体"));
        bFontPrev->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+,")));
        QAction *bFontNext = mFont->addAction(QStringLiteral("下一字体"));
        bFontNext->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+.")));
        QAction *bFontReset = mFont->addAction(QStringLiteral("恢复默认字体"));
        fa->addSeparator();
        QAction *bUndo = fa->addAction(QStringLiteral("撤销"));
        bUndo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
        QAction *bRedo = fa->addAction(QStringLiteral("重做"));
        bRedo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Y")));
        fa->addSeparator();
        // ── 生死区：自杀 = 动作；重生 = 登录项代理的全局键（提示）──
        QAction *bSuicide = fa->addAction(QStringLiteral("死"));
        bSuicide->setShortcut(QKeySequence(QStringLiteral("Ctrl+Meta+N")));
        QAction *bRebirth = fa->addAction(QStringLiteral("生 ⌃⇧⌘N"));
        bRebirth->setEnabled(false); // 全局热键由 naught-agent 登录项持有
        // 快捷键上下文 = ApplicationShortcut：无需窗口/菜单先被点开即
        // 可触发（旧版 WindowShortcut + 无窗口 = 要等菜单点开一次）
        const std::function<void(QMenu *)> setCtx = [&](QMenu *m) {
            for (QAction *a : m->actions()) {
                if (a->menu())
                    setCtx(a->menu());
                else
                    a->setShortcutContext(Qt::ApplicationShortcut);
            }
        };
        setCtx(fa);
        QObject::connect(bSuicide, &QAction::triggered, &editor, [&editor] { editor.commitSuicide(); });
        QObject::connect(bMo, &QAction::triggered, &editor, [&editor] { editor.mo(); });
        QObject::connect(bKong, &QAction::triggered, &editor, [&editor] { editor.kong(); });
        QObject::connect(bYin, &QAction::triggered, &editor, [&editor] { editor.setDark(true); });
        QObject::connect(bYang, &QAction::triggered, &editor, [&editor] { editor.setDark(false); });
        QObject::connect(bTu, &QAction::triggered, &editor, [&editor] { editor.toggleMode(Editor::Mode::Draw); });
        QObject::connect(bCa, &QAction::triggered, &editor, [&editor] { editor.toggleMode(Editor::Mode::Erase); });
        QObject::connect(bXiao, &QAction::triggered, &editor, [&editor] { editor.clearInk(); });
        QObject::connect(bBian, &QAction::triggered, &editor, [&editor] { editor.toggleCodeMode(); });
        QObject::connect(bXian, &QAction::triggered, &editor, [&editor] { editor.toggleCrt(); });
        QObject::connect(bLock, &QAction::triggered, &editor, [&editor] { editor.toggleViewLock(); });
        QObject::connect(bGreen, &QAction::triggered, &editor, [&editor] { editor.toggleMachine(); });
        QObject::connect(bDeclare, &QAction::triggered, &editor, [&editor] { editor.declareArtFromSelection(); });
        QObject::connect(bFontDir, &QAction::triggered, &editor, [&editor] { editor.openFontFolder(); });
        QObject::connect(bFontPrev, &QAction::triggered, &editor, [&editor] { editor.cycleCrtFont(-1); });
        QObject::connect(bFontNext, &QAction::triggered, &editor, [&editor] { editor.cycleCrtFont(+1); });
        QObject::connect(bFontReset, &QAction::triggered, &editor, [&editor] { editor.restoreDefaultFont(); });
        QObject::connect(bYan, &QAction::triggered, &editor, [&editor] { editor.yan(); });
        QObject::connect(bGe, &QAction::triggered, &editor, [&editor] { editor.ge(); });
        QObject::connect(bCenter, &QAction::triggered, &editor, [&editor] { editor.centerToWidth(); });
        QObject::connect(bBoxSingle, &QAction::triggered, &editor, [&editor] { editor.formatBox(0); });
        QObject::connect(bBoxDouble, &QAction::triggered, &editor, [&editor] { editor.formatBox(1); });
        QObject::connect(bBoxRound, &QAction::triggered, &editor, [&editor] { editor.formatBox(2); });
        QObject::connect(bBoxBold, &QAction::triggered, &editor, [&editor] { editor.formatBox(3); });
        QObject::connect(bJoin, &QAction::triggered, &editor, [&editor] { editor.joinLinesTo(); });
        QObject::connect(bSplit, &QAction::triggered, &editor, [&editor] { editor.restoreLines(); });
        QObject::connect(bPathsToTree, &QAction::triggered, &editor, [&editor] { editor.pathsToTree(); });
        QObject::connect(bTreeToPaths, &QAction::triggered, &editor, [&editor] { editor.treeToPaths(); });
        QObject::connect(bZoomIn, &QAction::triggered, &editor, [&editor] { editor.zoom(1); });
        QObject::connect(bZoomOut, &QAction::triggered, &editor, [&editor] { editor.zoom(-1); });
        QObject::connect(bZoom0, &QAction::triggered, &editor, [&editor] { editor.zoomReset(); });
        QObject::connect(bBrushIn, &QAction::triggered, &editor, [&editor] { editor.brushUp(); });
        QObject::connect(bBrushOut, &QAction::triggered, &editor, [&editor] { editor.brushDown(); });
        QObject::connect(bBrush0, &QAction::triggered, &editor, [&editor] { editor.brushDefault(); });
        QObject::connect(bUndo, &QAction::triggered, &editor, [&editor] { editor.undoAll(); });
        QObject::connect(bRedo, &QAction::triggered, &editor, [&editor] { editor.redoAll(); });
        QObject::connect(fa, &QMenu::aboutToShow, &editor, [&editor, bYin, bYang, bTu, bCa, bBian, bXian, bLock, bGreen, bDeclare] {
            bYin->setChecked(editor.isDark());
            bYang->setChecked(!editor.isDark());
            bTu->setChecked(editor.mode() == Editor::Mode::Draw);
            bCa->setChecked(editor.mode() == Editor::Mode::Erase);
            bBian->setChecked(editor.codeMode());
            bXian->setChecked(editor.crtOn());
            bLock->setChecked(editor.crtViewLocked());
            bLock->setEnabled(editor.crtOn()); // 锁定只在显会话内有意义
            bGreen->setEnabled(editor.crtOn());
            bDeclare->setChecked(editor.asciiArtActive()); // 画布编辑态可见
        });
    }
#endif

    return app.exec();
}
