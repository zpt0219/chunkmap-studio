# AI Chunk Map Studio: Desktop 与 CLI 用户体验设计

状态：历史 UX 基线；持久化、渲染与 session 章节已被 schema v2 架构取代

> 当前实现以 [CODE_ARCHITECTURE_DESIGN.md](./CODE_ARCHITECTURE_DESIGN.md) 和
> [IN_MEMORY_DOCUMENT_RENDERING_PLAN.md](./IN_MEMORY_DOCUMENT_RENDERING_PLAN.md) 为准。
> 本文仍保留早期交互决策，但其中的 Composite、cache、registration、旧目录路径和
> render/map export 命令不再有效。

实现状态：`0.6.0-phase6` 已使用共享 Command Queue / IPC 和 ChangeSet 刷新，Desktop
必须运行，CLI 不再直接读写项目。

本文定义第一版 Dear ImGui Desktop App 和配套 CLI 的用户体验。第一版刻意保持简单：Desktop App 只负责项目设置、看图、检查接缝和编辑文字 prompt；所有 AI 文字分析和图片生成都由用户在 Codex 中发起，Codex 通过 CLI 读取 context 并写回结果。

Phase 6 后，Desktop 与 CLI 共享同一个 DocumentCommandQueue 和 CommandDispatcher。详细
技术设计见
[PHASE6_COMMAND_DISPATCHER_DESIGN.md](./PHASE6_COMMAND_DISPATCHER_DESIGN.md)。

## 1. 第一版产品边界

### 1.1 Desktop App 负责

- 创建和打开地图项目。
- 上传全局 Concept Map。
- 设置地图有多少 Columns 和 Rows。
- 把用户提供的细节图导入任意一个或多个 chunk。
- 从第一张导入图片读取整个项目的 chunk 尺寸。
- 在可缩放、可拖拽的大画布中查看完整地图。
- 点击任意 chunk，查看其坐标、正式图片和文字 prompt。
- 在多行文本编辑器中直接编辑每个 chunk 的文字 prompt，并自动写入项目。
- 查看 chunk 与邻居的拼接效果和接缝。
- 在 Desktop 管理项目时接收 CLI command completion，立即显示 Codex 写回结果。

Desktop App 不负责：

- 不调用 AI。
- 不创建或运行生图任务。
- 不提供 Generate 按钮。
- 不提供生成队列。
- 不显示 AI 生成百分比或任务进度。
- 不保存临时生成结果或历史图片。
- 不提供图片审批流程。

Desktop 可以导出外部生图所需的 context，并接收用户在 Stable Diffusion、ComfyUI
或其他工具中生成的结果。这是文件交接，不是 App 内置 AI 生成。

### 1.2 CLI 负责

- 读取和修改与 Desktop 相同的项目数据。
- 为 Codex 导出 Concept Map 分析 context。
- 接收 Codex 写回的所有 chunk prompt。
- 为指定 chunk 制作透明 overlap template。
- 为 Codex 导出该 chunk 的图片和文字 context。
- 校验 Codex 生成的图片格式和尺寸。
- 把生成结果原子写入正式 chunk。
- 重新拼接 Composite Map。
- 输出机器可读 JSON，方便 Codex 稳定调用。
- 通过本地 IPC 把 typed command 提交给正在运行的 Desktop。

### 1.3 Codex 负责

- 根据用户指令主动运行 CLI。
- 读取 CLI 导出的 Concept Map、region 和 manifest。
- 为所有 chunk 编写初始文字 prompt。
- 读取目标 chunk 的透明模板和文字 prompt。
- 使用 subscription 中的图像生成能力生图。
- 调用 CLI 把文字或图片写回项目。
- 在 Codex 对话中向用户报告生成过程和错误。

## 2. 核心原则

### 2.1 App 是查看器和 Prompt 编辑器

用户在 App 中规划和查看地图，在 Codex 中要求 AI 工作。App 不尝试把 Codex 会话嵌入自己的任务系统。

