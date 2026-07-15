#include "image/image_buffer.h"
#include "io/atomic_file.h"
#include "model/project_document.h"
#include "project/project_service.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

class TempWorkspace {
public:
    TempWorkspace() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("chunkmap_test_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }

    ~TempWorkspace() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path make_image(const std::string& name, int width, int height) const {
        chunkmap::ImageBuffer image(width, height);
        auto& pixels = image.rgba();
        for (std::size_t index = 0; index < pixels.size(); index += 4U) {
            pixels[index] = 20U;
            pixels[index + 1U] = 100U;
            pixels[index + 2U] = 180U;
            pixels[index + 3U] = 255U;
        }
        const auto output = path / name;
        REQUIRE(image.save_png(output).ok());
        return output;
    }

    std::filesystem::path path;
};

chunkmap::Project create_project(TempWorkspace& workspace,
                                 chunkmap::ProjectService& service,
                                 const std::string& name = "test-world") {
    chunkmap::CreateProjectRequest request;
    request.name = name;
    request.concept_image = workspace.make_image("concept.png", 12, 8);
    request.columns = 3;
    request.rows = 2;

    auto created = service.create_project(request);
    REQUIRE(created.ok());
    return created.take_value();
}

}  // namespace

TEST_CASE("project creation persists only minimal formal inputs") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);

    CHECK(project.config.columns == 3);
    CHECK(project.config.rows == 2);
    CHECK_FALSE(project.config.has_chunk_size());
    CHECK(std::filesystem::is_regular_file(project.paths.concept_source()));
    CHECK_FALSE(std::filesystem::exists(project.paths.global_prompt()));
    auto global_prompt = service.read_global_prompt(project);
    REQUIRE(global_prompt.ok());
    CHECK(global_prompt.value().empty());

    CHECK(std::filesystem::is_empty(project.paths.chunks_dir()));
    CHECK(std::filesystem::is_empty(project.paths.prompts_dir()));
    CHECK_FALSE(std::filesystem::exists(project.paths.root() / "cache"));
    CHECK_FALSE(std::filesystem::exists(project.paths.root() / "context"));
    CHECK_FALSE(std::filesystem::exists(project.paths.root() / "concept"));
    auto project_json = chunkmap::atomic_file::read_text(project.paths.project_json());
    REQUIRE(project_json.ok());
    const auto config = nlohmann::json::parse(project_json.value());
    CHECK(config.size() == 5);
    CHECK(config.at("schema_version") == 3);
    CHECK_FALSE(config.contains("name"));
    CHECK_FALSE(config.contains("concept_file"));
    CHECK_FALSE(config.contains("feather_ratio"));
    CHECK(service.validate(project).ok());
}

TEST_CASE("empty project grid can change before the first chunk import") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    REQUIRE(service.write_global_prompt(project, "Shared pixel style").ok());

    REQUIRE(service.update_grid(project, 5, 4).ok());
    CHECK(project.config.columns == 5);
    CHECK(project.config.rows == 4);
    auto reopened = service.open_project(project.config.name);
    REQUIRE(reopened);
    CHECK(reopened.value().config.columns == 5);
    CHECK(reopened.value().config.rows == 4);
    auto global = service.read_global_prompt(reopened.value());
    REQUIRE(global);
    CHECK(global.value() == "Shared pixel style");
}

TEST_CASE("project grid locks when local prompts or chunk images exist") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);

    REQUIRE(service.write_prompt(project, {0, 0}, "Northern region").ok());
    auto with_prompt = service.update_grid(project, 4, 3);
    REQUIRE_FALSE(with_prompt);
    CHECK(with_prompt.error().code == "grid_has_prompts");
    REQUIRE(service.write_prompt(project, {0, 0}, "").ok());

    const auto image = workspace.make_image("grid-lock.png", 9, 7);
    REQUIRE(service.import_chunk_image(project, {0, 0}, image).ok());
    auto with_chunk = service.update_grid(project, 4, 3);
    REQUIRE_FALSE(with_chunk);
    CHECK(with_chunk.error().code == "grid_locked");
}

