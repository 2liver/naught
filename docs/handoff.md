# naught(無)交接 prompt —— 给接手 agent 的完整上下文

> 本文件 = 一份可直接喂给新 agent 的 prompt。把下面的全部内容给它,它会自己开工。
> 规则(用户钦定):**不虚报完成**——任何一项只有用户确认过才算"修好";修完用暴力测试验证,再部署+推送。

---

## 0. 项目是什么

macOS 写作应用「無」(naught),C++/Qt 6.9.3。核心卖点 = 「显」(Cmd+T):把编辑器变成 1975–1982 年老 CRT 显示器的拟真画面(琥珀磷光/绿磷/C64 真彩/IBM PC 白磷四机可切,Cmd+Shift+M)。
另有「涂」(Cmd+D 画笔)/「擦」(Cmd+E 橡皮)画布层,可与文字混用。
设计宪章 = docs/crt-filter.md(零 UI、单一模式、纯画面拟真、不毁打字)。

**用户钦定的四项目标(最高优先级,高于一切优化):流畅、无 bug、显示正确、功能稳定。**

## 0. 项目冻结（v0.3.16 收官）

- **冻结生效**：用户 Mac 送修中。回来后分两台机子做分发：**本机（Intel Mac）= 继续负责 macOS**；**修好的电脑 = 负责 Windows 和 Linux**。M1/arm64 架构暂不考虑。
- **归档待办（先不做，已记）**：
  1. **分辨率自适应**：按电脑分辨率自动调整字体尺寸（跨显示器的自动 ⌘0）。
  2. **跨平台「项」入口 + 鼠标适配性**：Windows/Linux 没有打开「项」（偏好）的地方；鼠标交互（与键盘交互）适配未做。Windows 目前右键菜单是唯一功能入口。
- **Windows 方交接**（`windows方测试结果/` 文件夹）：Windows 11 实机 + 真 GUI 按键实测——「显」闪退真根因 = `QRhi::create(impl, nullptr)` 空参数（各后端构造函数无条件解引用；macOS 因 Metal 优先从未触发，Linux 同样必崩）。修复（已 `git am` 并入 `39511c3`）：每后端配真实 InitParams / 探测链平台原生优先 / 黑帧拦截加语义闸 + 到期接受新基线 / showEvent 开新会话 + paintEvent 看门狗对称 invalidate / CI Windows 不再全跳真后端 + `NAUGHT_REQUIRE_GPU=1` 硬闸。Windows 实测矩阵 9 项全过（完整自检真 D3D11/D3D12、CI 冒烟、DPR1.5/2.0、stress/bench、真实 GUI 全套场景）；macOS 三模式 + ASAN 复验过。**遗留观察**（未改，防御性记录）：①`m_readbackGen` 在 `resetPipeline()` 不自增（迟到回读会画一帧旧内容，暂无害）；②回读回调 `QRhiReadbackResult*` 在回调永不触发时会泄漏（实测未触发）；③`d3dcompiler_47.dll` 是 Windows 运行期硬依赖（手工拼包勿漏）。
体积优化/性能优化/模块化都以不伤害这四条为前提——任何"为了 KPI 的优化"都会被骂。

## 1. 仓库与分支现状(2026-09,重要)

- 远端:https://github.com/2liver/naught (用户 = 2liver)
- **main 分支 = 受保护,禁止强推**(GH006)。v0.3.15 起 main = preview 合并线(Shift+方向键选区语义全面修复 + CRT 修复沉淀)。旧稳定线 `e2222df`(revert 提交)打标签 `v0.3.14-stable` 存历史。
- **v0.3.14 = 历史稳定版**(多平台发布:macOS DMG/zip + Windows zip + Linux AppImage,三平台 CI 全绿),GitHub Release 页保留。
- **preview 分支 = 新功能预览工作线**,后续修复仍先提交 preview,稳定后用户确认再合 main。
- **启动台单版本**(v0.3.15 统一):~/Applications/naught.app =「無」正式版 0.3.15(com.2liver.naught)。预览版 naught-preview.app 已退役删除(LaunchServices 反注册 + 偏好/存档状态清理)。构建目录已加 .noindex(防 Spotlight 把 build 的 naught.app 抢注)。
- **⌃⇧⌘N 愿景(用户五轮记录)**:代理(naught_agent.mm)枚举 ~/Applications 下 naught*.app 前缀逐个 LSOpen——单版本时代即召唤唯一正式版(改名成别的要同步改代理枚举前缀)。
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
4. "擦除填实心"作为功能保留(用户拍板):已还原 splitSubpaths 洞环并回(填实来源)+ 小环+橡皮盖洞界的填实流程探针复现 + 擦 ∞ 交叉点闸改为锁定填实。撤销粒度:多实心单笔拖动会话 = 逐实心撤销,回归闸已锁(CRT-FILL-UNDO)。**已修**:实心内部穿孔根修 = 橡皮会话从起点快照重放全集减法 + splitSubpaths 只并"闭合的新洞"(穿孔保洞、填实碎片开弧不并 → 填实功能保留)。闸:CRT-PUNCH(画环→填实→内部拖动穿孔→圈心透明)。
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