### 2.2 图片只有两种状态

| 状态 | 含义 |
|---|---|
| Empty | 该坐标没有正式细节图 |
| Ready | 该坐标存在一张正式细节图 |

不存在额外的审批、排队、生成中、过期或历史版本状态。

Codex 通过 CLI 成功写回图片后，该图片立即是该坐标的正式图，状态直接变为 Ready。

### 2.3 第一版不保留图片版本历史

每个 chunk 只保存一张正式图片。

如果用户要求 Codex 重新生成：

1. Codex重新读取当前 context。
2. Codex生成新图片。
3. CLI校验新图片。
4. CLI原子替换该 chunk 的正式图片。
5. 旧图片被删除，不出现在 App 中，也不进入版本历史。

文件写入使用临时文件加原子替换，避免写入中断导致正式图损坏。这只是写入安全措施，不是版本功能。

### 2.4 写回结果默认就是正式图

Codex 通过 CLI 写回的图片立即成为该 chunk 的正式图，无需用户再次确认。

用户可以在 App 中随时查看正式图和接缝。如果想调整结果：

- 在 App 的文本编辑器中直接修改 prompt。
- 回到 Codex，要求重新生成指定坐标。
- Codex再次通过 CLI 生成并覆盖正式图。

### 2.5 Concept Map 只用于写文字

Concept Map 和 region crop 只允许用于 AI 理解世界布局并编写 prompt。

生成细节 chunk 时，图片输入只能使用从正式邻居制作的透明 overlap template。不能把 Concept Map 或 region crop 作为生图参考图，避免复制低细节概念图。

### 2.6 正式修改只经过 DocumentCommandQueue

Desktop 操作和 CLI 操作都先转换为 typed command，再进入同一项目的串行 queue。Panel、
CLI handler 和 IPC handler 都不能直接写 `project.json`、Prompt 或正式图片。

这个 queue 是内部文档一致性机制，不是用户可见的生成队列：

- 不保存到磁盘。
- 不显示 AI 进度。
- 不创建生成中状态。
- 不保存 command history。
- 不提供 undo/redo。

CLI command 始终提交给正在运行的 Desktop。Desktop 未运行时 CLI 返回
`desktop_not_running` 并且不修改项目。用户使用的其他 CLI 命令和参数不变化。

`project init` 也进入 Desktop queue，成功后 App 直接打开新项目，与用户在 New Project
modal 中创建的行为相同。

## 3. 坐标与尺寸

### 3.1 坐标系

左上角为 `(0,0)`：

```text
x 向右增加
y 向下增加

(0,0)  (1,0)  (2,0)
(0,1)  (1,1)  (2,1)
(0,2)  (1,2)  (2,2)
```

### 3.2 Grid 尺寸

用户上传 Concept Map 后，只设置：

- Columns
- Rows

例如 3x3 表示 3 Columns 和 3 Rows。

### 3.3 Chunk 像素尺寸

App 不预定义 Chunk width 和 Chunk height。

用户导入第一张 chunk 图片时：

```text
chunk_width = imported_image_width
chunk_height = imported_image_height
```

例如第一张图片是 `1433x1098`，项目中所有 chunk 都必须是 `1433x1098`。

第一张导入图片：

- 不裁切。
- 不缩放。
- 不补边。
- 原始尺寸直接成为项目尺寸。

之后可以在任意坐标继续导入图片，彼此不要求相邻。尺寸完全一致时直接使用，相差 1px 时执行确定性补边，更大差异直接拒绝。导入图和 AI 生成图写入后都是普通 Ready 正式图，不保存来源身份。

### 3.4 Overlap

项目保存 overlap ratio，而不是预定义 overlap pixels。

默认值：

```text
horizontal_overlap_ratio = 0.15
vertical_overlap_ratio = 0.15
feather_ratio = 0.03
```

第一张图片导入后计算：