TEST_CASE("global prompt can be edited independently from chunk prompts") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);

    REQUIRE(service.write_global_prompt(project, "Top-down GBA pixel art").ok());
    REQUIRE(service.write_prompt(project, {1, 1}, "A forest path").ok());
    auto global = service.read_global_prompt(project);
    auto chunk = service.read_prompt(project, {1, 1});
    REQUIRE(global.ok());
    REQUIRE(chunk.ok());
    CHECK(global.value() == "Top-down GBA pixel art");
    CHECK(chunk.value() == "A forest path");
}

TEST_CASE("empty prompts remove sparse files") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);

    REQUIRE(service.write_prompt(project, {1, 0}, "temporary").ok());
    REQUIRE(service.write_global_prompt(project, "temporary style").ok());
    REQUIRE(service.write_prompt(project, {1, 0}, "").ok());
    REQUIRE(service.write_global_prompt(project, "").ok());
    CHECK_FALSE(std::filesystem::exists(project.paths.chunk_prompt({1, 0})));
    CHECK_FALSE(std::filesystem::exists(project.paths.global_prompt()));
}

TEST_CASE("schema v1 migration keeps formal content and removes derived files") {
    TempWorkspace workspace;
    const auto root = workspace.path / "output" / "legacy-world";
    std::filesystem::create_directories(root / "concept/regions");
    std::filesystem::create_directories(root / "chunks/0_0/history");
    std::filesystem::create_directories(root / "chunks/1_0");
    std::filesystem::create_directories(root / "cache/seams");
    std::filesystem::create_directories(root / "context/chunk_0_0");
    const auto concept = workspace.make_image("legacy-concept.png", 12, 8);
    const auto chunk = workspace.make_image("legacy-chunk.png", 9, 7);
    REQUIRE(chunkmap::atomic_file::write_binary(
        root / "concept/source.png", chunkmap::atomic_file::read_binary(concept).value()).ok());
    REQUIRE(chunkmap::atomic_file::write_binary(
        root / "chunks/0_0/image.png", chunkmap::atomic_file::read_binary(chunk).value()).ok());
    REQUIRE(chunkmap::atomic_file::write_text(root / "chunks/0_0/prompt.md", "keep prompt").ok());
    REQUIRE(chunkmap::atomic_file::write_text(root / "chunks/1_0/prompt.md", "").ok());
    REQUIRE(chunkmap::atomic_file::write_text(root / "global_prompt.md", "legacy style").ok());
    const nlohmann::json legacy = {
        {"schema_version", 1}, {"name", "legacy-world"},
        {"columns", 2}, {"rows", 1}, {"chunk_size", {9, 7}},
        {"overlap_ratio", {0.15, 0.15}}, {"feather_ratio", 0.03},
        {"concept_file", "concept/source.png"}};
    REQUIRE(chunkmap::atomic_file::write_text(root / "project.json", legacy.dump(2)).ok());

    chunkmap::ProjectService service(workspace.path);
    auto migrated = service.open_project("legacy-world");
    REQUIRE(migrated.ok());
    CHECK(migrated.value().config.schema_version == 3);
    CHECK(std::filesystem::is_regular_file(root / "concept.png"));
    CHECK(std::filesystem::is_regular_file(root / "chunks/0_0.png"));
    CHECK(std::filesystem::is_regular_file(root / "prompts/0_0.md"));
    CHECK(std::filesystem::is_regular_file(root / "global_prompt.md"));
    CHECK_FALSE(std::filesystem::exists(root / "concept"));
    CHECK_FALSE(std::filesystem::exists(root / "context"));
    CHECK_FALSE(std::filesystem::exists(root / "cache"));
    CHECK_FALSE(std::filesystem::exists(root / "chunks/0_0"));
    CHECK_FALSE(std::filesystem::exists(root / "prompts/1_0.md"));
}