## 3.5 第五轮反馈(待修/已修)

1. 方向键光标隐形移动 + Shift 无法选中(常规/显都有,存亡级)——**已修**:①眨眼定时器 setSingleShot(true) 但处理器从不重启(唤醒后 750ms 永久熄灭 = 隐形根因)——改周期触发;②cursorPositionChanged 只标脏区不 markDirty(显模式块光标/选区冻结)——补 markDirty。闸:820ms 后光标仍亮。
2. 滚动条灰伪影(离窗重进后)——已修:滚动条淡出期余晖清零(CrtConfig.fading + histPrimed)。
3. 橡皮擦回归纯擦除——已修:填实功能整体移除(isFillStart/m_fillSession/fillMode 删除,splitSubpaths 并所有闭合洞界 = 并集纯切)。两个橡皮 bug 已修(子代理状态机审计):toggleMode 切换前终结会话(endStroke+eraseEnd+endInkSession+释放 grab)+ clearAll 复位擦除态;闸 = 纯 Shift 跨模式两序列(NoButton+ShiftModifier 配方)。
4. 视差上下对调——已修:crt.frag curve() 的 c.y 改 +=。
5. 架构答问——见下 §5.5。
6. 平铺式来回画线卡顿——已修:canvas.h 活跃笔画增量轮廓缓存(旧版每帧 O(n²) 重算整条轮廓)。
7. Shift 多行选中"从中间一块一块出来"——已修:cursorPositionChanged 里选区变化 → markSnapshotFullDirty() 全量重拍(增量脏区逐块补 = 渲染不连续)。**待用户实机确认**。
8. 滚动条灰伪影·离窗重进仍现——已修:ZenScrollBar::hovered 悬停加宽瞬间 flushHistory()(把手几何瞬变冲刷余晖)。**待用户实机确认**。
9. ⌘B 换行后行号晚一拍(不显示,得再换一行前一行的才出来)——**已修**:根因 = 增量脏区以视口为锚(x ≥ 槽宽),行号槽永远不在脏区内,换行/软换行后下方行号永不重绘,只有光标激发光环偶尔擦进槽内才"晚一步"出现。computeDirty 左缘拉到 0(行号槽随文字一起重绘);非显模式补 m_lineNumberArea->update()。闸:Enter + 软换行两腿,增量帧与强制全量帧行号槽逐像素一致(负向验证:旧代码 diff=42 被抓住)。
10. 光标残影(各机型颜色不同)——**已修并重新锁闸**:光标从快照移除、改 CrtView 顶层叠加(Difference 反相)后残影不可能;旧闸在 m_shown 采样旧光标格底带,被机型 3 白磷字辉光污染(DPR2 必炸、且闸跑在非显模式量的是陈旧帧)。新闸 = 快照不随光标位置变化(同内容两次全新快照逐像素相同,显模式 + 焦点 + 亮拍下负向验证:临时恢复 paintCursor 进快照 → snapDiff=24 被抓住)。滚动灰闸补"新帧落地"等待(旧实现只等 readbackIdle,空窗期把旧帧当新帧扫,DPR2 偶发灰字误报)。

## 3.6 第六轮反馈(用户正式测试第一轮)