```text
horizontal_overlap_px = round(chunk_width * horizontal_overlap_ratio)
vertical_overlap_px = round(chunk_height * vertical_overlap_ratio)
horizontal_step = chunk_width - horizontal_overlap_px
vertical_step = chunk_height - vertical_overlap_px
```

尚未导入任何图片时，App 不显示虚假的最终地图像素尺寸，只显示 `Waiting for imported image`。

## 4. 首次使用流程

### 4.1 创建项目

用户在 App 中：

1. 点击 New Project。
2. 输入项目名称和保存目录。
3. 上传 Concept Map。
4. 设置 Columns 和 Rows。
5. 确认网格切片。

App 保存：

- Concept Map 原图。
- 网格配置。
- 每个坐标对应的 region crop。
- 初始为空的 prompt 文件。

### 4.2 让 Codex 编写全部 Prompt

Concept Map 切片后，用户在 Codex 中说，例如：

```text
请分析当前地图项目，为所有 chunk 编写描述。
```

Codex 的工作流程：

1. 运行 CLI 导出 concept context。
2. 读取全局 Concept Map、网格和所有 region crop。
3. 为每个坐标编写结构化文字 prompt。
4. 调用 CLI 一次性写回全部 prompt。

CLI command 进入 Desktop queue；command completion 直接刷新 Prompt Inspector。App 未
运行时命令返回 `desktop_not_running`，不会产生无人查看的第二条写入通路。

用户点击任何 chunk 都能看到它的描述，即使该 chunk 还没有正式图片，也没有正式邻居。

### 4.3 用户编辑 Prompt

用户可以在 App 中逐块检查规划：

- 选择 `(0,0)`，查看西北区域描述。
- 直接在 Prompt 文本编辑器中修改地形、地标、道路或禁止内容。
- 停止输入一小段时间后自动写入项目；切换 chunk、编辑器失焦或关闭项目时也立即写入。
- Codex 下一次导出生成 context 时读取最新 prompt。

App 不区分 AI Prompt 和 User Prompt 两层。第一版每个 chunk 只有一份当前 prompt，用户修改后直接成为新的当前内容。

### 4.4 导入 Chunk 图片

用户在 App 中：

1. 点击任意 chunk。
2. 点击 Import Image。
3. 选择一张细节图。
4. App 显示图片原始尺寸。
5. 用户确认使用该尺寸作为全项目 chunk 尺寸。

图片写入后：

- 该坐标状态变为 Ready。
- App 计算 overlap pixels 和完整地图尺寸。
- App 显示该正式图。
- CLI 可以为它的四邻域导出生成 context。
- 用户还可以在任意其他坐标导入正式图，作为额外的风格和内容锚点。

## 5. Desktop App 主界面

### 5.1 中央大地图

打开项目后直接显示地图画布，不显示 Dashboard 或欢迎页。

画布支持：

- 鼠标滚轮围绕光标缩放。
- 右键拖拽平移。
- 单击选择 chunk。
- 双击聚焦 chunk。
- `F` 聚焦当前 chunk。
- `Home` 适配完整地图。
- 显示或隐藏网格。
- 显示或隐藏坐标。
- 显示或隐藏接缝线。

Ready chunk 显示正式图。

Empty chunk 显示低对比度 region crop 或棋盘格，并明确标记为空，不能伪装成正式细节图。

### 5.2 顶部工具栏

第一版只保留查看相关操作：

- Open Project
- Project Settings
- Toggle Grid
- Toggle Coordinates
- Toggle Seams
- Fit Map
- Reload

没有：

- Generate
- Queue
- Pause
- Stop Generation
- 图片确认按钮
- Version History

### 5.3 右侧浮动 Inspector

Inspector 浮在画布右侧，可以调整宽度或折叠。

第一版包含三个标签页：

- Chunk
- Prompt
- Seam

不包含 Job 和 Versions 标签页。

### 5.4 Chunk 标签页

显示：