TEST_CASE("schema v2 migration changes only project metadata") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto source = workspace.make_image("v2-source.png", 16, 12);
    REQUIRE(service.import_chunk_image(project, {0, 0}, source));
    const auto chunk_path = project.paths.chunk_image({0, 0});
    const auto before = chunkmap::atomic_file::read_binary(chunk_path);
    REQUIRE(before);
    auto project_json = chunkmap::atomic_file::read_text(project.paths.project_json());
    REQUIRE(project_json);
    auto parsed = nlohmann::json::parse(project_json.value());
    parsed["schema_version"] = 2;
    REQUIRE(chunkmap::atomic_file::write_text(
        project.paths.project_json(), parsed.dump(2) + '\n'));

    auto migrated = service.open_project(project.config.name);
    REQUIRE(migrated);
    CHECK(migrated.value().config.schema_version == 3);
    CHECK(chunkmap::atomic_file::read_binary(chunk_path).value() == before.value());
}

TEST_CASE("legacy project without global prompt treats it as empty") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    std::error_code error;
    std::filesystem::remove(project.paths.global_prompt(), error);
    REQUIRE_FALSE(error);

    auto global = service.read_global_prompt(project);
    REQUIRE(global.ok());
    CHECK(global.value().empty());
    CHECK(service.validate(project).ok());
    REQUIRE(service.write_global_prompt(project, "Pixel art").ok());
    CHECK(std::filesystem::is_regular_file(project.paths.global_prompt()));
}

TEST_CASE("first imported image determines chunk size and survives reload") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto image = workspace.make_image("first.png", 9, 7);

    auto imported = service.import_chunk_image(project, {2, 1}, image);
    REQUIRE(imported.ok());
    REQUIRE(project.config.has_chunk_size());
    CHECK(*project.config.chunk_width == 9);
    CHECK(*project.config.chunk_height == 7);

    auto reopened = service.open_project("test-world");
    REQUIRE(reopened.ok());
    CHECK(reopened.value().config.chunk_width == 9);
    CHECK(reopened.value().config.chunk_height == 7);

    auto saved_image = chunkmap::ImageBuffer::load(reopened.value().paths.chunk_image({2, 1}));
    REQUIRE(saved_image.ok());
    CHECK(saved_image.value().width() == 9);
    CHECK(saved_image.value().height() == 7);
    auto project_json = chunkmap::atomic_file::read_text(reopened.value().paths.project_json());
    REQUIRE(project_json.ok());
    CHECK_FALSE(nlohmann::json::parse(project_json.value()).contains("seed"));
    CHECK(service.validate(reopened.value()).ok());
}

TEST_CASE("multiple imported images require exact dimensions without requiring neighbors") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto first = workspace.make_image("first.png", 9, 7);
    REQUIRE(service.import_chunk_image(project, {2, 1}, first).ok());

    const auto second = workspace.make_image("second.png", 9, 7);
    auto imported = service.import_chunk_image(project, {0, 0}, second);
    REQUIRE(imported.ok());
    CHECK(std::filesystem::is_regular_file(project.paths.chunk_image({0, 0})));

    const auto one_pixel_short = workspace.make_image("short.png", 8, 7);
    auto normalized = service.import_chunk_image(project, {1, 1}, one_pixel_short);
    REQUIRE_FALSE(normalized.ok());
    CHECK(normalized.error().code == "chunk_size_mismatch");
    CHECK_FALSE(std::filesystem::exists(project.paths.chunk_image({1, 1})));

    const auto wrong_size = workspace.make_image("wrong.png", 7, 7);
    auto rejected = service.import_chunk_image(project, {2, 0}, wrong_size);
    REQUIRE_FALSE(rejected.ok());
    CHECK(rejected.error().code == "chunk_size_mismatch");
    CHECK_FALSE(std::filesystem::exists(project.paths.chunk_image({2, 0})));
}

