# Desktop Menu Bar 计划

状态：已实现

## 1. 目标

为 ChunkMap Studio Desktop 增加稳定的应用级 menu bar，把项目生命周期操作从 Toolbar
迁移到用户熟悉的位置，同时保留地图编辑器自己的高频查看工具。

这次调整解决三个问题：

- `Open Project`、`New Project`、`Reload Project` 不应该长期占用 Toolbar；
- Grid、Coordinates、Seams 和 panel 显示状态需要一个统一入口；
- 后续 `Export Full Map...`、项目验证等全局功能需要可扩展但不过度拥挤的命令层级。

v1 使用 Dear ImGui `BeginMainMenuBar()`，在 macOS 上显示于应用窗口顶部。它不是 AppKit
提供的系统全局菜单；原生 macOS menu integration 不属于本阶段。

## 2. 信息架构

第一版只增加三个有真实职责的菜单，不为了“看起来完整”加入空的 Help/Edit 菜单。

### 2.1 File

```text
File
  New Project...              Cmd/Ctrl+N
  Open Project...             Cmd/Ctrl+O
  ─────────────────────────
  Reload Project              Cmd/Ctrl+R
  ─────────────────────────
  Export Full Map...          Cmd/Ctrl+Shift+E
  ─────────────────────────
  Quit                        Cmd/Ctrl+Q
```

规则：

- `New Project...` 和 `Open Project...` 始终可用；
- `Reload Project`、`Export Full Map...` 只在项目已打开时可用；
- Export 在项目打开且没有其他整图导出进行时可用；
- New/Open/Reload/Quit 之前复用现有 dirty Prompt flush 行为；
- 不增加 `Save`：当前正式 mutation 已即时进入 Desktop command queue，没有独立 Save 语义；
- v1 不做 `Open Recent`，因为项目列表和最近记录尚无正式模型。

### 2.2 Project

```text
Project
  Project Settings...
```

只在项目打开时启用。第一版不把尚未存在的 Project Validate、Reveal Project Folder 等功能
塞进菜单。后续只有在对应用户流程真正实现时才添加。

### 2.3 View

```text
View
  Fit Map                     Home
  Focus Selected              F
  ─────────────────────────
  [x] Grid
  [x] Coordinates
  [x] Seams
  ─────────────────────────
  Panels
    [x] Map Controls
    [x] Inspector
    [x] Log
  Reset Layout
```

规则：

- `Fit Map` 需要已打开项目；
- `Focus Selected` 还需要当前 selected chunk；
- Grid、Coordinates、Seams 直接绑定现有 bool，不创建第二份 UI state；
- panel checkbox 控制对应 ImGui window 的 `p_open`；
- `Map` 是核心 document viewport，v1 不允许从 Panels 中关闭；
- `Reset Layout` 清理当前 dock layout，并在下一帧应用默认布局；
- `Home` 和 `F` 保留现有 canvas-hover 快捷键，不在编辑 Prompt 时全局截获字符键。

## 3. Toolbar 重构

当前 Toolbar：

```text
Open Project | New Project | Project Settings | Reload |
Fit Map | Grid | Coordinates | Seams | project status
```

调整后改名为 `Map Controls`，只保留地图查看动作和当前文档状态：

```text
Fit Map | Grid | Coordinates | Seams     zelda-overview · 4×4 · 16/16 Ready
```

迁移原则：

- 从 Toolbar 删除 Open、New、Project Settings、Reload；
- 不复制 File/Project 菜单中的低频操作；
- Grid、Coordinates、Seams 同时出现在 Toolbar 和 View menu，因为它们是高频即时查看开关；
- 无项目时隐藏/禁用 Map Controls 内容；
- Canvas 的空状态继续显示大尺寸 `Open Project` 和 `New Project` 按钮，这是首次启动引导，
  不属于重复的全局 Toolbar 命令。

## 4. 布局方向

ChunkMap Studio 是面向地图制作过程的工具，menu bar 保持安静、标准，视觉识别放在有实际
信息价值的项目状态串中，而不是装饰 menu item。

```text
┌ File   Project   View ─────────────────────────────────────────────┐
├ Fit Map  ☑ Grid  ☑ Coordinates  ☑ Seams     zelda · 4×4 · 16/16 ┤
├───────────────────────────────────────────┬───────────────────────┤
│                                           │ Inspector             │
│                   Map                     │                       │
│                                           │                       │
├───────────────────────────────────────────┴───────────────────────┤
│ Log                                                               │
└───────────────────────────────────────────────────────────────────┘
```

