# Full Map Export 设计

状态：已实现

## 1. 目标

为 Desktop 和 CLI 增加显式的整图 PNG 导出。导出只是用户主动触发的一次外部文件写入，
不是项目保存流程的一部分，也不会重新引入项目级 Composite。

核心契约：

- Desktop 的 File menu 提供 `Export Full Map...`；
- CLI 提供 `map export <output.png>`；
- CLI 仍然只通过 IPC 请求正在运行的 Desktop，不直接读取或修改项目；
- 导出读取 Desktop 当前的 `ProjectDocument`，因此结果与当前会话一致；
- 输出路径必须位于项目目录之外；
- 不在 `output/<project>/` 中创建 Composite、cache、临时文件或导出记录；
- 不修改 `project.json`、Prompt、chunk 状态或 `ChangeSet`；
- 导出失败不影响任何正式 chunk；
- v1 只导出原始全分辨率 RGBA PNG，不附加 Grid、坐标、Seam 标记或 UI。

## 2. 用户接口

### 2.1 Desktop

File menu 增加：

```text
Export Full Map...
```

点击后：

1. 打开原生 PNG 保存对话框；
2. 提交 `MapExport` 到 `DocumentCommandQueue`；
3. worker thread 执行导出，ImGui 主线程保持响应；
4. 导出期间禁用重复导出入口，并显示模态进度窗口；
5. 完成后显示尺寸和输出路径，并提供 `Reveal Exported File`。

进度按实际工作单元报告：chunk 解码/合成、band 编码和 PNG finalization。Desktop 与 CLI
触发的导出都会通过 command queue progress event 显示，不使用估算时间或假进度。当前不
提供取消、缩放、裁剪或格式选项。Command queue 保证导出看到一个稳定的 document 顺序；
导出完成前，后续正式 mutation 在队列中等待。

### 2.2 CLI

```bash
./build/cli/chunkmap --workspace "$PWD" --project my-world \
  map export /absolute/path/to/my-world.png
```

已有文件默认拒绝覆盖：

```bash
./build/cli/chunkmap --workspace "$PWD" --project my-world \
  map export /absolute/path/to/my-world.png --force
```

JSON 成功结果：

```json
{
  "ok": true,
  "command": "map export",
  "project": "my-world",
  "data": {
    "output": "/absolute/path/to/my-world.png",
    "size": [5454, 3634],
    "ready_chunks": 16,
    "empty_chunks": 0
  }
}
```

CLI 没有 Desktop host 时继续返回 `desktop_not_running`。

## 3. 合成语义

导出尺寸复用 `map_geometry()`：

```text
step_x       = chunk_width  - horizontal_overlap_px
step_y       = chunk_height - vertical_overlap_px
world_width  = chunk_width  + (columns - 1) * step_x
world_height = chunk_height + (rows    - 1) * step_y
```

chunk `(x, y)` 的左上角为 `(x * step_x, y * step_y)`。

像素覆盖顺序必须与 Desktop `draw_map()` 一致：按 `y` 从小到大、同一行按 `x` 从小到大
绘制，后绘制的 Ready chunk 覆盖重叠区域。这样 Desktop 当前看到的地图与导出的 PNG 完全
一致，不增加 feather、registration 或另一套混合规则。

Empty chunk 不绘制，对应未被任何 Ready chunk 覆盖的像素保持透明。只要至少有一个 Ready
chunk 就允许导出；完全空的项目返回 `no_ready_chunks`。成功结果明确返回 Empty 数量，避免
把不完整地图误认为完整成品。

## 4. 内存模型

不能重新创建 `world_width * world_height * 4` 的整图 RGBA buffer。v1 直接实现 scanline/
band streaming PNG：

```text
ProjectDocument
  -> map_geometry
  -> bounded RGBA row band
  -> Ready chunks in Desktop draw order
  -> streaming PNG writer
  -> external .tmp
  -> atomic rename to requested output
```

建议 band buffer 上限为 32 MiB：

```text
band_height = clamp(32 MiB / (world_width * 4), 1, chunk_height)
```

每个 band 先清成透明，再加载与该 band 相交的 Ready chunk 并按正式绘制顺序 blit，随后将
band 中的 scanline 顺序写入 PNG encoder。输出内存随地图高度不增长。

当前 `stb_image_write` 的 PNG API 要求完整输入 buffer，不适合该契约。Core 使用 zlib
实现逐行 PNG writer（链接 `ZLIB::ZLIB`，直接写 IHDR/IDAT/IEND），chunk 解码仍复用
`ImageBuffer`。band 算法不得
绕过 `ProjectDocument::image()`，这样外部文件变更检查和当前 document cache 语义仍然有效。