TEST_CASE("chunk import preserves user pixels even beside a Ready neighbor") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto neighbor = workspace.make_image("neighbor.png", 20, 20);
    REQUIRE(service.import_chunk_image(project, {0, 0}, neighbor).ok());

    chunkmap::ImageBuffer imported_image(20, 20);
    for (int y = 0; y < imported_image.height(); ++y) {
        for (int x = 0; x < imported_image.width(); ++x) {
            auto* pixel = imported_image.pixel(x, y);
            pixel[0] = 210U;
            pixel[1] = 30U;
            pixel[2] = 40U;
            pixel[3] = 255U;
        }
    }
    const auto imported_path = workspace.path / "user-import.png";
    REQUIRE(imported_image.save_png(imported_path).ok());
    REQUIRE(service.import_chunk_image(project, {1, 0}, imported_path).ok());
    auto official = chunkmap::ImageBuffer::load(project.paths.chunk_image({1, 0}));
    REQUIRE(official);
    CHECK(official.value().pixel(0, 10)[0] == 210U);
    CHECK(official.value().pixel(0, 10)[1] == 30U);
}

TEST_CASE("alignment placement persists without changing formal PNG bytes") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto neighbor_path = workspace.make_image("neighbor.png", 30, 30);
    REQUIRE(service.import_chunk_image(project, {0, 0}, neighbor_path).ok());

    chunkmap::ImageBuffer source(30, 30);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            auto* pixel = source.pixel(x, y);
            pixel[0] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
            pixel[1] = static_cast<std::uint8_t>((x * 2 + y * 11) % 256);
            pixel[2] = static_cast<std::uint8_t>((x * 13 + y) % 256);
            pixel[3] = 255U;
        }
    }
    const auto source_path = workspace.path / "source.png";
    REQUIRE(source.save_png(source_path).ok());
    REQUIRE(service.import_chunk_image(project, {1, 0}, source_path).ok());
    const auto official_path = project.paths.chunk_image({1, 0});
    const auto before = chunkmap::atomic_file::read_binary(official_path);
    REQUIRE(before);

    chunkmap::NeighborImages neighbors;
    auto neighbor = chunkmap::ImageBuffer::load(project.paths.chunk_image({0, 0}));
    REQUIRE(neighbor);
    neighbors.left = neighbor.take_value();
    auto preview = service.preview_chunk_alignment(
        project, {1, 0}, source, neighbors, false, 2, -1);
    REQUIRE(preview);
    CHECK(preview.value().registration.offset_x == 2);
    CHECK(preview.value().registration.offset_y == -1);
    auto after_preview = chunkmap::atomic_file::read_binary(official_path);
    REQUIRE(after_preview);
    CHECK(after_preview.value() == before.value());

    auto applied = service.apply_chunk_shift(
        project, {1, 0}, 2, -1);
    REQUIRE(applied);
    auto after_apply = chunkmap::atomic_file::read_binary(official_path);
    REQUIRE(after_apply);
    CHECK(after_apply.value() == before.value());
    CHECK(project.layout.placement({1, 0}).offset_x == 2);
    CHECK(project.layout.placement({1, 0}).offset_y == -1);
    auto reopened = service.open_project(project.config.name);
    REQUIRE(reopened);
    CHECK(reopened.value().layout.placement({1, 0}).offset_x == 2);
    CHECK(reopened.value().layout.placement({1, 0}).offset_y == -1);
}

