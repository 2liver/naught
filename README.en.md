# 無 (naught)

*Write in vain.*

> **naught** /nɔːt/ — one syllable, Old English for *nothing*, *zero*, *無*.
> To **come to naught** is to dissolve into nothing — 化为乌有.
> This is a writing app that believes writing should leave no trace:
> open it, write, close it. Nothing is saved. Nothing persists. When the
> app dies, a keystroke brings it back — *nothing becomes something*.

*中文版: [README.md](README.md).*

## Philosophy

- **Radical minimalism**: the page is everything. No toolbars, no settings
  windows, nothing extra — the interface of 無 is just paper.
- **Zen aesthetics**: writing and vanishing are one breath. Nothing is
  adjustable; every texture comes from the screen itself — scanlines,
  phosphor glow, afterglow, rising and falling like breathing. Open,
  write, close: it is gone.
- **The death drive**: everything written here walks toward its own
  disappearance. Freud called this impulse toward dissolution the death
  drive (*Todestrieb*) — naught gives it a key: **死** (⌃⌘N). Write,
  die, leave no corpse; press **生** (⌃⇧⌘N) and the blank page returns —
  nothing becomes something.
- **Icarus**: the writer is Icarus, flying toward the sun of meaning until
  the wax melts and the sea takes him. naught offers no archive to soften
  the fall — it believes only writing without a safety net flies high
  enough. Falling is the ending; nothing remains.
- **ADHD-friendly**: a writing environment built for the easily distracted
  mind — a single task (no tabs, no notifications, no "do you want to
  save" dialogs), instant visual feedback (phosphor bloom, block cursor,
  typewriter pacing) that anchors attention in the present. Writing badly
  costs nothing — it will all vanish anyway, so there is no
  perfectionism trap.

## What it is

**naught (無)** is a desktop writing instrument: it opens onto a blank
page, and everything written on it dissolves when the window closes.
Drafts, flashes, working-through, manifestos — all the writing that
exists in order to disappear lives here.

Press ⌘T and it becomes **a cabinet of 1975–1982 cathode-ray tubes**:
the screen turns into a machine from that era. Type facing the amber
screen of a 1981 Osborne; when you write science fiction, the display
itself is the time machine.

| Machine | Original | Era | Phosphor | Factory font |
|---------|----------|-----|----------|--------------|
| Amber | Osborne Executive-class portable (the portable-writing ancestor) | 1981 | P3 amber | Fusion Pixel (CJK pixel font) |
| Green | IBM 5100 (the first portable computer) | 1975 | P1 green | VT323 |
| C64 | Commodore 64 on a color monitor (16-color PETSCII) | 1982 | P22 triad | Press Start 2P |
| White | IBM PC 5150 (CGA white) | 1981 | P4 white | Fixedsys Excelsior |

What is simulated is the **screen phenomenon, not the machine**: only the
texture of an eye looking through glass at a tube. No sound, no flicker,
no "emulator" parts.

## What makes it special

**The CRT simulation** (recipes and models in
[docs/crt-filter.md](docs/crt-filter.md)):

- Phosphor persistence: double-exponential decay, calibrated per machine
  against measured P1/P3/P4/P22 curves; scrolling leaves a ghost that
  fades in place, freshly typed characters excite brighter, then settle
- Beam-spot physics: brighter means wider — bright glyphs saturate at the
  core and soften around it
- 2px soft scanlines, rolling scan excitation, true gaussian bloom,
  warm-up (dark → bright → overshoot → settle), the rolling refresh band,
  noise and dust, glass vignette with a diagonal reflection
- Graticule diffraction: red/blue fringes on bright vertical edges; the
  C64 gets a real dot-triad shadow mask (RGB phosphor grid, 3px period,
  per-channel phase) — the color structure of a color tube, absent on the
  monochrome machines
- A block inverse cursor: a whole-cell blazing phosphor block, blinking in
  step with the I-beam
