# naught(無)交接 prompt —— 给接手 agent 的完整上下文

> 本文件 = 一份可直接喂给新 agent 的 prompt。把下面的全部内容给它,它会自己开工。
> 规则(用户钦定):**不虚报完成**——任何一项只有用户确认过才算"修好";修完用暴力测试验证,再部署+推送。

---

## 0. 项目是什么

macOS 写作应用「無」(naught),C++/Qt 6.9.3。核心卖点 = 「显」(Cmd+T):把编辑器变成 1975–1982 年老 CRT 显示器的拟真画面(琥珀磷光/绿磷/C64 真彩/IBM PC 白磷四机可切,Cmd+Shift+M)。
另有「涂」(Cmd+D 画笔)/「擦」(Cmd+E 橡皮)画布层,可与文字混用。
设计宪章 = docs/crt-filter.md(零 UI、单一模式、纯画面拟真、不毁打字)。

**用户钦定的四项目标(最高优先级,高于一切优化):流畅、无 bug、显示正确、功能稳定。**
体积优化/性能优化/模块化都以不伤害这四条为前提——任何"为了 KPI 的优化"都会被骂。

## 1. 仓库与分支现状(2026-09,重要)

- 远端:https://github.com/2liver/naught (用户 = 2liver)
- **main 分支 = 受保护,禁止强推**(GH006)。当前 main 顶端 = `e2222df`(一个 revert 提交,文件内容 = v0.3.14)。
- **v0.3.14 = 稳定版**(多平台发布:macOS DMG/zip + Windows zip + Linux AppImage,三平台 CI 全绿)。用户拍板:这是"尺子",main 必须保持它。
- **preview 分支 + 标签 v0.3.15-preview = 实验/预览工作线**(体积优化后的 CRT 深度修复)。后续修复都提交到 preview,稳定后用户确认再决定怎么合。
- CI:release 管线只对指向 main 尖端的 tag 全量构建发布;preview 的 tag 不会自动发版。

## 2. 用户三轮反馈全记录(都是要修的)

### 第一轮(已修,有回归闸)
1. 显模式倒影(错开白影):辉光在屏幕空间采样、内容在视差空间采样——已修(辉光改 cuv)。
2. 字体往上弧斜:桶形畸变违反宪章"不做桶形畸变(毁打字)"——已删,视差纯平移;锁定视角 (0,0);顶部内容被内缩补丁裁掉 ~5px——已删内缩。
3. C64 ⌘0 窗口化字号反超全屏——第一轮修成"网格跟随窗口"。

### 第二轮(已修,有回归闸)
1. 撤销/重做对不上("撤回的并非想撤回的"):旧双栈靠 m_lastWasInk 猜上一操作类型,交叉序列撤错目标——已改为**时间序统一撤销日志**(editor.h:m_undoOps/m_redoOps,true=文字步/false=墨迹步)+ m_inUndoRedo 重入保护(撤销自身触发 contentsChange 会清空重做日志)。
2. 显模式删字/中文输入法落字卡 1 秒:回读看门狗每次 800ms 超时即整管线重建 = 冻结——已改轻恢复(作废在途回读+强制重拍,连续三次才重建)+ 性能埋点 watchdogFires/maxReadbackMs。
3. ⌃⇧⌘T 暗角黑边框感:ent 专属斜向玻璃反光 0.03 浮点强度 = +19 亮度(暗底上 +105% 刺眼斜带)——已删;主反光带基准角 normalize(0.35,0.94) 使 dot=1.0 带飞出屏外——已改 normalize(0.30,0.60);bezel 改 15% 宽柔和渐散;有数值闸(edge≥50%/corner≥60%/断崖≤45%)。
4. 环形/自交笔迹空心回归、涂模式打字回归、90 步混合混沌闸(selftest.cpp)。

