#include "image/image_buffer.h"
#include "io/atomic_file.h"
#include "project/project_service.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>
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

TEST_CASE("project creation leaves chunk size empty and creates prompt files") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);

    CHECK(project.config.columns == 3);
    CHECK(project.config.rows == 2);
    CHECK_FALSE(project.config.has_chunk_size());
    CHECK(std::filesystem::is_regular_file(project.paths.concept_source()));

    for (int y = 0; y < project.config.rows; ++y) {
        for (int x = 0; x < project.config.columns; ++x) {
            CHECK(std::filesystem::is_regular_file(
                project.paths.chunk_prompt({x, y})));
            CHECK(std::filesystem::is_regular_file(
                project.paths.concept_region({x, y})));
        }
    }
    CHECK(service.validate(project).ok());
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
    auto metadata = chunkmap::atomic_file::read_text(
        reopened.value().paths.chunk_metadata({2, 1}));
    REQUIRE(metadata.ok());
    CHECK_FALSE(nlohmann::json::parse(metadata.value()).contains("is_seed"));
    auto project_json = chunkmap::atomic_file::read_text(reopened.value().paths.project_json());
    REQUIRE(project_json.ok());
    CHECK_FALSE(nlohmann::json::parse(project_json.value()).contains("seed"));
    CHECK(service.validate(reopened.value()).ok());
}

TEST_CASE("multiple imported images share normalization without requiring neighbors") {
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
    REQUIRE(normalized.ok());
    CHECK(normalized.value().added_left + normalized.value().added_right == 1);
    auto normalized_image = chunkmap::ImageBuffer::load(project.paths.chunk_image({1, 1}));
    REQUIRE(normalized_image.ok());
    CHECK(normalized_image.value().width() == 9);

    const auto wrong_size = workspace.make_image("wrong.png", 7, 7);
    auto rejected = service.import_chunk_image(project, {2, 0}, wrong_size);
    REQUIRE_FALSE(rejected.ok());
    CHECK(rejected.error().code == "chunk_size_mismatch");
    CHECK_FALSE(std::filesystem::exists(project.paths.chunk_image({2, 0})));
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
    CHECK(std::filesystem::is_regular_file(context.value().prompts_schema));
    auto manifest = chunkmap::atomic_file::read_text(context.value().manifest);
    REQUIRE(manifest.ok());
    CHECK(manifest.value().find("concept_image") != std::string::npos);
    CHECK(manifest.value().find("prompts import") != std::string::npos);
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
    auto context = service.export_chunk_context(project, {1, 1});
    REQUIRE(context.ok());
    CHECK(context.value().ready_directions == std::vector<std::string>{"right"});
    CHECK(std::filesystem::is_regular_file(context.value().template_image));
    CHECK(std::filesystem::is_regular_file(context.value().mask_image));
    auto mask = chunkmap::ImageBuffer::load(context.value().mask_image);
    REQUIRE(mask.ok());
    CHECK(mask.value().pixel(0, 5)[0] == 255);
    CHECK(mask.value().pixel(9, 5)[0] == 0);
    auto manifest = chunkmap::atomic_file::read_text(context.value().manifest);
    REQUIRE(manifest.ok());
    CHECK(manifest.value().find("concept_image") == std::string::npos);
    CHECK(manifest.value().find("concept/regions") == std::string::npos);
    CHECK(manifest.value().find("neighbor hash") == std::string::npos);
}

TEST_CASE("chunk write overwrites the official image and rebuilds composite") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    REQUIRE(service.import_chunk_image(
        project, {2, 1}, workspace.make_image("first.png", 10, 10)).ok());
    const auto first = workspace.make_image("first.png", 10, 10);
    auto first_write = service.write_chunk_image(project, {1, 1}, first);
    REQUIRE(first_write.ok());
    CHECK(std::filesystem::is_regular_file(first_write.value().composite));

    chunkmap::ImageBuffer replacement(10, 10);
    for (auto& value : replacement.rgba()) value = 77;
    const auto replacement_path = workspace.path / "replacement.png";
    REQUIRE(replacement.save_png(replacement_path).ok());
    auto second_write = service.write_chunk_image(project, {1, 1}, replacement_path);
    REQUIRE(second_write.ok());
    CHECK(std::filesystem::is_regular_file(
        project.paths.seam_dir({1, 1}, "right") / "metrics.json"));
    auto official = chunkmap::ImageBuffer::load(project.paths.chunk_image({1, 1}));
    REQUIRE(official.ok());
    CHECK(official.value().rgba()[0] == 77);
    CHECK_FALSE(std::filesystem::exists(project.paths.chunk_dir({1, 1}) / "history"));

    auto composite = chunkmap::ImageBuffer::load(project.paths.composite_png());
    REQUIRE(composite.ok());
    CHECK(composite.value().width() == 26);
    CHECK(composite.value().height() == 18);
}

TEST_CASE("chunk write records deterministic 1px normalization") {
    TempWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_project(workspace, service);
    REQUIRE(service.import_chunk_image(
        project, {2, 1}, workspace.make_image("first.png", 10, 10)).ok());
    const auto short_image = workspace.make_image("short.png", 9, 10);
    auto written = service.write_chunk_image(project, {1, 1}, short_image);
    REQUIRE(written.ok());
    CHECK(written.value().added_left + written.value().added_right == 1);
    CHECK_FALSE(written.value().registration_applied);
    auto official = chunkmap::ImageBuffer::load(project.paths.chunk_image({1, 1}));
    REQUIRE(official.ok());
    CHECK(official.value().width() == 10);
    auto metadata_text = chunkmap::atomic_file::read_text(
        project.paths.chunk_metadata({1, 1}));
    REQUIRE(metadata_text.ok());
    const auto metadata = nlohmann::json::parse(metadata_text.value());
    CHECK(metadata.at("registration").at("applied") == false);
    CHECK(metadata.at("registration").at("offset_x") == 0);
    CHECK(metadata.at("registration").at("offset_y") == 0);
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
