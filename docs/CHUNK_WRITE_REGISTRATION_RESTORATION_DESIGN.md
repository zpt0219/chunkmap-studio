# 原始 Chunk、非破坏 Placement 与可编辑 Seam 设计

状态：第一版已实现

这份文档替代原先“平移、羽化、恢复 overlap 后覆盖 Chunk PNG”的设计。新的首要原则是：

> Chunk PNG 是不可变源素材。Registration、裁切、折线 Seam 与羽化都只保存参数，任何
> App 操作都不得改写 Chunk PNG 像素。

## 1. 结论

项目不再把一张处理后的 Chunk PNG 当作最终显示状态，也不恢复项目内 Composite。地图由
三类正式输入实时排版得到：

```text
原始 Chunk PNG
  + Chunk Placement 参数
  + 相邻 Chunk 的 Seam 参数
  -> Desktop Map Canvas / Generation Context / Full Map Export
```

职责固定为：

- Chunk PNG：保存用户导入或外部生成器产出的原始 PNG；
- Registration：分析相邻图像，只产生一个可保存的 `(offset_x, offset_y)`；
- Placement：在固定 Chunk footprint 内非破坏地平移采样源 PNG；
- Seam：保存 overlap 中的可编辑折线和羽化宽度；
- Renderer：只在 overlap 中生成派生 Seam patch，并与原始 Chunk texture 排版；
- Full Map Export：显式请求时流式渲染到项目外 PNG，不创建项目 Composite。

原有 Low-resolution 2D 与 Projection registration 算法继续保留。需要撤销的是把 offset
烘焙进 PNG、单侧羽化、protected overlap 恢复和 Apply 后覆盖 PNG 的行为。

## 2. 不可违反的产品契约

### 2.1 原始 PNG 不可变

`chunk import`、`chunk write` 和 Desktop Replace 均执行同一条源素材契约：

1. 解码输入，验证它是可读 PNG；
2. 第一张图片建立项目 Chunk 尺寸；
3. 后续图片必须与项目尺寸严格相同；
4. 按原始字节原子复制到 `chunks/<x>_<y>.png`；
5. 不做 1px normalization、不重编码、不平移、不羽化、不恢复邻居像素。

尺寸不一致时返回错误，由用户或外部生成器修正；App 不以“修图”方式接受输入。

这里的“不可变”表示所有编辑操作不得覆盖源 PNG。Replace 是用户显式提供一个新的原始
PNG，因此可以原子替换该坐标的源文件，但不能用 App 派生结果替换它。

### 2.2 没有运行时 Full Map 图片

Desktop 不持有、保存或持续重建一张完整合成图。Map Canvas 由 Chunk source texture、
placement sampling 和小尺寸 Seam patch 直接排版。允许存在：

- GPU 中的原始 Chunk texture；
- 内存中的 placement preview；
- 内存/GPU 中按需生成的 overlap Seam patch；
- 用户显式导出的最终 Full Map PNG。

不允许存在项目内 Composite、Seam PNG cache 或隐藏的处理后 Chunk 副本。

### 2.3 所有视图共用同一排版语义

Desktop Map Canvas、Seam Editor、Full Map Export 与 `chunk context` 必须调用同一套 Core
sampling / seam 规则。不能出现 Desktop 看起来正确、Export 或下一次生成 Context 使用了
另一套像素的情况。

## 3. 正式持久化格式

新格式建议提升为 schema v3：

```text
output/<project-name>/
  project.json
  concept.png
  global_prompt.md                 # 非空时存在
  prompts/<x>_<y>.md               # 非空时存在
  chunks/<x>_<y>.png               # 原始 PNG
  placements.json                  # 仅非零 placement 存在时创建
  seams/
    <x>_<y>_right.json             # 仅用户修改过的 Seam override
    <x>_<y>_bottom.json
```

`project.json` 继续只保存网格、Chunk 尺寸与 overlap ratio；schema 版本变为 3。默认
placement 和默认 Seam 可以推导，不重复持久化。

### 3.1 Placement 文件

`placements.json` 只记录非零 offset：

```json
{
  "schema_version": 1,
  "placements": [
    {"x": 0, "y": 2, "offset": [42, 0]}
  ]
}
```

- 未出现的 Ready Chunk 等价于 `[0,0]`；
- offset 是累计 placement，不是相对于当前 PNG 的一次性增量；
- Apply 修改 JSON 和内存 document，不修改 PNG；
- Reset 删除该坐标记录，空文件集合时删除 `placements.json`；
- registration score、候选算法和置信诊断仍只属于瞬时结果，不持久化。