现有暗色编辑器 palette、蓝色 selection、绿色成功和红色错误保持不变。本阶段不修改字体、
图标系统或整体主题，避免 menu bar 改造扩大为无关的视觉重做。

## 5. 快捷键

使用 ImGui shortcut API 统一处理，不在每个 menu item 后单独轮询按键。

平台 modifier（ImGui GLFW backend 会在 macOS 将 `ImGuiMod_Ctrl` 映射为 Cmd）：

```text
macOS          ImGuiMod_Ctrl -> Cmd
Windows/Linux  ImGuiMod_Ctrl -> Ctrl
```

必须满足：

- menu item 显示的 shortcut 与实际处理一致；
- modal 打开或文本编辑器需要键盘输入时，不误触无 modifier 的全局命令；
- disabled item 的 shortcut 同样不执行；
- shortcut 只调用现有 action method，例如 `open_project_dialog()`，不复制业务逻辑。

`Cmd+Q` 的实现通过 App 暴露 `exit_requested()`，由 `main.cpp` 设置 GLFW window close flag；
不在 App 内直接保存 GLFW window pointer。

## 6. 状态与行为

新增 UI state：

```cpp
bool show_map_controls_ = true;
bool show_inspector_ = true;
bool show_log_ = true;
bool exit_requested_ = false;
```

`Reset Layout` 直接恢复三个 panel visibility bool，并把 `layout_initialized_` 置为 false，
让同一帧后续的 dockspace 绘制重建默认布局，不需要额外的持久 state。

不增加 command state 或项目持久化字段。Menu 与 panel visibility 属于 Desktop session UI，
不是 map project 内容。

动作路由：

```text
Menu item / Shortcut
  -> existing App action
  -> DocumentCommandQueue when project command is required
  -> normal completion/log flow
```

因此从 menu 发起的 New/Open/Reload/Settings 操作继续显示在 Log panel，CLI 与 Desktop 的
统一 command 路径不变。

## 7. 代码改动范围

### `desktop/src/app.*`

- 新增 `draw_main_menu_bar()`；
- 将 menu bar 放在 `draw_dockspace()` 之前绘制，使 viewport work area 正确避开菜单高度；
- 将 `draw_toolbar()` 重命名为 `draw_map_controls()`；
- 增加 panel visibility、Reset Layout 和 exit request state；
- 复用现有 New/Open/Reload/Settings/Fit/Focus action；
- menu 和 shortcut action 统一调用同一函数。

### `desktop/src/main.cpp`

- 每帧检查 `app.exit_requested()`；
- 设置 GLFW close flag；
- 不在这里实现 menu action。

### 文档和测试

- README 更新 Desktop 命令入口；
- Desktop smoke test 确认 menu bar 构建和退出路径；
- architecture source test 确认 menu action 没有绕过 command queue。

## 8. 测试清单

### File

- 无项目时 New/Open 可用，Reload 不可用；
- Open 继续使用 macOS 无 JSON filter 的文件对话框；
- Open/New/Reload 后 project snapshot、Log 和 panel 状态正确；
- Cmd/Ctrl+N、O、R 与 menu item 行为一致；
- Quit 会先 flush dirty Prompt/Global Prompt，再退出。

### Project / View

- Project Settings 无项目时 disabled；
- Fit Map 与 Focus Selected 的 enable condition 正确；
- View menu 和 Map Controls checkbox 双向同步；
- Inspector、Log、Map Controls 可以隐藏并重新打开；
- Reset Layout 恢复默认 Map/Inspector/Log/Map Controls 布局；
- Prompt editor 聚焦时输入不会触发 View 快捷键。

### 回归

- CLI `project open` 仍能让 Desktop 切换当前项目；
- 后台 PNG decoder pool 和 Log panel 正常工作；
- `cmake --build build -j 8` 成功；
- `ctest --test-dir build --output-on-failure` 全部通过。

## 9. 实施顺序

1. 增加 main menu bar 和 File/Project/View action wiring；
2. 增加跨平台 modifier shortcut；
3. 将 Toolbar 收敛并重命名为 Map Controls；
4. 增加 panel visibility 与 Reset Layout；
5. 增加 Quit request plumbing；
6. 更新 README 和测试；
7. 在 macOS 实际检查 menu、modal、dock 和快捷键；
8. 完整 build + ctest。

## 10. 明确不做

- 不实现 macOS AppKit 系统全局菜单；
- 不增加假的 Save/Save As；
- 不实现 Open Recent；
- 不增加空 Help/Edit 菜单；
- 不改变项目 schema 或保存目录；
- 不重做主题、字体或图标；
- 不在本阶段实现 Full Map Export 本身。