- 坐标。
- 图片状态：Empty 或 Ready。
- 图片尺寸。
- 上、下、左、右邻居是否 Ready。
- 当前正式图缩略图。
- 原始图片文件路径。

Empty chunk 可以显示一条只读提示：

```text
Ready neighbors: right, bottom
Codex can generate this chunk from 2 neighbors.
```

这只是 context 可用性提示，不是 App 内的生成资格状态，也不提供 Generate 按钮。

操作：

- `Visible on Map`：仅控制当前 Desktop 会话中的 Ready 图片图层。关闭时地图用该坐标的
  Concept region 覆盖该 chunk 的完整 footprint（包括 overlap）；不改变 Ready、正式图片、
  Context、Seam 或整图导出。
- Import Image 或 Replace Image，Empty/Ready chunk 均可用。
- Export Context，有 Ready 正交邻居时可用。
- Reveal Image File，仅 Ready chunk 可用。
- Copy Coordinate。

Visibility 默认开启，只存在于 `App` 内存；Open 或 Reload 项目后全部恢复可见，不写入
`project.json` 或其他项目文件。隐藏的 chunk 仍可选择，Inspector 仍显示其正式图片。

`Export Context` 直接向 Desktop 的 DocumentCommandQueue 提交 `ChunkContext`，导出后在
文件管理器中显示 `manifest.json`。用户在外部工具生成后，仍使用统一的 Import Image
或 Replace Image 写回。App 不要求用户声明图片来源；所有导入均执行相同的规范化、配准、
Composite 与 Seam 重建。

外部生图 context 包含：

- `template.png`：邻居共享边为不透明像素，其余区域透明。
- `mask.png`：白色区域允许生成，黑色邻居边必须保护。
- `prompt.txt`：App 文本编辑器中的最新 prompt。
- `manifest.json`：坐标、尺寸、overlap、mask 约定和邻居路径。

### 5.5 Prompt 标签页

标签页包含两个始终可编辑、明确标注的多行文本编辑器：上方 Global Prompt 控制整个
项目的共享视觉风格，下方 Local Chunk Prompt 控制当前坐标的内容与布局。生成 context
会组合两者，但 App 不保存组合后的第三份 Prompt。

编辑器支持常规文本操作：

- 按编辑器宽度进行软换行，不修改或插入 Prompt 原文中的换行符。
- 光标移动和文本选择。
- `Ctrl/Cmd+C`、`Ctrl/Cmd+X`、`Ctrl/Cmd+V`。
- `Ctrl/Cmd+Z` 和 `Ctrl/Cmd+Shift+Z`。
- 多行输入和滚动。

界面没有 Edit、Save、Revert 或 Copy Prompt 按钮。用户点击文本即可编辑，复制使用文本编辑器的标准选择和快捷键。

修改采用 60 秒 debounce，连续输入期间不会逐字符写盘；任一编辑器失焦、切换
chunk/项目或关闭 App 时会强制写入尚未保存的内容。标签页底部显示 dirty 或 autosaved 状态。

没有 prompt revision、Generated Brief、User Notes 或历史版本。

### 5.6 Seam 标签页

只有 Ready chunk 且存在 Ready 邻居时可用。

显示模式：

- Composite：显示羽化后的正式拼接结果。
- Raw Boundary：显示两张正式原图的接边。
- Difference：显示 overlap 像素差异。
- Overlap Only：只查看共享区域。

显示：

- 邻居方向。
- overlap pixels。
- feather pixels。
- Mean absolute RGB difference。

这些指标只帮助用户观察，不改变图片状态。

## 6. 图片状态与 App 刷新

### 6.1 Empty

该坐标没有正式图片文件。

App 可以显示：

- Concept region 占位。
- 当前 prompt。
- Ready 邻居数量。
- Codex 是否能够从至少一个邻居导出 context。

### 6.2 Ready

该坐标存在一张通过 CLI 校验后写入的正式图片。

App 可以显示：

- 正式图。
- 与 Ready 邻居拼接后的 Composite。
- 接缝指标。