### 3.2 Seam 文件

每对相邻 Chunk 只有一个 canonical Seam：

- 左右相邻：左侧坐标的 `right` Seam；
- 上下相邻：上侧坐标的 `bottom` Seam；
- 不保存 `left` 或 `top` 副本。

```json
{
  "schema_version": 1,
  "first": [0, 1],
  "direction": "right",
  "feather_width": 24,
  "points": [
    {"along": 0.0, "across": 0.50},
    {"along": 0.32, "across": 0.43},
    {"along": 0.68, "across": 0.61},
    {"along": 1.0, "across": 0.50}
  ]
}
```

- `along` 是沿 Seam 长边的归一化位置；
- `across` 是折线在 overlap 宽度中的归一化位置；
- points 按 `along` 严格递增，首尾必须是 `0.0` 和 `1.0`；
- 折线必须是单值函数，不允许回折、自交或越出 overlap；
- `feather_width` 是像素宽度，限制在 `[0, overlap_pixels]`；
- 没有 override 文件时，自动使用 overlap 中央直线和默认 3% Chunk 羽化宽度；
- Reset to Straight 删除 override 文件，回到可推导默认值。

Seam 文件只保存参数，不保存 preview、difference、blend texture 或输入图片 hash。

## 4. Placement 语义

Chunk 在世界中的 footprint 始终由网格与 overlap 决定，不因为 offset 改变位置或尺寸。
Placement 改变的是 footprint 内对源 PNG 的采样：

```text
display(x, y) = source(clamp(x - offset_x), clamp(y - offset_y))
```

第一版使用整数像素与 nearest sampling，不缩放、不旋转、不做亚像素插值。超出源图的采样
使用 edge clamp，复现当前平移预览的边缘延展，但不生成新 PNG。

offset 的安全范围继续由 overlap 决定：

```text
maximum_x = min(96, floor(overlap_x * 2 / 3))
maximum_y = min(64, floor(overlap_y * 2 / 3))
```

外层 world boundary 上的大幅 placement 可能暴露 edge clamp；Panel 必须实时显示结果，让
用户决定是否保存。

## 5. Registration 只产生 Placement

### 5.1 Auto 算法

现有两个独立候选算法继续运行：

1. Low-resolution 2D；
2. Projection row/column profiles。

它们读取原始 Chunk 和当前邻居 placement 后的采样结果，使用共同的原尺寸精修、评分和
置信度门槛选优。算法输出：

```text
suggested_offset
score_before
score_after
relative_improvement
candidate comparison
```

算法不得返回或保存一张平移后的正式图片。

### 5.2 Import 与 Write

- `chunk import`：保存原始 PNG，初始 placement 为 `[0,0]`；Auto 只在用户点击后运行；
- `chunk write`：保存外部生成器的原始 PNG，然后自动计算可信 placement；
- Auto 未通过置信门时，placement 保持 `[0,0]`；
- 源 PNG 与 placement 分别原子写入；若 placement 持久化失败，源 PNG 仍是有效 Ready
  source，并以 `[0,0]` 重新加载，不能留下半写 JSON；
- registration 失败不得回退到修改 PNG。

### 5.3 Alignment Panel

```text
PLACEMENT
Horizontal  [  42 ] px
Vertical    [   0 ] px

[Auto]
Low-resolution  +42, 0   21.0%   Accepted
Projection      +39, 1   19.4%   Accepted

[Reset] [Save Placement]
```

- 输入框显示当前累计 placement；
- 拖动或输入只修改临时参数，并立即更新 Map Canvas 和相邻 Seam patches；
- Auto 填入建议，但不自动覆盖已保存的手工 placement；
- Save Placement 只写 `placements.json`；
- Cancel、切换项目或 Reload 丢弃未保存 preview；
- 替换 Chunk PNG 默认保留 placement 与 Seam override，避免静默删除用户排版；用户可 Reset。

## 6. Editable Seam 模型

### 6.1 输入

一条 Seam 的输入只有：

- 两张相邻的原始 Chunk PNG；
- 两张图各自的 placement；
- canonical overlap rectangle；
- polyline points；
- feather width。

Concept、Prompt、非相邻 Chunk、旧 preview 和 Full Map 不参与 Seam 计算。

### 6.2 折线求值

左右 Seam 对每一行计算一个折线中心 `center_x(y)`；上下 Seam 对每一列计算
`center_y(x)`。中心由相邻 control points 线性插值得到。