- The follow view: locked = the "perfect angle" of a mouse that has left
  the window; unlocked = the observer follows the mouse (parallax + slow
  scan roll)
- Everything runs on the GPU (QRhi · Metal offscreen render + readback);
  typing latency outranks every effect

**On the writing side**:

- **Typewriter-speed printing**: ASCII art types itself out line by line
  (a page in ~1.6s), never a flash
- **Ink and text live on separate layers**: the brush and eraser touch
  only strokes; strokes recolor with the phosphor and the dark/light
  theme, and undo/redo together with text (100 steps); hold Shift while
  dragging = hold the brush
- **Declare as art** (⇧⌘A): turn the selection into an ASCII-art source
  image — rescalable, re-renderable; hidden feature: drop an image onto
  the window and the whole page becomes ASCII art
- **Code mode** (⌘B): monospace + line numbers + syntax highlighting
  (KF6, optional) — pure draft, no run, no save
- **言 / 隔** (⌘L / ⌘F): batch proofreading partners — wrap every line in
  「」 / space every line apart
- **Text processing**: four box styles, join/split lines, path↔tree,
  centering — on the selection or the current line/document, one undo step
- Character zoom and brush zoom are independent, with hold-to-accelerate;
  anchored zoom keeps the point under the cursor fixed
- Three-tier font model: factory default (undeletable) / classic stock
  (6 faces, deletable) / session default; drop your own fonts in the
  font folder

## Quick start

Open it, write. Right-click (Windows/Linux) or the macOS menu bar 「项」
is the entry to every function.

```text
⌘T      CRT screen (amber)   ⇧⌘M   switch machine (amber→green→C64→white)
⌘B      code mode            Esc    leave brush/eraser/code mode
⌘N      clear text (undoable) ⌃⌘N   die          ⌃⇧⌘N  live again
```

## Shortcuts (the complete 「项」 menu)

> On macOS ⌘ = Command; on Windows/Linux use Ctrl (die/live are
> Ctrl+Win+N and Ctrl+Shift+Win+N). A native menu bar exists only on
> macOS; on Windows/Linux the entry point is the right-click menu, with
> the same items.

### View

| Function | Shortcut |
|----------|----------|
| Code mode (編) | ⌘B |
| CRT screen (顯) | ⌘T |
| Switch machine (4-machine cycle) | ⇧⌘M |
| Follow-view lock | ⇧⌘T |
| Machine-native grid (80 columns) | ⌘0 inside the CRT session |
| Declare as art (selection → ASCII source) | ⇧⌘A |
| Drop image → ASCII art | drag & drop (hidden feature) |
| Leave mode (brush/eraser/code) | Esc |

### Text

| Function | Shortcut |
|----------|----------|
| 言 (wrap every line in 「」) | ⌘L |
| 隔 (blank line around every line) | ⌘F |
| Single / double / round / bold box | ⇧⌘G / ⇧⌘H / ⇧⌘U / ⇧⌘V |
| Join lines / split back | ⇧⌘J / ⇧⌘K |
| Paths → tree / tree → paths | ⇧⌘P / ⇧⌘R |
| Center | ⇧⌘C |

### Tools

| Function | Shortcut |
|----------|----------|
| 摹 (select all & copy) | ⌘S |
| 空 (clear text, undoable) | ⌘N |
| 陰 (black page) / 陽 (white page) | ⌘I / ⌘O |
| 塗 (brush) / 擦 (eraser) | ⌘D / ⌘E |
| 消 (clear all ink) | ⇧⌘N |
| Hold the brush | hold Shift while dragging in brush/eraser |

### Zoom

| Function | Shortcut |
|----------|----------|
| Font bigger / smaller / reset | ⌘= / ⌘- / ⌘0 (hold to accelerate) |
| Brush thicker / thinner / reset | ⇧⌘= / ⇧⌘- / ⇧⌘0 |
| Free zoom | ⌘+scroll / trackpad pinch |
| Horizontal scroll | ⇧+scroll / trackpad pan |