### 6.3 Command completion 刷新

App 不轮询项目文件。Desktop 是单实例 document host，CLI 把所有命令通过 IPC 提交给
Desktop 内置的 DocumentCommandQueue。

Codex 调用 CLI 写回图片后：

1. CLI 连接 Desktop IPC endpoint 并发送 typed command。
2. Desktop queue 串行执行 `CommandDispatcher -> ProjectService`。
3. Dispatcher 原子替换正式图片并重建 Composite 与相关 Seam cache。
4. Command completion 返回精确的 changed chunk、composite 和 seam。
5. App 只刷新这些 Project state 和 GPU textures。
6. CLI 收到稳定的 JSON result 后退出。
7. 用户无需重启 App，立即看到生成结果。

Desktop 未运行时，CLI 返回 `desktop_not_running`，不创建本地 queue、Dispatcher 或
ProjectService，也不修改项目文件。

App 保留手动 Reload，用于 Git 同步或调试期间的直接文件修改；正式编辑不依赖文件监听。

App 不需要知道 Codex 的生图百分比，也不维护 Codex 当前是否正在工作。

## 7. Codex 生成 Chunk 的完整流程

### 7.1 用户发出指令

用户在 Codex 中说：

```text
请生成 (1,2) 这个 chunk。
```

或者：

```text
我修改了 (1,2) 的 prompt，请重新生成。
```

### 7.2 Codex 导出 Context

Codex 先运行：

```bash
chunkmap --project my-world chunk context 1,2 --json
```

CLI 负责：

- 检查坐标合法。
- 检查项目已经有 chunk size。
- 检查至少存在一个 Ready 四邻居。
- 读取该坐标的最新 prompt。
- 读取上、下、左、右方向中所有 Ready 邻居的正式图。
- 根据实际存在的 1 到 4 个 Ready 邻居制作透明 overlap template。
- 导出自包含 context。

每个 Ready 邻居贡献目标 chunk 对应方向的共享边：

- 左邻居的右侧 overlap 放到模板左边。
- 右邻居的左侧 overlap 放到模板右边。
- 上邻居的底部 overlap 放到模板上边。
- 下邻居的顶部 overlap 放到模板下边。

模板必须支持单边、双边、三边和四边。多个共享边在角落相交时固定按上、下、左、右顺序粘贴，后粘贴的边覆盖角落，保证结果确定即可，不增加冲突检查。例如 3x3 地图先完成外围 8 个 chunk，最后生成中心 chunk 时，CLI 应生成同时包含上、下、左、右四条参考边的透明模板。

CLI 返回：

```json
{
  "chunk": [1, 2],
  "expected_size": [1433, 1098],
  "template": "/repo/output/my-world/context/chunk_1_2/template.png",
  "mask": "/repo/output/my-world/context/chunk_1_2/mask.png",
  "mask_convention": "white_generate_black_protect",
  "prompt": "/repo/output/my-world/context/chunk_1_2/prompt.txt",
  "manifest": "/repo/output/my-world/context/chunk_1_2/manifest.json",
  "write_command": "chunkmap --project my-world chunk write 1,2 --image <generated.png>"
}
```

Context 不需要 job id、claim、queue 或任务状态。

### 7.3 Codex 生图

Codex：

1. 读取 prompt 和 manifest。
2. 查看透明 template。
3. 只把 template 作为图像生成参考图。
4. 使用 subscription 图像生成。
5. 检查输出方向和尺寸。

Concept Map 和 region crop 不出现在该 context 的生成图片输入中。

### 7.4 Codex 写回正式图

Codex 运行：

```bash
chunkmap --project my-world chunk write 1,2 \
  --image /path/to/generated.png
```

`chunk write` 默认就是写入该坐标的正式图。如果该坐标已经是 Ready，CLI 直接原子覆盖旧图，不需要额外参数。用户要求 Codex 生成这个坐标，就已经同意更新该坐标。

CLI 负责：