这种约束不是任意二维路径，但有三个重要优点：

- 不会自交或形成无法解释的封闭区域；
- 每个像素只需一次 segment interpolation；
- 拖动时可以稳定地在 overlap 像素量级实时更新。

### 6.3 羽化

以左右 Seam 为例：

```text
d = local_x - center_x(y)
t = clamp(0.5 + d / feather_width, 0, 1)
result = lerp(left_sample, right_sample, t)
```

上下 Seam 使用相同公式，只交换 x/y。`feather_width == 0` 表示沿折线硬切。第一版沿用
确定性的 RGBA 线性插值；不引入 Poisson blending、multiband blending 或内容感知补图。

所有采样先应用相应 Chunk placement。算法只访问 overlap，不修改两侧 Chunk buffer。

### 6.4 四块交点

四个 Chunk 同时相交时，pairwise Seam patches 会交叉。第一版采用固定两阶段顺序：

1. 每行先应用所有 `right` Seam；
2. 再在行结果之间应用所有 `bottom` Seam。

Desktop、Export 与 Context 必须使用相同顺序。不能依赖 ImGui 当前 draw-call 顺序偶然决定
交点颜色。Golden test 锁定 2x2 和 3x3 交点结果。

## 7. Seam Editor

两张 Ready Chunk 的整个 overlap band 都是 Seam 命中区。悬停高亮完整 band，点击后
Inspector 从 Chunk/Prompt 切换到该 Seam 的编辑内容；点击普通 Chunk 则退出 Seam 编辑。
地图始终可见，用户拖动时可以同时观察主地图。

```text
Inspector
Seam (0,1) -> (1,1)   right overlap only

Feather [24] px   [Auto Boundary]

┌──────────────────────────────────────────────────────┐
│                    overlap preview                   │
│       ●────────●                                     │
│                 ╲                                    │
│                  ●────────────●                      │
└──────────────────────────────────────────────────────┘

Click: Add point   Drag: Move   Right-click: Remove

[Save Seam] [Use Auto Default] [Cancel]
```

交互规则：

- Canvas 与 Inspector 共用同一份当前融合 patch texture；
- 左键拖动 control point；
- 单击空白位置增加 point；
- 右键中间 point 删除；首尾 point 不可删除；
- point 的 `along` 不能越过相邻 point，`across` 限制在 overlap；
- Feather slider 与 point 拖动均实时重算当前 overlap；
- Cancel 只在参数实际变化时恢复已保存 patch，并释放两张编辑源图；
- Save Seam 通过正式 command 保存 JSON，不写 PNG。

Auto Boundary 只恢复当前 Core 默认折线，不静默保存；没有实际参数变化时 Save 与
Use Auto Default 保持禁用，避免写入无意义的默认 override。

## 8. 实时渲染与性能

典型 1024x1024 Chunk、15% overlap 的一条竖直 Seam 只处理：

```text
154 x 1024 = 157,696 pixels
```

每次交互的工作是 control-point 插值、两次 placement sampling 和一次 RGBA lerp，适合在
拖动帧实时执行。实现要求：

- 只在参数或相邻 source/placement 改变时标记 Seam dirty；
- 只重建受影响的 Seam patch；
- patch 完成后更新对应小纹理，不上传完整地图；
- 相邻 Chunk 替换或 placement 变化时，只失效其最多四条 Seam；
- derived patch 只存在于内存/GPU，不写项目 cache。

Map Canvas 的概念模型为：

```text
draw placement-sampled Chunk textures
  -> draw canonical right Seam patches
  -> draw canonical bottom Seam patches
  -> overlays / selection / coordinates
```

实现可以使用 CPU 生成 patch 后上传纹理；第一版不要求自定义 OpenGL shader。若 CPU profiling
证明拖动低于目标帧率，再考虑 GPU mask，不提前增加复杂度。

## 9. Generation Context 与 mask

`chunk context` 必须从当前 source + placement + Seam 排版结果获取邻居视觉边界，不能假设
邻居 PNG 已经被平移或恢复 protected pixels。

当前“整个 overlap 黑色保护、写回后再次 restore”的契约需要改变：

- App 不再对生成结果执行 post-write protected restore；
- 外部生成器输出什么 PNG，项目就保存什么 PNG；
- template 继续提供当前排版后的邻居上下文；
- mask 只保护最外侧 1px anchor，overlap 其余部分允许生成，使两张 Chunk 在 overlap 内都
  保留可供 Seam 选择的内容；