### 第三轮(最新,大部分已修,见 §4 剩余)
1. C64 ⌘0:**已修**——恢复全屏封顶 16(m_size,像素 20px)为"尺子",窗口化 = 尺子×(窗口宽/屏宽),严格按比例小于全屏。见 editor.h crtGridSize()。
2. 显模式 Shift 笔刷松键黑屏一会:**已修**——根因 = 画刷会话半分辨率切换在会话边界整管线重建(黑屏);已移除该切换。闸 = CRT-SHIFT-STORM(间隔开关 Shift 拖动画笔,零看门狗+文字带常亮)。顺带:用户喜欢 Shift 按住时的"发光/朦胧"质感,已移植为**常驻字体光效**(shaders/crt.frag 里 18% 帐篷模糊混合,需用户微调浓度,不能糊分辨率)。
3. 显模式仍卡(删除/⌃⇧N 清笔迹/Shift 选区/方向键/打字光标):已撤 50ms 链节流 + 快照节流只拦环境拍(脏帧即时重拍)+ 子代理审计找到的画布缓存"每次滚动整窗重烘"(方向键卡顿主因,已修为只随笔迹内容变化重烘)+ 撤销日志与 Qt 栈失步三处(200 上限/墨栈 100 错位截断/程序性重写清栈不清日志,已修)。**待用户实机确认**。
4. "擦除填实心"作为功能保留(用户拍板):已还原 splitSubpaths 洞环并回(填实来源)+ 小环+橡皮盖洞界的填实流程探针复现 + 擦 ∞ 交叉点闸改为锁定填实。撤销粒度:多实心单笔拖动会话 = 逐实心撤销,回归闸已锁(CRT-FILL-UNDO)。**剩余**:实心内部无法直接擦除(子代理审计中,几何 = 穿孔的洞界碎片被单独填充盖回)。
5. 撤销顺序乱:确定性时间线测试已加并通过(画一笔→打字→擦掉笔迹→撤销×3 精确回放)。多实心粒度见 #4。
6. "优化越来越差":已回退节流与画刷半分辨率。**待用户确认**。
7. ⌃⇧⌘T 滚动条灰块伪影:**已修**——灰块 = 余晖 max 模型把中灰把手的旧位置涂抹成残影;滚动期余晖清零(histPrimed 加 !scrolling)。
8. 常规模式被渗透(选中/打字光标/方向键等"等等"):**待修**——子代理逐行对照 v0.3.14 审计中,必须全部找出来。
9. GitHub 回滚:**已做**——main = revert 到 v0.3.14,preview 分支+标签存当前版。
10. 画布越画越卡:**已修**——canvas.h 笔迹烘焙 QPixmap 缓存(paintEvent 只 blit+活跃笔画),失效点:restore/endStroke/clearAll/applyErase/setInk(滚动不再失效——平移 blit)。
11. 常规模式画画 Shift 来回画线变卡:同 #10 的缓存修复覆盖,**待确认**。
12. 交接 prompt = 本文件。

## 3. 已修清单与验证方式(别重复劳动)

- selftest(离屏,Metal 真跑):`SELFTEST=1` 等价于 `--selftest`。包含:暴力几何闸(琥珀+C64 双机,文字带顶部/连续/下半帧零亮行)、倒影 ghost 闸、Y 翻转闸、暗角渐散闸、输入突发闸(零看门狗)、Shift 风暴闸、时间线撤销闸、环形/自交/擦交叉空心闸、C64 ⌘0 真实键路径闸、90 步混沌闸、CRT 色彩/扫描线/衍射黄金参考对照。
- 验证矩阵:release DPR1 + DPR2 + ASAN 全绿才算过。

## 4. 待修清单(按优先级)