### Font & editing

| Function | Shortcut |
|----------|----------|
| Previous / next font | ⇧⌘, / ⇧⌘. |
| Restore default font | menu |
| Undo / redo | ⌘Z / ⌘Y |
| Soft tab (4 spaces) | Tab |

### Death & life

| Function | Shortcut |
|----------|----------|
| 死 (die — quit instantly, no save prompt) | ⌃⌘N |
| 生 (live again — global hotkey held by the login-item agent) | ⌃⇧⌘N |

## Install

Prebuilt artifacts live in [Releases](../../releases). The macOS bundle is
not notarized: right-click → Open on first launch; on Windows choose
"More info → Run anyway" in SmartScreen.

**macOS, one command** (installs to `~/Applications`, appears in
Launchpad):

    curl -fsSL https://raw.githubusercontent.com/2liver/naught/main/install/one.sh | sh

Custom directory: `… | sh -s -- <dir>` (only the default is indexed by
Launchpad). If GitHub is unreachable, set a proxy first, e.g.
`export https_proxy=http://127.0.0.1:7897`.

**Windows** (PowerShell one-liner; forces TLS 1.2, falls back through
mirrors when the direct connection fails):

    [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; $d='D:\naught'; md $d -Force|Out-Null; $base='https://github.com/2liver/naught/releases/download/v0.3.14/naught-windows.zip'; foreach($m in @('','https://gh-proxy.com/','https://ghproxy.net/','https://gh.ddlc.top/')){try{iwr ($m+$base) -OutFile "$d\n.zip" -ErrorAction Stop; break}catch{}}; Expand-Archive "$d\n.zip" -DestinationPath $d -Force

**Linux** (single-file AppImage):

    curl -fsSL https://github.com/2liver/naught/releases/latest/download/naught-linux.AppImage -o ~/.local/bin/naught && chmod +x ~/.local/bin/naught

## Build from source

Requires Qt 6.9+ (with ShaderTools) and CMake. Syntax highlighting is an
optional dependency (KF6 SyntaxHighlighting; without it code mode falls
back to monospace + line numbers):

    cmake -S . -B build
    cmake --build build

If Qt is not on the default path, add
`-DCMAKE_PREFIX_PATH=<Qt dir>/<version>/<compiler>`. To install into
Launchpad on macOS: `./install.sh`.

## Self-test & performance

- `--selftest`: a geometry-and-behavior regression gate, mandatory green
  on all three platforms in CI.
- `--bench`: local benchmarks (offscreen software rasterization; real
  machines composite on the GPU and score better): typing in a 2000-line
  document ≈ 0.5 ms/keystroke, zoom ≈ 0.2 ms, scrolling ≈ 3 ms/step;
  typing under the CRT ≈ 0.2 ms/keystroke. Anchored zoom costs
  proportionally to the blocks above the anchor. Known boundary: a single
  100k-character line (giant paste) ≈ 100 ms/keystroke — an inherent
  QPlainTextEdit block-relayout cost; normal wrapped writing is
  unaffected.

## Icon

The 「無」 icon is rendered from Source Han Serif Heavy (black on white,
SIL OFL 1.1, see `resources/icon/OFL.txt`). To regenerate:

    cmake --build build --target naught_icon_tool
    ./build/naught_icon_tool <SourceHanSerifSC-Heavy.otf> build/icons

Then copy the outputs back into `resources/` and `resources/icons/`.

## Design documents

- [docs/crt-filter.md](docs/crt-filter.md) — CRT recipes and physical models
- [docs/fidelity-audit.md](docs/fidelity-audit.md) — the per-machine fidelity ledger
- [docs/roadmap.md](docs/roadmap.md) — milestones and the working runbook

## License

MIT © 2026 2liver (see `LICENSE`). Bundled fonts carry their own OFL and
other licenses; texts live under `resources/fonts/` and
`resources/icon/OFL.txt`.
