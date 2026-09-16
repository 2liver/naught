// naught-agent —— 重生代理（M6）：纯 Cocoa/Carbon 登录项应用，无 Qt 依赖。
// 持 ⌃⇧⌘N 全局热键；触发时 LSOpen 并排的 naught.app（死亡=重生，在跑=激活）。
// LSUIElement：无 Dock 图标、无菜单栏，但作为真正的登录项应用运行，
// 完整接入窗口服务器事件通道——launchd 裸进程收不到系统热键投递，
// 这正是此前"注册成功却无反应"的根因。
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>

static void agentLog(NSString *msg)
{
    NSString *dir = [NSHomeDirectory()
        stringByAppendingPathComponent:@"Library/Application Support/naught"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir
                              withIntermediateDirectories:YES
                                               attributes:nil error:nil];
    NSString *path = [dir stringByAppendingPathComponent:@"agent.log"];
    NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:path];
    if (!fh)
        fh = [NSFileHandle fileHandleForWritingAtPath:path];
    if (!fh) {
        [[NSData data] writeToFile:path atomically:YES];
        fh = [NSFileHandle fileHandleForWritingAtPath:path];
    }
    if (fh) {
        [fh seekToEndOfFile];
        NSString *line = [NSString stringWithFormat:@"%@: %@\n",
                          [NSDate date], msg];
        [fh writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
        [fh closeFile];
        // 轮转：超 64KB 截断（防无限增长）
        NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
        if ([[attrs objectForKey:NSFileSize] longLongValue] > 65536) {
            NSFileHandle *tf = [NSFileHandle fileHandleForWritingAtPath:path];
            [tf truncateFileAtOffset:0];
            [tf closeFile];
        }
    }
}

static OSStatus hotKeyHandler(EventHandlerCallRef, EventRef, void *user)
{
    NSString *mainApp = (__bridge NSString *)user;
    agentLog([@"hotkey fired: opening " stringByAppendingString:mainApp]);
    NSURL *url = [NSURL fileURLWithPath:mainApp];
    CFURLRef cfurl = (CFURLRef)CFBridgingRetain(url);
    const OSStatus rc = LSOpenCFURLRef(cfurl, NULL);
    CFRelease(cfurl);
    agentLog([NSString stringWithFormat:@"LSOpen result: %d", (int)rc]);
    return noErr;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        // 单实例：flock 锁文件（进程退出自动释放）
        NSString *lockPath = [NSHomeDirectory()
            stringByAppendingPathComponent:
                @"Library/Application Support/naught/agent-app.lock"];
        [[NSFileManager defaultManager] createDirectoryAtPath:[lockPath stringByDeletingLastPathComponent]
                                  withIntermediateDirectories:YES attributes:nil error:nil];
        int fd = open([lockPath fileSystemRepresentation], O_CREAT | O_RDWR, 0644);
        if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) != 0) {
            agentLog(@"another agent alive, exiting");
            return 0;
        }

        // 主应用 = 上溯三级（代理嵌套于 naught.app/Contents/Resources/
        // naught-agent.app——嵌套即不进启动台，分发的"幽灵"代理）
        NSString *agentBundle = [[NSBundle mainBundle] bundlePath];
        NSString *mainApp = [[[agentBundle stringByDeletingLastPathComponent]
                              stringByDeletingLastPathComponent]
                             stringByDeletingLastPathComponent];
        NSString *keep = mainApp; // 回调数据（进程存续期持有）

        // ⌃⇧⌘N：被占不退出——5 秒后重试，成功即装 handler（常驻等让位）
        __block EventHotKeyRef hotKey = NULL;
        const EventHotKeyID hotKeyId = { 'nagt', 1 };
        void (^tryRegister)(void) = ^{
            const OSStatus st2 = RegisterEventHotKey(kVK_ANSI_N, cmdKey | controlKey | shiftKey,
                                                     hotKeyId, GetApplicationEventTarget(),
                                                     0, &hotKey);
            if (st2 == noErr) {
                const EventTypeSpec spec = { kEventClassKeyboard, kEventHotKeyPressed };
                InstallEventHandler(GetApplicationEventTarget(),
                                    NewEventHandlerUPP(hotKeyHandler), 1, &spec,
                                    (__bridge void *)keep, NULL);
                agentLog(@"agent up (registered)");
            } else {
                agentLog([NSString stringWithFormat:@"hotkey busy (%d), retry in 5s", (int)st2]);
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC),
                               dispatch_get_main_queue(), tryRegister);
            }
        };
        tryRegister();
        agentLog([@"agent up: " stringByAppendingString:mainApp]);
        [NSApp run]; // 登录项应用的事件循环：Carbon 热键在此派发
    }
    return 0;
}