- 校验图片格式。
- 校验目标尺寸。
- 允许确定且可记录的 1px 边缘修正。
- 原子写入 `chunks/1_2/image.png`。
- 删除被替换的旧正式图片。
- 重建受影响的拼接缓存。
- 返回包含正式图、Composite 和 Seam invalidation 的 `ChangeSet`。

写回成功后，它立即是正式图，不需要额外审批步骤。

### 7.5 用户查看结果并按需重生成

App 自动刷新并显示新图。

用户可以：

- 缩放查看细节。
- 查看完整地图。
- 检查四周接缝。
- 编辑 prompt。
- 回到 Codex 要求重新生成。

## 8. Codex 编写全部 Prompt 的流程

### 8.1 导出 Concept Context

Codex 运行：

```bash
chunkmap --project my-world concept context --json
```

CLI 返回：

```json
{
  "concept_image": "/repo/output/my-world/concept/source.png",
  "grid": {"columns": 3, "rows": 3},
  "regions_dir": "/repo/output/my-world/concept/regions",
  "output_schema": "/repo/output/my-world/context/concept/prompts.schema.json",
  "write_command": "chunkmap --project my-world prompts import --input <prompts.json>"
}
```

### 8.2 AI 分析

Codex 查看：

- 完整 Concept Map。
- 每个 region crop。
- 坐标和相邻关系。
- 项目级全局说明。

Codex 为每个坐标生成一份 prompt。Prompt 应描述：

- 该区域的作用。
- 地形和生态。
- 地标。
- 与四周连接的方向。
- 禁止复制或不应出现的内容。

### 8.3 写回

Codex 运行：

```bash
chunkmap --project my-world prompts import --input /tmp/prompts.json
```

CLI 校验：

- 所有坐标都在 grid 范围内。
- JSON 没有重复坐标。
- 每个 prompt 至少有正文。
- 输入文件中出现的坐标直接覆盖现有 prompt；没有出现在输入文件中的坐标保持不变。

`prompts import` 不区分 prompt 是 AI 写的还是用户编辑的。用户要求 Codex 重新编写并导入这些坐标，就已经同意覆盖相应内容。

Command completion 立即刷新 Prompt Inspector。CLI 不存在绕过 App 的本地写回模式。

## 9. CLI 命令设计

CLI 名称暂定为 `chunkmap`。

运行以下任何命令前，AI Chunk Map Studio Desktop 必须已经启动。CLI 不自动启动 App，
也不能直接修改项目文件。

### 9.1 项目

```bash
chunkmap project init my-world \
  --concept world.png \
  --columns 3 \
  --rows 3 \
  --overlap-ratio 0.15x0.15 \
  --feather-ratio 0.03

chunkmap --project my-world project status
chunkmap --project my-world project status --json
chunkmap --project my-world project validate
```

`project init my-world` 创建顶层目录 `output/my-world/`。其余命令使用全局参数 `--project my-world` 明确选择该项目，CLI 将其解析到 `output/my-world/`。

### 9.2 Chunk 图片导入

```bash
chunkmap --project my-world chunk import 2,2 --image /path/to/detail.png
```

### 9.3 Prompt

```bash
chunkmap --project my-world concept context --json
chunkmap --project my-world prompts import --input prompts.json
chunkmap --project my-world prompt show 1,2
chunkmap --project my-world prompt set 1,2 --file prompt.md
```

### 9.4 Chunk 图片

```bash
chunkmap --project my-world chunk context 1,2 --json
chunkmap --project my-world chunk write 1,2 --image generated.png
chunkmap --project my-world chunk write 1,2 --image regenerated.png
chunkmap --project my-world chunk show 1,2
chunkmap --project my-world chunk remove 1,2
```

`chunk remove` 是危险命令，需要 `--yes`，并直接把该坐标恢复为 Empty。

### 9.5 拼接与检查

```bash
chunkmap --project my-world render
chunkmap --project my-world render --preview
chunkmap --project my-world seam inspect 1,2 --direction right
chunkmap --project my-world map export map.png
```