TEST_CASE("seam parameters persist independently and never rewrite chunk PNGs") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto first = workspace.make_image("seam-first.png", 20, 20);
    const auto second = workspace.make_image("seam-second.png", 20, 20);
    REQUIRE(service.import_chunk_image(project, {0, 0}, first));
    REQUIRE(service.import_chunk_image(project, {1, 0}, second));
    const auto first_before = chunkmap::atomic_file::read_binary(
        project.paths.chunk_image({0, 0}));
    const auto second_before = chunkmap::atomic_file::read_binary(
        project.paths.chunk_image({1, 0}));
    REQUIRE(first_before);
    REQUIRE(second_before);

    chunkmap::SeamDefinition seam;
    seam.key = {{0, 0}, chunkmap::SeamDirection::Right};
    seam.feather_width = 2;
    seam.points = {{0.0, 0.35}, {0.5, 0.7}, {1.0, 0.45}};
    REQUIRE(service.set_seam(project, seam));
    CHECK(std::filesystem::is_regular_file(project.paths.seam_file(seam.key)));
    CHECK(chunkmap::atomic_file::read_binary(project.paths.chunk_image({0, 0})).value() ==
          first_before.value());
    CHECK(chunkmap::atomic_file::read_binary(project.paths.chunk_image({1, 0})).value() ==
          second_before.value());

    auto reopened = service.open_project(project.config.name);
    REQUIRE(reopened);
    REQUIRE(reopened.value().layout.seams.count(seam.key) == 1U);
    CHECK(reopened.value().layout.seams.at(seam.key).points.size() == 3U);
    REQUIRE(service.reset_seam(project, seam.key));
    CHECK_FALSE(std::filesystem::exists(project.paths.seam_file(seam.key)));
}

TEST_CASE("prompt import overwrites listed chunks and preserves others") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);

    REQUIRE(service.write_prompt(project, {0, 0}, "keep me").ok());
    REQUIRE(service.write_prompt(project, {1, 0}, "old prompt").ok());

    const nlohmann::json input = {
        {"prompts", nlohmann::json::array({
            {{"x", 1}, {"y", 0}, {"prompt", "new prompt"}},
            {{"x", 2}, {"y", 1}, {"prompt", "south east"}},
        })},
    };
    const auto input_path = workspace.path / "prompts.json";
    REQUIRE(chunkmap::atomic_file::write_text(input_path, input.dump()).ok());
    REQUIRE(service.import_prompts(project, input_path).ok());

    auto unchanged = service.read_prompt(project, {0, 0});
    auto changed = service.read_prompt(project, {1, 0});
    auto added = service.read_prompt(project, {2, 1});
    REQUIRE(unchanged.ok());
    REQUIRE(changed.ok());
    REQUIRE(added.ok());
    CHECK(unchanged.value() == "keep me");
    CHECK(changed.value() == "new prompt");
    CHECK(added.value() == "south east");

    auto status = service.status(project);
    REQUIRE(status.ok());
    CHECK(status.value().prompts_with_content == 3);
}

TEST_CASE("invalid prompt batch is validated before any file is overwritten") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    REQUIRE(service.write_prompt(project, {0, 0}, "original").ok());

    const nlohmann::json input = {
        {"prompts", nlohmann::json::array({
            {{"x", 0}, {"y", 0}, {"prompt", "would overwrite"}},
            {{"x", 99}, {"y", 99}, {"prompt", "invalid"}},
        })},
    };
    const auto input_path = workspace.path / "invalid_prompts.json";
    REQUIRE(chunkmap::atomic_file::write_text(input_path, input.dump()).ok());

    auto imported = service.import_prompts(project, input_path);
    REQUIRE_FALSE(imported.ok());
    auto prompt = service.read_prompt(project, {0, 0});
    REQUIRE(prompt.ok());
    CHECK(prompt.value() == "original");
}

TEST_CASE("a corrupt image is not reported as a Ready chunk") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto image = workspace.make_image("first.png", 9, 7);
    REQUIRE(service.import_chunk_image(project, {2, 1}, image).ok());
    REQUIRE(chunkmap::atomic_file::write_text(
        project.paths.chunk_image({2, 1}), "not an image").ok());

    auto status = service.status(project);
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == "image_decode_failed");
    CHECK_FALSE(service.validate(project).ok());
}