超宽地图还需在分配前检查：

- `world_width * 4` 是否溢出；
- 单行是否超过可表示的 buffer 大小；
- PNG 的宽高限制；
- 输出盘空间不足、编码失败和 rename 失败。

导出写到目标目录中的唯一临时兄弟文件，成功后原子替换。任何失败都删除临时文件。没有
`--force` 时，目标已存在必须在创建临时文件前失败。

## 5. 代码结构

新增：

```text
src/image/full_map_exporter.h
src/image/full_map_exporter.cpp
src/image/png_stream_writer.h
src/image/png_stream_writer.cpp
```

建议接口：

```cpp
struct FullMapExportOptions {
    std::filesystem::path output;
    bool overwrite = false;
    std::size_t maximum_band_bytes = 32U * 1024U * 1024U;
};

struct FullMapExportResult {
    std::filesystem::path output;
    int width = 0;
    int height = 0;
    int ready_chunks = 0;
    int empty_chunks = 0;
};

Result<FullMapExportResult> export_full_map(
    ProjectDocument& document,
    const FullMapExportOptions& options);
```

Command 层新增：

- `CommandType::MapExport`；
- `MapExportPayload { path, overwrite }`；
- codec 和 JSON contract；
- Dispatcher 调用 `export_full_map()`；
- 成功结果不设置任何 `changes.*` 字段。

CLI 新增 `run_map()`；Desktop 新增文件对话框、pending request id 和 completion UI。正式导出
实现放在 Core，Desktop 与 CLI 不能各自维护一套合成逻辑。

## 6. 路径与项目边界

Dispatcher 在开始导出前规范化目标路径并拒绝：

- 相对路径；
- 非 `.png` 后缀；
- 项目根目录本身或其任意子路径；
- 不存在的父目录；
- 已存在但未显式允许覆盖的文件；
- 目标是目录、symlink 到项目内部，或无法安全规范化的路径。

允许导出到 workspace 的其他位置，只要不位于当前项目根目录。导出不进入
`ProjectPaths`，因为它不是项目资产。

## 7. 错误码

第一版使用稳定、可测试的错误码：

```text
missing_chunk_dimensions
no_ready_chunks
invalid_export_path
export_inside_project
export_parent_missing
export_exists
map_dimensions_overflow
export_row_too_large
image_decode_failed
image_encode_failed
export_write_failed
```

## 8. 测试

### Core

- 1x1 Ready chunk 导出像素完全一致；
- 2x2 不同纯色 chunk 验证尺寸、位置和重叠区 topmost ownership；
- 缺少 chunk 时输出透明洞，Ready/Empty 计数正确；
- 完全空项目失败；
- 横向和纵向 overlap 使用与 `map_geometry()` 相同的取整结果；
- 极小 band 上限强制跨多个 band，结果仍与基准图逐像素一致；
- 重复导出产生确定性像素结果；
- 失败后不遗留临时文件；
- 拒绝项目内路径，且项目目录白名单保持不变；
- 已存在文件的默认拒绝和显式覆盖；
- 导出前后 `project.json`、Prompt、chunks 和 document Ready 状态不变。

### Command / CLI / Desktop

- `MapExport` request/response codec round trip；
- JSON contract 包含 output、size、Ready/Empty 计数；
- CLI 无 Desktop 时返回 `desktop_not_running`；
- CLI 使用 Desktop 当前内存 document；
- Desktop smoke test 覆盖应用启动，Core 与 CLI integration 覆盖正式导出入口；
- 导出在 worker thread 执行，进度事件单调到达 100%，completion 后关闭弹窗并恢复入口。

## 9. 实施顺序

1. 引入 `ZLIB::ZLIB`，实现并单测 streaming writer；
2. 实现 band composer 和像素所有权测试；
3. 接入 `MapExport` command、codec 和 Dispatcher；
4. 接入 CLI `map export`；
5. 接入 Desktop `Export Full Map...`；
6. 更新 README、`AGENTS.md` 和过时设计文档标记；
7. 完整执行 CMake build 与 `ctest`。

## 10. 明确不做

- 不把导出 PNG 记入项目；
- 不恢复自动 Composite；
- 不在 chunk import/write 后触发导出；
- 不导出 Grid、坐标、选区、Seam Inspector 或其他 UI overlay；
- 不增加 feather、registration、重采样或颜色校正；
- 不做 JPEG/WebP、缩放、裁剪、分块包、进度条和取消；
- 不让 CLI 在 Desktop 未运行时直接访问项目文件。