- manifest 明确返回 anchor width、editable overlap 和 expected size；
- 外部生成器不遵守 mask 时，App 不暗中修复 PNG，只在 Seam Editor 中让用户处理结果。

详细 Chunk 生成仍不得使用 Concept crop 作为图像参考。

## 10. Full Map Export

Full Map Export 是用户请求的输出，不是项目运行状态。Exporter 继续使用 bounded RGBA band
和 streaming PNG writer，但每个 band 的像素来自统一 Layout Renderer：

```text
source PNG sampling
  -> placement
  -> right seams
  -> bottom seams
  -> encode output rows
```

Exporter 不构建完整 CPU canvas，不写回 Chunk，不创建 Composite，不修改 placement/Seam。

## 11. Command 与 mutation 边界

所有正式 mutation 继续经过：

```text
DocumentCommandQueue -> CommandDispatcher -> ProjectSession -> ProjectService
```

建议的 typed commands：

### `ChunkPlacementPreview`

- 输入 coord、手动 offset 或 `auto=true`；
- 返回 placement 参数、候选比较与受影响 Seam preview；
- 不写文件，不产生 `ChangeSet`。

### `ChunkPlacementSet`

- 保存累计 offset 到 `placements.json`；
- 更新 document placement；
- 返回更新后的 project snapshot，并标记 `project_changed`；
- 不返回处理后正式 PNG。

### `SeamSet`

- 验证 canonical key、points 和 feather width；
- 原子保存一个 Seam override；
- 更新 document Seam 参数；
- 返回更新后的 project snapshot，并标记 `project_changed`。

### `SeamReset`

- 删除 override，恢复推导默认值；
- 不删除或修改 Chunk PNG。

### `chunk remove`

- 删除该坐标原始 PNG；
- 删除其 placement；
- 删除与它直接相邻的 Seam overrides，因为 pair 已不存在；
- 不影响其他 Chunk 或 Seam。

Desktop 可以在打开 Seam Editor 时一次取得两张 source 和参数快照，然后使用纯 Core
`SeamRenderer` 在本地内存实时预览；只有 Save/Reset 进入 command queue。不要为每个鼠标
移动事件排队一个正式 command。

## 12. Seam Inspect 重新定义

原来的 `seam inspect == 0.0` 依赖 protected overlap 被逐像素复制，和新模型不再兼容。
新的 Seam Inspect 分成两组信息：

- Raw difference：placement 后两张原始 overlap 的平均差异；
- Composited continuity：羽化带两侧边缘的颜色/梯度跳变。

Raw difference 高并不必然代表最终 Seam 差；它主要帮助用户决定折线应绕开哪些结构。UI
不得再用 `0.0` 作为唯一验收标准。

## 13. Schema v2 项目迁移

当前 schema v2 Chunk 可能已经经历 normalization、registration、feather 或 protected
restore。项目没有保存更早的原始文件，因此迁移不能恢复丢失像素。

一次性 v2 -> v3 迁移规则：

1. 现有 `chunks/<x>_<y>.png` 原样保留，从迁移时起视为不可变 source；
2. 所有 placement 默认为 `[0,0]`；
3. 所有相邻 Ready pair 使用推导的中央直线 Seam；
4. 不重编码、不重写、不尝试反向恢复旧 PNG；
5. 用户若需要真正的双侧 overlap 内容，必须显式重新 Import/Replace 或重新生成相关 Chunk。

迁移后所有新操作都遵守不可变 PNG 契约。

## 14. 当前实现需要撤销或改造的部分

- 删除 `ProjectService` 写回中的 `ImageRegistration::translate -> ImageFeathering ->
  protected restore -> save_png`；
- `ImageRegistration` 保留分析和评分，平移改成 renderer sampling 参数；
- `ChunkShiftApply` 的语义改为 placement set，不得保存处理后图片；
- 删除 1px normalization 接受路径，改为严格尺寸验证；
- `TemplateBuilder` 改为读取统一 Layout Renderer 的邻居边界；
- Map Canvas 从“后绘制 Chunk 拥有 overlap”改为显式 Seam patch 所有权；
- Full Map Exporter 使用相同 SeamRenderer；
- 删除旧 `ImageFeathering::blend_and_restore()` 与 1px normalizer，避免保留第二套语义；
- README、AGENTS、当前架构文档与 Prompt Authoring Guide 在实现时同步更新。

这不是在当前 bake 流程上增加一个 UI，而是替换图片状态模型。不得同时保留“Apply 覆盖
PNG”和“PNG 永远原始”两套语义。