1. **实心内部擦除**(§2 第三轮 #4a):填实后橡皮无法从内部穿孔(只能外部擦入)。已复现(探针:穿孔的洞界碎片被单独填充盖回)。候选修法:splitSubpaths 只把"完整闭合且被宿主包含"的洞环并回(穿孔 = 完整闭合洞 → 并回保洞;填实的洞界 = 被管切碎的开放弧 → 不并回 → 保留填实)。
2. **常驻光效微调**:crt.frag 的 18% 帐篷模糊等用户拍板浓度。
3. 用户的其余"等等"——发现一个修一个,不许漏。

## 5. 构建/测试/部署命令(全在这台机器上验证过)

```bash
# 工具路径
CM=/Users/2liver/Library/Python/3.9/lib/python/site-packages/cmake/data/bin/cmake
NINJA=/Users/2liver/Library/Python/3.9/bin/ninja
QT=/Users/2liver/Qt/6.9.3/macos

# 构建(preview 工作线)
$CM --build build --parallel
# ASAN
$CM --build build-asan --parallel

# 离屏自检
env -i HOME=$HOME QT_QPA_PLATFORM=offscreen DYLD_FRAMEWORK_PATH=$QT/lib \
  build/naught.app/Contents/MacOS/naught --selftest
# DPR2
env -i HOME=$HOME QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 \
  DYLD_FRAMEWORK_PATH=$QT/lib build/naught.app/Contents/MacOS/naught --selftest
# ASAN
env -i HOME=$HOME QT_QPA_PLATFORM=offscreen DYLD_FRAMEWORK_PATH=$QT/lib \
  ASAN_OPTIONS=detect_leaks=0 build-asan/naught.app/Contents/MacOS/naught --selftest

# 部署到启动台（预览版）
$CM --install build --prefix "$HOME/Applications"
APP=~/Applications/naught.app
install_name_tool -change /Users/2liver/kf6/lib/libKF6SyntaxHighlighting.6.dylib \
  @executable_path/../Frameworks/libKF6SyntaxHighlighting.6.31.0.dylib \
  $APP/Contents/MacOS/naught
$QT/bin/macdeployqt $APP -always-overwrite
mkdir -p $APP/Contents/PlugIns/platforms
cp $QT/plugins/platforms/libqoffscreen.dylib $APP/Contents/PlugIns/platforms/
codesign --force --deep --sign - $APP
codesign --verify --deep --strict $APP
env -i HOME=$HOME QT_QPA_PLATFORM=offscreen $APP/Contents/MacOS/naught --selftest

# 稳定版 v0.3.14 部署（回滚后的 main）
# 工作树在 /tmp/naught-stable（git worktree add /tmp/naught-stable v0.3.14），构建目录 /tmp/naught-stable/build
```

注意坑:
- 着色器 = qsb 预编译进资源(qt_add_shaders),改 shaders/*.frag 后必须重建。
- 探针工具:shaderLog 写 /tmp/naught-crt-shader.log。
- 独立 QRhi 探针在 /tmp/rhi_probe2.cpp、/tmp/canvas_probe*.cpp(定位几何/着色器问题的有效手段,probe 失败多半是 cwd 要在仓库根)。
- Python 改 C++ 源码时:\n 在 C++ 字符串里必须写成 \\n(吃过三次亏)。
- 编辑 editor.h 的机器人大段替换后务必整跑 selftest。

## 6. 文件地图

- editor.h(2754 行,上帝对象但已减负)/ selftest.cpp(2242 行,全部回归闸)
- crt_view.cpp/.h —— QRhi/Metal 离屏 CRT 管线(快照→persist→辉光→主着色→回读)
- shaders/*.frag/.vert —— GLSL 450,qsb 编译
- canvas.h —— 画布层(涂/擦,QPixmap 烘焙缓存,洞环合并)
- snapshot_compositor.cpp/.h —— 快照合成器(视口+画布+行号+滚动条)
- crt.cpp/.h —— CPU 黄金参考(辉光/余晖/衍射,自检对照)
- main.mm —— 入口/菜单/快捷键/登录项代理
- docs/crt-filter.md —— 设计宪章

## 7. 网络/账号环境(交接给新 agent 时按此操作)

- GitHub:github.com/2liver/naught;git 走 https,凭据已存(git push 无需交互)。
- main 分支受保护:不能强推;要回退只能 revert 提交;发布靠指向 main 尖端的 tag。
- CI:GitHub Actions(build + release 双管线),release 全量构建发布只对 main 尖端 tag。
- **clash 代理相关配置细节本会话未记录到——如果网络操作(推送/下载)失败,先检查代理状态;具体 clash 配置请用户补充。**

## 8. 交接规则(用户钦定)

1. 不虚报:任何修复都要暴力测试(混合操作+随机种子+不变量断言)证明,并且**用户实机确认才算修好**。
2. 改动先修 bug 再谈优化;四项目标(流畅/无 bug/显示正确/功能稳定)优先于体积/性能/模块化。
3. 每批改动:全矩阵(release DPR1/DPR2 + ASAN)全绿 → 提交 preview → 部署启动台 → 推 GitHub。
4. 用户报的新 bug 一律先复现(独立探针/落盘取证/逐块二分),再修,再锁回归闸。