TEST_CASE("concept context exports regions schema and manifest") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);

    auto context = service.export_concept_context(project);
    REQUIRE(context.ok());
    CHECK(context.value().regions.size() == 6);
    CHECK(std::filesystem::is_regular_file(context.value().manifest));
    CHECK(std::filesystem::is_regular_file(context.value().authoring_guide));
    CHECK(std::filesystem::is_regular_file(context.value().prompts_schema));
    const auto expected_handoff = workspace.path / ".chunkmap" / "handoff" / "test-world";
    CHECK(context.value().manifest.string().rfind(expected_handoff.string(), 0) == 0);
    CHECK_FALSE(context.value().manifest.string().rfind(project.paths.root().string(), 0) == 0);
    CHECK_FALSE(std::filesystem::exists(project.paths.root() / "context"));
    auto manifest = chunkmap::atomic_file::read_text(context.value().manifest);
    REQUIRE(manifest.ok());
    CHECK(manifest.value().find("concept_image") != std::string::npos);
    CHECK(manifest.value().find("authoring_guide") != std::string::npos);
    CHECK(manifest.value().find("prompts import") != std::string::npos);
    auto guide = chunkmap::atomic_file::read_text(context.value().authoring_guide);
    REQUIRE(guide.ok());
    CHECK(guide.value().find("Specification version: 3") != std::string::npos);
    CHECK(guide.value().find("Preserve model freedom") != std::string::npos);
    CHECK(guide.value().find("Interpret Concept symbols semantically") != std::string::npos);
    CHECK(guide.value().find("gameplay-ready overworld tilemap") != std::string::npos);
    CHECK(guide.value().find("Generation-time Prompt discipline") != std::string::npos);
    CHECK(guide.value().find("actual Desktop result also depends on placement") !=
          std::string::npos);
}

TEST_CASE("concept slice export writes one external grid region") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto output = workspace.path / "concept slice 2_1.png";

    auto exported = service.export_concept_slice(project, {2, 1}, output, false);
    REQUIRE(exported.ok());
    CHECK(exported.value().output == std::filesystem::weakly_canonical(output));
    CHECK(exported.value().width == 4);
    CHECK(exported.value().height == 4);
    auto image = chunkmap::ImageBuffer::load(output);
    REQUIRE(image.ok());
    CHECK(image.value().width() == 4);
    CHECK(image.value().height() == 4);

    auto existing = service.export_concept_slice(project, {2, 1}, output, false);
    REQUIRE_FALSE(existing.ok());
    CHECK(existing.error().code == "export_exists");
    REQUIRE(service.export_concept_slice(project, {2, 1}, output, true).ok());

    auto inside = service.export_concept_slice(
        project, {0, 0}, project.paths.root() / "derived.png", false);
    REQUIRE_FALSE(inside.ok());
    CHECK(inside.error().code == "export_inside_project");
}

TEST_CASE("chunk context requires a Ready neighbor and excludes concept references") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    const auto image = workspace.make_image("first.png", 10, 10);
    REQUIRE(service.import_chunk_image(project, {2, 1}, image).ok());

    auto unavailable = service.export_chunk_context(project, {0, 0});
    REQUIRE_FALSE(unavailable.ok());
    CHECK(unavailable.error().code == "no_ready_neighbor");

    REQUIRE(service.write_prompt(project, {1, 1}, "dense forest path").ok());
    REQUIRE(service.write_global_prompt(project, "Top-down GBA pixel art").ok());
    auto context = service.export_chunk_context(project, {1, 1});
    REQUIRE(context.ok());
    CHECK(context.value().ready_directions == std::vector<std::string>{"right"});
    CHECK(std::filesystem::is_regular_file(context.value().template_image));
    CHECK(std::filesystem::is_regular_file(context.value().mask_image));
    CHECK(std::filesystem::is_regular_file(context.value().global_prompt));
    CHECK(std::filesystem::is_regular_file(context.value().chunk_prompt));
    CHECK(context.value().manifest.string().rfind(
        (workspace.path / ".chunkmap" / "handoff" / "test-world").string(), 0) == 0);
    CHECK_FALSE(std::filesystem::exists(project.paths.root() / "context"));
    auto combined = chunkmap::atomic_file::read_text(context.value().prompt);
    REQUIRE(combined.ok());
    CHECK(combined.value() ==
          "[GLOBAL VISUAL STYLE]\nTop-down GBA pixel art\n\n"
          "[CHUNK CONTENT]\ndense forest path");
    auto mask = chunkmap::ImageBuffer::load(context.value().mask_image);
    REQUIRE(mask.ok());
    CHECK(mask.value().pixel(0, 5)[0] == 255);
    CHECK(mask.value().pixel(9, 5)[0] == 0);
    auto manifest = chunkmap::atomic_file::read_text(context.value().manifest);
    REQUIRE(manifest.ok());
    CHECK(manifest.value().find("concept_image") == std::string::npos);
    CHECK(manifest.value().find("concept/regions") == std::string::npos);
    CHECK(manifest.value().find("neighbor hash") == std::string::npos);
    CHECK(manifest.value().find("global_prompt.txt") != std::string::npos);
    CHECK(manifest.value().find("chunk_prompt.txt") != std::string::npos);

    REQUIRE(service.write_global_prompt(project, "").ok());
    auto context_without_global = service.export_chunk_context(project, {1, 1});
    REQUIRE(context_without_global.ok());
    auto chunk_only = chunkmap::atomic_file::read_text(context_without_global.value().prompt);
    REQUIRE(chunk_only.ok());
    CHECK(chunk_only.value() == "dense forest path");
}