## 15. 实施顺序

1. **Schema 与原始 PNG 写入**：v3 repository、严格尺寸验证、原字节原子复制；
2. **Placement model**：document/runtime/persistence、现有双 registration 算法输出参数；
3. **Seam core**：canonical key、validation、polyline interpolation、patch renderer、golden；
4. **Map Canvas**：原始 texture 排版、placement preview、right/bottom patch 与失效；
5. **Seam Editor**：Inspector 内容、overlap band 命中、控制点编辑、实时 preview、
   Save/Reset command；
6. **Exporter 与 Context**：切换到统一 Layout Renderer；
7. **Migration 与文档**：v2 -> v3、行为说明与 Agent 工作流；
8. **清理旧 bake 路径**：删除不再使用的正式图片变换与相应测试。

每一步必须保持可以构建和测试；不能先删除旧显示路径而留下无法查看地图的中间状态。

## 16. 测试计划

### 原始 PNG

- import/write 后目标文件字节与输入完全一致；
- 尺寸差 1px 也明确拒绝；
- Save Placement、Save Seam、Full Map Export 均不改变 Chunk 文件 hash；
- Replace 只在用户提供新 PNG 时改变 hash。

### Placement

- 两个候选算法仍恢复已知 offset；
- Preview 不写文件；
- Save 只改 `placements.json`；
- Reload 后累计 placement 一致；
- Reset 删除稀疏记录；
- edge clamp sampling 与当前整数平移结果一致。

### Seam core

- 默认中央直线；
- horizontal/vertical polyline interpolation；
- `feather_width=0` 硬切；
- 非零 width 的线性权重；
- point 排序、边界、自交约束；
- 2x2 与 3x3 交点 golden；
- 相同输入与参数得到逐像素确定结果。

### Desktop

- 拖动 point 只重建当前 Seam patch；
- 主 Map Canvas 与 editor preview 同步；
- Cancel 不写文件；
- Save/Reset 只改变 Seam JSON；
- placement 变化只失效最多四条相邻 Seam；
- 切换项目、Reload 和关闭 editor 正确丢弃临时状态。

### Export 与 Context

- Full Map Export 与 Desktop 抽样像素一致；
- Export 仍使用 bounded memory；
- Context template 使用 placement 和 Seam 后的邻居视觉边界；
- 所有流程都不创建项目 Composite、Seam PNG cache 或处理后 Chunk 副本。

## 17. 验收标准

1. 任意 App 编辑前后，未 Replace 的 Chunk PNG 字节完全不变。
2. Auto/手动 placement 可保存、重载并影响 Map Canvas，但不修改 PNG。
3. 用户可以在 Inspector 的 Seam Editor 拖动折线与羽化宽度，并实时看到 Inspector 和
   主地图结果。
4. Seam 计算严格限制在 overlap；典型 1024px Chunk 拖动保持交互流畅。
5. 没有 override 的 Seam 自动使用中央直线；保存后只持久化参数。
6. Desktop、Full Map Export 与 Generation Context 对相同像素给出一致结果。
7. 项目内没有 Composite、Seam 图片 cache、处理后 Chunk 或隐藏原图副本。
8. schema v2 项目无损保留现有 PNG，并明确告知无法恢复旧处理前像素。
9. 完整 CMake build 与 `ctest` 通过。

## 18. 明确不做

- 不修改、重编码或覆盖源 Chunk PNG；
- 不保存处理后 Chunk 副本；
- 不创建项目 Full Map Composite；
- 不保存 Seam preview/difference/blend 图片；
- 不增加 undo/history、candidate、provenance 或 revision；
- 不做任意闭合 mask、Bezier 曲线、自交折线或自由手绘区域；
- 不做 Poisson、multiband、内容感知修复或 AI 自动描 Seam；
- 第一版不增加 Seam CLI 编辑命令，正式 Desktop mutation 仍使用 typed command。

## 19. 评审重点

实现前只需确认以下边界没有被后续代码稀释：

1. PNG 是不可变源素材，所有视觉调整都是参数；
2. placement 是固定 footprint 内的采样 transform，不移动网格；
3. Seam 是相邻 pair 的独立正式参数，只计算 overlap；
4. Desktop 没有完整合成大图，只有原始 Chunk texture 与小 Seam patch；
5. Export 和 Context 复用同一排版器；
6. 任何 Apply/Save 都不能把派生像素写回 Chunk PNG。