### 9.6 CLI 输出规则

- 查询命令支持 `--json`。
- 成功返回退出码 0。
- 参数或校验错误返回非 0。
- stdout 输出结果。
- stderr 输出错误和诊断。
- 写操作成功后输出被修改的文件路径。
- CLI 不输出伪造的 AI 进度。
- 本地和 Desktop IPC 路由必须输出相同的 `schema_version: 1` JSON envelope。
- 路由过程不输出到 stdout，保证 JSON 仍然只有一个 document。

## 10. Prompt 数据

第一版每个 chunk 只有一份 prompt 文件，例如：

```text
chunks/1_2/prompt.md
```

建议内容：

```markdown
# Chunk (1,2)

## Role
Western beach transition.

## Terrain
Rocky coast, sparse forest, sand and open ocean.

## Landmarks
One small market or fishing shelter.

## Connections
Continue the road from the right neighbor.
Continue the forest cliffs from the top neighbor.

## Forbidden
Do not duplicate buildings or shoreline landmarks from overlap strips.
```

App 直接用多行文本编辑器编辑这份 Markdown，并自动写回同一个文件。

## 11. 项目目录建议

仓库顶层放代码、文档和构建目录。所有地图项目统一放在顶层 `output/` 下，并以项目名作为子目录。

```text
chunkmap-studio/
  CMakeLists.txt
  src/
  include/
  cli/
  tests/
  docs/
  third_party/
  build/
  output/
    my-world/
      project.json
      concept/
        source.png
        grid_preview.png
        regions/
          0_0.png
          1_0.png
      chunks/
        0_0/
          prompt.md
          image.png
          metadata.json
        1_0/
          prompt.md
          metadata.json
      context/
        concept/
        chunk_1_2/
          template.png
          mask.png
          prompt.txt
          manifest.json
      cache/
        thumbnails/
        seams/
        composite.png
        composite_preview.jpg
    another-world/
      project.json
```

`output/my-world/` 是一个完整、可独立打开的地图项目。其中：

- `project.json`、`concept/`、`chunks/` 是正式项目数据。
- `context/` 是 Codex 单次分析或生图所需的临时输入，可随时重建。
- `cache/` 是 App 查看地图所需的派生文件，可随时重建。

Phase 6 不再创建 `events/`。Desktop 更新来自内存中的 command completion；IPC socket
或 Named Pipe 位于系统用户 runtime 环境，不属于项目数据。

不存在临时生成图片目录、历史图片目录或任务队列目录。Codex 的生成结果只有在 CLI 校验并写入 `chunks/<x>_<y>/image.png` 后才属于正式项目数据。

`context/` 是可重建文件。CLI 每次运行 `chunk context` 时可以覆盖对应坐标的旧 context。

## 12. 错误处理

### 12.1 目标没有 Ready 邻居

`chunkmap chunk context` 直接失败：

```text
Chunk (0,0) has no Ready orthogonal neighbor.
Import an image into an adjacent chunk first.
```

### 12.2 图片尺寸错误

- 方向或比例明显错误：拒绝写回。
- 相差 1px：CLI 可以评估边缘补像素方案，并记录修正。
- 更大尺寸差异：拒绝写回，Codex 应重新生成。
- 不做隐式缩放。

### 12.3 App 正在运行

Desktop 必须运行。CLI 通过固定的当前用户 IPC endpoint 提交 command，不能绕过 Desktop
queue 本地写入。Desktop 完成原子写入后依据 `ChangeSet` 刷新 texture。

CLI 无法连接 endpoint 时返回 `desktop_not_running`。第二个 Desktop instance 发现已有
endpoint 可连接后退出，因此当前用户只有一个 document host 和一条 command queue。

## 13. 第一版 MVP

### 13.1 Desktop 必须完成