TEST_CASE("chunk write preserves the generated PNG without baking overlap pixels") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    REQUIRE(service.import_chunk_image(
        project, {2, 1}, workspace.make_image("first.png", 10, 10)).ok());
    const auto first = workspace.make_image("first.png", 10, 10);
    auto first_write = service.write_chunk_image(project, {1, 1}, first);
    REQUIRE(first_write.ok());

    chunkmap::ImageBuffer replacement(10, 10);
    for (auto& value : replacement.rgba()) value = 77;
    const auto replacement_path = workspace.path / "replacement.png";
    REQUIRE(replacement.save_png(replacement_path).ok());
    auto second_write = service.write_chunk_image(project, {1, 1}, replacement_path);
    REQUIRE(second_write.ok());
    auto official = chunkmap::ImageBuffer::load(project.paths.chunk_image({1, 1}));
    REQUIRE(official.ok());
    CHECK(official.value().rgba()[0] == 77);
    CHECK(official.value().pixel(7, 5)[0] == 77);
    CHECK(official.value().pixel(9, 5)[0] == 77);
    CHECK(chunkmap::atomic_file::read_binary(project.paths.chunk_image({1, 1})).value() ==
          chunkmap::atomic_file::read_binary(replacement_path).value());
    CHECK_FALSE(std::filesystem::exists(project.paths.root() / "cache"));
    CHECK_FALSE(std::filesystem::exists(project.paths.root() / "context"));
}

TEST_CASE("chunk write records registration as placement and preserves generated bytes") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    constexpr int width = 240;
    constexpr int height = 160;
    constexpr int shift = 8;
    chunkmap::ImageBuffer target(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto* pixel = target.pixel(x, y);
            const int cell = ((x / 7) + (y / 5) * 3) % 11;
            pixel[0] = static_cast<std::uint8_t>((cell * 23 + x * 3) % 256);
            pixel[1] = static_cast<std::uint8_t>((cell * 41 + y * 5) % 256);
            pixel[2] = static_cast<std::uint8_t>((cell * 17 + x + y * 2) % 256);
            pixel[3] = 255U;
        }
    }
    chunkmap::ImageBuffer right(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto* pixel = right.pixel(x, y);
            pixel[3] = 255U;
        }
    }
    const int overlap = static_cast<int>(std::lround(width * 0.15));
    REQUIRE(right.blit(target, {width - overlap, 0, overlap, height}, {0, 0}));
    const auto right_path = workspace.path / "right-neighbor.png";
    REQUIRE(right.save_png(right_path));
    REQUIRE(service.import_chunk_image(project, {2, 1}, right_path));

    chunkmap::ImageBuffer generated(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int source_x = std::min(width - 1, x + shift);
            std::copy_n(target.pixel(source_x, y), 4, generated.pixel(x, y));
        }
    }
    const auto generated_path = workspace.path / "generated.png";
    REQUIRE(generated.save_png(generated_path));
    auto written = service.write_chunk_image(project, {1, 1}, generated_path);
    REQUIRE(written);
    CHECK(written.value().registration.applied);
    CHECK(written.value().registration.offset_x >= shift - 1);
    CHECK(written.value().registration.offset_x <= shift);
    CHECK(written.value().registration.offset_y == 0);
    CHECK(written.value().registration.score_after <
          written.value().registration.score_before);
    auto official = chunkmap::ImageBuffer::load(project.paths.chunk_image({1, 1}));
    REQUIRE(official);
    CHECK(chunkmap::atomic_file::read_binary(project.paths.chunk_image({1, 1})).value() ==
          chunkmap::atomic_file::read_binary(generated_path).value());
    CHECK(project.layout.placement({1, 1}).offset_x ==
          written.value().registration.offset_x);
}

