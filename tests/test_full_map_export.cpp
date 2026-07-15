#include "image/full_map_exporter.h"

#include "image/image_buffer.h"
#include "io/atomic_file.h"
#include "model/project_document.h"
#include "project/project_service.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class ExportWorkspace {
public:
    ExportWorkspace() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("chunkmap_export_test_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }

    ~ExportWorkspace() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path make_image(
        const std::string& name, std::array<std::uint8_t, 4> color) const {
        chunkmap::ImageBuffer image(4, 4);
        for (std::size_t index = 0; index < image.rgba().size(); index += 4U) {
            std::copy(color.begin(), color.end(), image.rgba().begin() +
                      static_cast<std::ptrdiff_t>(index));
        }
        const auto output = path / name;
        REQUIRE(image.save_png(output).ok());
        return output;
    }

    std::filesystem::path path;
};

chunkmap::Project create_export_project(
    ExportWorkspace& workspace, chunkmap::ProjectService& service,
    int columns = 2, int rows = 2) {
    chunkmap::CreateProjectRequest request;
    request.name = "export-world";
    request.concept_image = workspace.make_image("concept.png", {10U, 20U, 30U, 255U});
    request.columns = columns;
    request.rows = rows;
    request.horizontal_overlap_ratio = 0.25;
    request.vertical_overlap_ratio = 0.25;
    auto created = service.create_project(request);
    REQUIRE(created.ok());
    return created.take_value();
}

void check_pixel(const chunkmap::ImageBuffer& image, int x, int y,
                 std::array<std::uint8_t, 4> expected) {
    const auto* pixel = image.pixel(x, y);
    REQUIRE(pixel != nullptr);
    CHECK(std::equal(expected.begin(), expected.end(), pixel));
}

}  // namespace

TEST_CASE("full map export streams default seam patches in desktop draw order") {
    ExportWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_export_project(workspace, service);

    REQUIRE(service.import_chunk_image(
        project, {0, 0}, workspace.make_image("red.png", {255U, 0U, 0U, 255U})).ok());
    REQUIRE(service.import_chunk_image(
        project, {1, 0}, workspace.make_image("green.png", {0U, 255U, 0U, 255U})).ok());
    REQUIRE(service.import_chunk_image(
        project, {0, 1}, workspace.make_image("blue.png", {0U, 0U, 255U, 255U})).ok());
    REQUIRE(service.import_chunk_image(
        project, {1, 1}, workspace.make_image("yellow.png", {255U, 255U, 0U, 255U})).ok());
    const auto source_before = chunkmap::atomic_file::read_binary(
        project.paths.chunk_image({1, 1}));
    REQUIRE(source_before);
    const auto source_path = project.paths.chunk_image({1, 1});

    auto document = chunkmap::ProjectDocument::load(std::move(project));
    REQUIRE(document.ok());
    const auto output = workspace.path / "full-map.png";
    std::vector<std::pair<std::size_t, std::size_t>> progress;
    auto exported = chunkmap::export_full_map(
        document.value(), {output, false, 7U * 4U * 2U},
        [&](std::size_t completed, std::size_t total, const std::string&) {
            progress.emplace_back(completed, total);
        });
    REQUIRE(exported.ok());
    CHECK(exported.value().width == 7);
    CHECK(exported.value().height == 7);
    CHECK(exported.value().ready_chunks == 4);
    CHECK(exported.value().empty_chunks == 0);
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.front().first == 0U);
    CHECK(progress.back().first == progress.back().second);
    for (std::size_t index = 1; index < progress.size(); ++index) {
        CHECK(progress[index].first >= progress[index - 1U].first);
        CHECK(progress[index].second == progress.front().second);
    }

    auto image = chunkmap::ImageBuffer::load(output);
    REQUIRE(image.ok());
    CHECK(image.value().width() == 7);
    CHECK(image.value().height() == 7);
    check_pixel(image.value(), 0, 0, {255U, 0U, 0U, 255U});
    check_pixel(image.value(), 3, 0, {128U, 128U, 0U, 255U});
    check_pixel(image.value(), 0, 3, {128U, 0U, 128U, 255U});
    check_pixel(image.value(), 3, 3, {128U, 255U, 0U, 255U});
    check_pixel(image.value(), 6, 6, {255U, 255U, 0U, 255U});
    CHECK(chunkmap::atomic_file::read_binary(source_path).value() == source_before.value());
}

TEST_CASE("full map export leaves missing chunks transparent") {
    ExportWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_export_project(workspace, service, 2, 1);
    REQUIRE(service.import_chunk_image(
        project, {0, 0}, workspace.make_image("ready.png", {20U, 40U, 60U, 255U})).ok());
    auto document = chunkmap::ProjectDocument::load(std::move(project));
    REQUIRE(document.ok());

    const auto output = workspace.path / "sparse.png";
    auto exported = chunkmap::export_full_map(document.value(), {output});
    REQUIRE(exported.ok());
    CHECK(exported.value().ready_chunks == 1);
    CHECK(exported.value().empty_chunks == 1);
    auto image = chunkmap::ImageBuffer::load(output);
    REQUIRE(image.ok());
    CHECK(image.value().width() == 7);
    CHECK(image.value().height() == 4);
    check_pixel(image.value(), 0, 0, {20U, 40U, 60U, 255U});
    check_pixel(image.value(), 6, 0, {0U, 0U, 0U, 0U});
}

TEST_CASE("full map export protects project contents and existing files") {
    ExportWorkspace workspace;
    chunkmap::ProjectService service(workspace.path);
    auto project = create_export_project(workspace, service, 1, 1);
    const auto ready_image = workspace.make_image(
        "ready.png", {80U, 100U, 120U, 255U});
    REQUIRE(service.import_chunk_image(project, {0, 0}, ready_image).ok());
    REQUIRE(service.remove_chunk_image(project, {0, 0}).ok());
    auto empty_document = chunkmap::ProjectDocument::load(project);
    REQUIRE(empty_document.ok());
    auto no_ready = chunkmap::export_full_map(
        empty_document.value(), {workspace.path / "empty.png"});
    REQUIRE_FALSE(no_ready.ok());
    CHECK(no_ready.error().code == "no_ready_chunks");

    REQUIRE(service.import_chunk_image(project, {0, 0}, ready_image).ok());
    auto document = chunkmap::ProjectDocument::load(std::move(project));
    REQUIRE(document.ok());

    auto inside = chunkmap::export_full_map(
        document.value(), {document.value().project().paths.root() / "map.png"});
    REQUIRE_FALSE(inside.ok());
    CHECK(inside.error().code == "export_inside_project");

    const auto output = workspace.path / "existing.png";
    REQUIRE(chunkmap::ImageBuffer(1, 1).save_png(output).ok());
    auto existing = chunkmap::export_full_map(document.value(), {output});
    REQUIRE_FALSE(existing.ok());
    CHECK(existing.error().code == "export_exists");
    REQUIRE(chunkmap::export_full_map(document.value(), {output, true}).ok());
    auto replaced = chunkmap::ImageBuffer::load(output);
    REQUIRE(replaced.ok());
    CHECK(replaced.value().width() == 4);
    CHECK(replaced.value().height() == 4);

    CHECK_FALSE(std::filesystem::exists(document.value().project().paths.root() / "cache"));
    CHECK_FALSE(std::filesystem::exists(document.value().project().paths.root() / "composite.png"));
}