- New/Open Project。
- 上传 Concept Map。
- 设置 Columns 和 Rows。
- 显示 Concept Map 网格切片。
- Import Image 到任意坐标，Ready chunk 可直接覆盖。
- 从第一张导入图片确定 chunk size。
- 完整地图缩放和拖拽。
- Empty/Ready chunk 显示。
- 右侧 Chunk、Prompt、Seam Inspector。
- Prompt 多行文本编辑和自动写入。
- CLI/DocumentCommandQueue completion 驱动的精确自动刷新。
- Composite Map 显示。
- 强制 Desktop 单实例。
- 内置唯一 DocumentCommandQueue、CommandDispatcher 和本地 IPC server。

### 13.2 CLI 必须完成

- Project init、status 和 validate。
- Concept context 导出。
- Prompt 批量写回。
- Chunk 图片导入。
- AI `chunk write` 必须至少有一个 Ready 正交邻居。
- Chunk context 导出。
- 1 到 4 边透明模板生成。
- 正式图片写入和覆盖。
- 图片格式和尺寸校验。
- Composite Map 重建。
- JSON 输出。
- Desktop IPC client；Desktop 未运行时稳定返回 `desktop_not_running`。

### 13.3 明确延后

- 临时图片审批流程。
- 图片历史记录。
- App 内 Generate。
- App 内任务进度。
- 生成队列。
- 自动扩散。
- 并行生成。
- 自动接受阈值。
- 多人协作。
- Godot TileMap 导出。

## 14. 3x3 示例流程

1. 用户在 App 创建项目并上传 Concept Map。
2. 用户设置 3 Columns、3 Rows。
3. App 切出 9 个 concept regions。
4. 用户让 Codex 为所有 chunk 写描述。
5. Codex 运行 `chunkmap concept context --json`。
   如果 App 正在管理该项目，CLI 自动把命令转发给 Desktop queue。
6. Codex 分析概念图并运行 `chunkmap prompts import`。
7. App 自动显示 9 个 prompt。
8. 用户逐块查看并修改 prompt。
9. 用户在 `(2,2)` 导入一张细节图。
10. 该图片尺寸成为项目 chunk size。
11. 用户在 Codex 中要求生成 `(1,2)`。
12. Codex 运行 `chunkmap chunk context 1,2 --json`。
13. Codex使用 template 和 prompt 生图。
14. Codex运行 `chunkmap chunk write 1,2 --image ...`。
15. Desktop command completion 直接更新 `(1,2)` 正式图和最新 Composite Map。
16. 用户在 App 查看正式图和接缝。
17. 如果想调整结果，用户编辑 prompt，再让 Codex重新生成 `(1,2)`。
18. Codex再次运行同一个 `chunk write` 命令，新图覆盖旧图。

## 15. UX 验收标准

- App 中不存在生成、队列、图片审批或历史记录控件。
- App 不显示 AI 任务百分比或生成阶段。
- 每个 chunk 图片状态只有 Empty 和 Ready。
- 每个 chunk 只存在一张正式图片。
- 重新生成会覆盖旧图，不保留历史版本。
- 第一张导入图片的原图尺寸决定项目 chunk size。
- 用户图片可导入任意坐标且无需邻居；AI `chunk write` 必须有 Ready 正交邻居。
- Concept Map 切片后，Codex 能通过 CLI 为所有 chunk 写回 prompt。
- 用户能选择尚未生成的 chunk，查看和修改 prompt。
- Codex 能用一条 CLI 命令导出某个 chunk 的完整生成 context。
- Codex 能用一条 CLI 命令把图片写成正式图。
- CLI 写回后，运行中的 App 无需重启即可显示结果。
- 所有正式写入都经过 Desktop DocumentCommandQueue，CLI 不能本地绕过。
- Desktop 未运行时 CLI 明确失败且不修改项目。
- Desktop UI command 与 CLI IPC command 调用同一个 Dispatcher。
- App 能查看正式地图、缩放、拖拽和检查接缝。
- Concept Map region 不会进入 chunk 生图的图片参考输入。
