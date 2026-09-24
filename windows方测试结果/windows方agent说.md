隔壁agent说：“修好了，而且根因和你猜的不一样——**不是 CRT 着色器的问题，是 QRhi 后端探测时传了空指针**。

## 根因

`crt_view.cpp` 的 `ensureRhi()` 对所有后端一律调用 `QRhi::create(impl, nullptr)`，但 QRhi 每个后端的构造函数**无条件解引用这个参数**（Qt 6.9 源码）：`qrhivulkan.cpp` 第一行就是 `inst = params->inst;`。

Windows 上 `QT_FEATURE_metal = -1`，Metal 探测安全返回 nullptr 后，**探测链第二站就是 Vulkan → 当场空指针崩溃**。macOS 上 Metal 排第一且成功，永远走不到第二站——所以这 bug 只在 Windows 暴露。

## 证据

你机器的事件日志里记录着你那份分发包的崩溃：

```
错误模块: Qt6Gui.dll    异常代码: 0xc0000005
错误偏移量: 0x4d79e6   路径: ...\Downloads\naught-windows\naught.exe
```

我用**你那份原封不动的分发包**跑出**逐字节相同**的偏移量 `0x4d79e6`。再用真实窗口 + 合成按键实测：修复前按 Ctrl+T 进程当场消失，修复后整段「显」会话（进出/换机/视角锁定/隔 8 秒重进）全部存活。

## 顺带修掉的

1. **「显」入场黑屏 1.5 秒 + 之后每帧刷一条告警** —— 黑帧拦截器只按亮度阈值判，于是「阳」换肤成暗底、空文档下块光标眨眼都被误判成"视口渲染坏了"。
2. **退出「显」超 6 秒再进来白做两次整管线重建** —— 活性看门狗跨会话残留，且 `paintEvent` 与 `renderFrame` 各触发一次。
3. **CI 漏洞（这才是事故真正成因）** —— `build.yml` 和 `release.yml` 的 Windows 自检把 `vulkan,d3d11,d3d12,gles2` **全跳过**，只能落到 Null 后端（对一切说"成功"却不产真帧），所以这个闪退在 CI 全绿的情况下发到了你手上。

## 给你的东西

- [naught-windows-fix-报告.md](C:\Dev\naught-windows-fix-报告.md) —— 直接丢给 mac 端 agent 的交接报告
- [naught-windows-fix.patch](C:\Dev\naught-windows-fix.patch) —— 已验证能干净 `git am` 到 `origin/main` 全新克隆
- [naught-windows-fixed.zip](C:\Dev\naught-windows-fixed.zip) —— 立即可双击测试的修复包

## 两点必须提醒

- **Linux 版也是坏的**（Vulkan 排第一，同样崩），这次一并治好了，但我这边只能验 Windows。
- 我打的包里**没有 KF6SyntaxHighlighting.dll**（本机没 KF6），所以「编」会退化成等宽+行号。正式分发务必走你的 `release.yml`，它会从源码构建 KF6——exe 是不是带高亮，取决于构建时找不找得到 KF6。

测试矩阵 9 项全过：完整自检（真 D3D11/D3D12 全部 GPU 像素断言）、CI 冒烟、DPR 1.5/2.0、`--stress`、`--bench`，以及真实 GUI 的尺寸拖拽、右键菜单、关窗退出。其中"故意退回 Null 必须失败"那条是**红的**——那是新加的 CI 硬闸按预期拦住了，不是故障。”