TEST_CASE("chunk write rejects dimensions that would require normalization") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    REQUIRE(service.import_chunk_image(
        project, {2, 1}, workspace.make_image("first.png", 10, 10)).ok());
    const auto short_image = workspace.make_image("short.png", 9, 10);
    auto written = service.write_chunk_image(project, {1, 1}, short_image);
    REQUIRE_FALSE(written.ok());
    CHECK(written.error().code == "chunk_size_mismatch");
    CHECK_FALSE(std::filesystem::exists(project.paths.chunk_image({1, 1})));
    CHECK_FALSE(std::filesystem::exists(project.paths.root() / "cache"));
}

TEST_CASE("project and atomic IO support workspace paths with spaces") {
    TempWorkspace temporary;
    const auto workspace_path = temporary.path / "workspace with spaces";
    std::filesystem::create_directories(workspace_path);
    const auto concept_path = workspace_path / "concept source.png";
    chunkmap::ImageBuffer concept(12, 8);
    for (auto& value : concept.rgba()) value = 120;
    REQUIRE(concept.save_png(concept_path).ok());

    chunkmap::ProjectService service(workspace_path);
    chunkmap::CreateProjectRequest request;
    request.name = "space-path-world";
    request.concept_image = concept_path;
    request.columns = 3;
    request.rows = 2;
    auto created = service.create_project(request);
    REQUIRE(created.ok());
    CHECK(created.value().paths.root() ==
          std::filesystem::absolute(workspace_path / "output" / request.name).lexically_normal());

    const auto text_path = created.value().paths.chunk_prompt({0, 0});
    REQUIRE(chunkmap::atomic_file::write_text(text_path, "first").ok());
    REQUIRE(chunkmap::atomic_file::write_text(text_path, "second").ok());
    auto text = chunkmap::atomic_file::read_text(text_path);
    REQUIRE(text.ok());
    CHECK(text.value() == "second");
    CHECK_FALSE(std::filesystem::exists(text_path.string() + ".tmp"));
}

TEST_CASE("project rejects grids that exceed the hard safety limit") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    chunkmap::CreateProjectRequest request;
    request.name = "oversized-grid";
    request.concept_image = workspace.make_image("concept.png", 12, 8);
    request.columns = 1001;
    request.rows = 1000;
    auto created = service.create_project(request);
    REQUIRE_FALSE(created.ok());
    CHECK(created.error().code == "grid_too_large");
}

TEST_CASE("project document bounds its lazy image cache without changing Ready state") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    chunkmap::CreateProjectRequest request;
    request.name = "cache-world";
    request.concept_image = workspace.make_image("cache-concept.png", 10, 10);
    request.columns = 5;
    request.rows = 5;
    auto project = service.create_project(request);
    REQUIRE(project.ok());
    auto document = chunkmap::ProjectDocument::load(project.take_value());
    REQUIRE(document.ok());
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            document.value().replace_image({x, y}, chunkmap::ImageBuffer(4, 4));
        }
    }
    CHECK(document.value().ready_count() == 25);
    CHECK(document.value().cached_image_count() == 16);
}