0. 码一行字 + 方向键左右落光标 → 闪退。栈 = `CrtView::renderFrame` 的 `uploadTexture` 空指针。方向键标脏(五轮 #1 修复)驱动帧循环，踩中半残管线(看门狗/resize 中途 releaseGpu+ensureRhi 失败后仍拿空纹理/空批次上传)。根修 = renderFrame/ensureRhi 全路径失败即弃帧重试：看门狗重建后重验资源、resize 分支重验 m_r/m_ps/m_snapTex/m_ubuf、批次/纹理/图像三守卫、ensureRhi 六处创建失败统一 `releaseGpu+delete m_r` 归零(不留半残)。闸:CRT 段内打字+方向键+选区+resize 60 步风暴,画面仍活文字完整。**实机待用户确认**。
1. 非首行空行上 ⌘F → 闪退。根因 = `ge()` 对空 `lines` 向量取 `last()/first()` 越界(空行全被跳过 = 无内容行)。修复 = `lines.isEmpty()` 即返回(空行 = 隔板,语义不动)。负向验证:去掉守卫 → SIGSEGV 精确复现。闸:GE-EMPTY。
2. 首行很多功能无法触发(⌘⇧C 等)。根因 = 仅挂菜单 QAction 的功能键(QMenuBar(nullptr) 从未挂窗/显示, QAction 不进 QShortcutMap → macOS 死键):居中/四种框/压行/还原/路径树互转/字体循环全死。病根修复 = 全部收进 keyPressEvent 单一路径(与 ⌘B/⌘T 同款);顺手修同病根的 ⌘A 被无条件 return 吞掉(全选失而复得);⌘F 首行补"上方空行"(旧代码首行永远少隔离)。闸:DEAD-KEYS 逐键走真实键路径 + 裸 ⌘C/⌘V 复制粘贴不回归。
3. 暴力乱测 = selftest 内 NAUGHT_FUZZ 段(固定种子 LCG):打字×方向键×Shift 选区×Enter/Tab×缩放×换机×编模式×功能键×撤销重做×resize 风暴(管线全量重建路径)×清空重打,CRT 开/关两轮各 500 步。DPR1/DPR2 各 20 种子 + ASAN 3 种子全绿。滚动灰闸改为稳态帧扫描(过渡出尽+强制全量重拍后扫——载重下第一帧瞬态余晖会误报)。

## 3.7 第七轮反馈(CRT 打字+删除 → 画面冻结)

症状:打一串字再删掉 → 磷光痕迹删不掉、再打字无字(只占位)、换机底色不变(光标形状随换机变 = 叠加层活着,但 m_shown 永远停在旧帧);常规模式文字完好。诊断:帧循环死掉(旧帧 + 余晖冻屏),离屏五配方(A 退格删光/B 全选删/C 空/D 覆盖/E 混合)均不复现 → 真机特有路径,最可能 = 管线重建瞬时失败后 m_rhiUnavailable 永久停用渲染层(旧代码"一次失败永不复活")。

根修(自愈三件套 + 心跳看门狗,全部有负向验证):
1. m_rhiUnavailable 改限时死亡:3s 后自动重试(瞬时失败 ≠ 永远失败)。
2. 黑帧拦截改时限上限:连续失字 >1.5s 即如实上传(保留对百毫秒级布局瞬态的拦截,同时杜绝"拦截死循环冻结旧帧")。
3. paintEvent 活性看门狗:6s 无回读落地 → 强制 resetPipeline + 快照作废全量重拍(paintEvent 由光标眨眼 750ms 驱动 = 定时器死了也有心跳)。
4. 全部 qWarning 埋点:复发时日志直接定位是哪条路径。
NAUGHT_FREEZE 探针(五配方 + 三自愈负向验证)留在 selftest 内。**实机待用户确认**。

## 3.8 第七轮追加（用户配方澄清 + Osborne 光标考据）

- 冻结配方（用户两轮澄清）：首行 输字→删光→**空删一大段**→再输再删→空删……**循环直到复现**（多轮累积触发）。离屏：3 轮/20 轮/40 轮循环 + 60 连空删全部健康（DPR1 15 轮全绿，DPR2 待跑）——真机专属。诊断已就位：CrtView 2s 心跳，回读 4s 不落地 → 落盘 **/tmp/naught-freeze-diag.log**（管线状态 + vpLum 视口裸渲染亮度[判视口渲染为空根因] + 快照顶部亮度 + 看门狗计数）+ 自愈锤（视口 update + 快照作废 + resetPipeline）。liveness 看门狗已挪进帧定时器顶部（33ms 恒跳）。
- **Osborne（机型 0）光标考据（用户视频确认）**：实心亮块、字被盖住但**字能见、不是负片**。当前实现（白 Difference 负片）不对——下一轮改：亮块填充 + 字形保持可见（拟：格底填 cursorBlock 亮色、字形像素原样压回或加深，观感 = 亮块里浮着字）。
- C64（机型 2）已改色对调反相（亮蓝块 + 深蓝镂空字），**待用户视频核对**；IBM PC 下划线 = 已确认正确；5100 待考据。
- 剩余：NAUGHT_RECIPE 循环探针（selftest 内，env 触发）已加 40 轮版。

## 3.9 第八轮（用户洞察 + 光标对齐稳定版）

- **"冻结"真相**：用户观察 = 字占位隐形/删不掉/换机慢 = 同一件事——**渲染太慢**，不是冻结（字最终会出来）。根修两刀：①看门狗轻恢复不再作废在途回读（旧代码每次 800ms 超时 gen++ 作废 → 回读永不落地 = 丢弃级联 = 画面不更新；迟到帧落地远好过永不落地，gen++ 只在整管线重建时做）；②黑帧拦截扫描步长 8（大窗口每拍几十毫秒白烧）。
- **光标对齐稳定版（用户拍板：稳定版每机型样式都对）**：块光标 = 稳定版双色反相（块色填格、字形呈底色——亮块里浮着深色字，非白负片）；满格块宽（一律字体标准格宽——旧实现窄字符上缩成细窄竖条 = 用户报的"常规光标那种细窄高亮竖线"）；5150（机型 3）= 空位下划线亮条、**压在字上仍走块效果**（用户："并非下划线在字下"）；编模式块色 = 中性暖白。C64 色对调特例删除（统一稳定版算法）。
- 若实机仍慢：下一步 = 管线简化对齐稳定版单遍（CPU 余晖/辉光 + 单着色器，替换多遍 GPU）。
- 稳定版工作树 /tmp/naught-stable（v0.3.14 系）已加 NAUGHT_RECIPE 探针。

## 3.10 第九轮（实机诊断铁证 → 视口渲染空根修）

- 实机 diag：vpLum=15/765 = **源视口裸渲染为空**（快照失字 = 删不掉/隐形/换机慢的全部根因）。
- 根修：toggleCrt 退出的无守卫 `viewport()->releaseMouse()` 移除（第五轮二分定位过的同类雷：无抓取强释放 → 视口渲染被破坏；收口与 toggleMode 同款——会话终结、真机抓取交 keyReleaseEvent Shift 分支）。
- 光标块宽 = max(标准格宽, 光标位字符推进宽)：窄字符满格块（防细窄竖条）、CJK 双格整字覆盖（用户报"只有半边字"）。
- NAUGHT_CURPROBE 探针入 selftest（CJK/窄字符/打字态三张光标像素取证图）。
- 若"字符色高光窄线"仍在：剩余嫌疑 = 激发辉光 tight 圈边 / IME 组合渲染，等实机再取证。

## 3.11 第十轮（深度审查综合修复——四个子代理 vs 稳定版全量比对）

审查结论汇总：main.mm 两版逐字节相同；预览版 = 稳定版之上的净正向修复集（~15 处真实修复），亏损集中在光标叠加实现、画布烘焙缓存、帐篷模糊浓度、少量边角。本轮已修：
1. **"字符色高光窄线"根修**：光标回快照（审查 V1：顶层叠加在已辉光帧上反相 → 字的光晕环 ±8px 没被反相 = 磷光色窄环；V2：阈值用原始调色板量已处理帧 = 偏色过渡带）。快照内反相经辉光整体调制 = 稳定版观感。
2. **残影根治（新架构）**：persist.frag 加"光标掩膜"（当前格 + 邻格边距不进余晖历史）——光标 = 瞬态 UI 永不进历史，文字磷光拖尾完整保留。闸重写：光标块在当前格 + 移走后旧格复原。
3. 激发辉光软径向渐变（旧三嵌套圆角矩形的硬边 = 窄线来源之一）；删除时激发位在删除区间内即熄灭（"本该被删的字符常驻"根修）。
4. 帐篷模糊 18% → 10%（审查 A1：文字糊的单点来源）。
5. 画布烘焙缓存 DPR 修复（Retina 发糊）+ qCeil（边缘截断）。（分块 tile 化 = 待办）
6. 抓取守卫：toggleMode/toggleCrt 仅"真抓取过才释放"（审查 R1：既防泄漏又防无抓取强释放破坏视口渲染）。
7. ⌘M 裸键落回基类（审查 R2：与 ⌘A 同病根的漏修）。
8. 撤销日志卫生：文字编辑清 m_inkRedo；ascii 暂停路径同步清日志。
9. 部署收口 scripts/deploy-preview.sh（审查 R1：独立 bundle ID 此前只在 commit message/手工 PlistBuddy）。
10. 光标块宽 = 真实选区矩形（审查 R2：非 CJK 机型的 CJK 半字）。

待办（下轮）：画布缓存 tile 化（OOM 风险）、agent 通配全开收窄（用户四轮拍板保留）、crt.frag.full/probe 黄金参考与主 shader 脱节。

## 3.12 第十二轮（待办清空）

1. 画布烘焙缓存 → 分块 tile（512 逻辑px）：内存有界（上下各一笔 = 2 瓦片，旧整幅 = 全文高度 pixmap = OOM）；endStroke 增量烘焙（只烘新笔画覆盖的瓦片，每笔 O(覆盖瓦片) 而非 O(全部笔画)）；Retina DPR 按瓦片设置。
2. agent ⌃⇧⌘N 通配 `naught*.app` → 白名单 {naught.app, naught-preview.app}（误开残留副本修复；未来主版本沿用 naught 前缀自动纳入）。
3. 黄金参考 crt.frag.full/.probe + 三个探针工具（glshader_probe/rhi_probe/analyze_png）= 与主 shader 脱节的孤儿，已删除（git 历史可恢复）。
4. 黑帧拦截 → "突然变暗"判定：只拦从亮变暗的帧（布局瞬态/视口坏），本来就暗的合法帧（空文档/顶部无字）直接放行——旧绝对阈值误拦 1.5s 修复。

## 3.13 第十三轮（画一笔消失——瓦片烘焙坐标 bug）

用户报：画完一笔松 Shift 消失一半，重试整笔消失。根因 = 十二轮瓦片烘焙漏平移：笔迹是文档坐标，直接画进 512 瓦片本地 = 超出首瓦片即被裁（近原点的笔画全在首瓦片内，自检旧闸测不出）。修复 = 烘焙时 translate(-瓦片原点) + setClipRect(瓦片)。闸 TILE-FAR：1000×400 画布 + 横跨 512 边界的 900px 笔画，两端都必须有墨；负向验证：去平移 → 右段 0 墨 → FAIL（抓得住）。

## 4. 待修清单(按优先级)

1. **常驻光效微调**:crt.frag 的 18% 帐篷模糊等用户拍板浓度。
2. 第五轮 #1/#2/#8 待用户实机确认(闸已绿,但按用户规则:实机确认才算修好)。
3. 用户的其余"等等"——发现一个修一个,不许漏。

## 5.5 架构答问(用户五轮问:为什么 bug 多,架构很差吗)

诚实分析:bug 的根源不是"架构差",而是**状态复杂度**。CRT 管线 = 快照合成 × 余晖历史 × 辉光 × 回读 × 视差 × 模式状态机 × 双栈撤销 × 画布几何——每一层都与其他层有交互(余晖×滚动条把手、Shift 会话×模式切换、撤销×程序性改写、减法×拆分子路径)。v0.3.14 简单(CPU 画家+简单撤销)= 交互面少 = 少 bug。别人的程序丝滑 = 交互模型简单 + 海量 QA——不可比,但教训相同:**少状态 = 少 bug**。本轮的方向 = 以简化对抗复杂度(移除填实功能、纯橡皮、增量轮廓、抑制式消除残影),每一刀都删状态。后续修复继续这个原则:优先删功能/删状态,其次才加逻辑。

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
