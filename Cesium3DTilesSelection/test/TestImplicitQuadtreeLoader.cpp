#include "ImplicitQuadtreeLoader.h"

#include <Cesium3DTilesContent/SubtreeAvailability.h>
#include <Cesium3DTilesContent/registerAllTileContentTypes.h>
#include <Cesium3DTilesSelection/Tile.h>
#include <Cesium3DTilesSelection/TileContent.h>
#include <Cesium3DTilesSelection/TileLoadResult.h>
#include <Cesium3DTilesSelection/TilesetContentLoader.h>
#include <CesiumAsync/AsyncSystem.h>
#include <CesiumGeometry/OrientedBoundingBox.h>
#include <CesiumGeospatial/BoundingRegion.h>
#include <CesiumGeospatial/Ellipsoid.h>
#include <CesiumGeospatial/S2CellBoundingVolume.h>
#include <CesiumGeospatial/S2CellID.h>
#include <CesiumGltf/Model.h>
#include <CesiumNativeTests/SimpleAssetAccessor.h>
#include <CesiumNativeTests/SimpleAssetRequest.h>
#include <CesiumNativeTests/SimpleAssetResponse.h>
#include <CesiumNativeTests/SimpleTaskProcessor.h>
#include <CesiumNativeTests/readFile.h>
#include <CesiumUtility/Math.h>

#include <doctest/doctest.h>
#include <glm/ext/matrix_double3x3.hpp>
#include <glm/ext/vector_double3.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace doctest;
using namespace Cesium3DTilesContent;
using namespace Cesium3DTilesSelection;
using namespace CesiumGeometry;
using namespace CesiumGeospatial;
using namespace CesiumUtility;
using namespace CesiumNativeTests;

namespace {
std::filesystem::path testDataPath = Cesium3DTilesSelection_TEST_DATA_DIR;
}

TEST_CASE("Test implicit quadtree loader") {
  Cesium3DTilesContent::registerAllTileContentTypes();

  auto pMockedAssetAccessor = std::make_shared<SimpleAssetAccessor>(
      std::map<std::string, std::shared_ptr<SimpleAssetRequest>>{});

  CesiumAsync::AsyncSystem asyncSystem{std::make_shared<SimpleTaskProcessor>()};

  ImplicitQuadtreeLoader loader{
      "tileset.json",
      "content/{level}.{x}.{y}.b3dm",
      "subtrees/{level}.{x}.{y}.json",
      5,
      5,
      OrientedBoundingBox(glm::dvec3(0.0), glm::dmat3(20.0))};

  SUBCASE("Load tile that does not have quadtree ID") {
    Tile tile(&loader);
    tile.setTileID("This is a test tile");

    TileLoadInput loadInput{
        tile,
        {},
        asyncSystem,
        pMockedAssetAccessor,
        spdlog::default_logger(),
        {}};

    auto tileLoadResultFuture = loader.loadTileContent(loadInput);

    asyncSystem.dispatchMainThreadTasks();

    auto tileLoadResult = tileLoadResultFuture.wait();
    CHECK(tileLoadResult.state == TileLoadResultState::Failed);
  }

  SUBCASE("Load empty tile") {
    // add subtree with all empty tiles
    loader.addSubtreeAvailability(
        QuadtreeTileID{0, 0, 0},
        SubtreeAvailability{
            ImplicitTileSubdivisionScheme::Quadtree,
            5,
            SubtreeAvailability::SubtreeConstantAvailability{true},
            SubtreeAvailability::SubtreeConstantAvailability{false},
            {SubtreeAvailability::SubtreeConstantAvailability{false}},
            {}});

    // check that this tile will have empty content
    Tile tile(&loader);
    tile.setTileID(QuadtreeTileID{1, 0, 1});

    TileLoadInput loadInput{
        tile,
        {},
        asyncSystem,
        pMockedAssetAccessor,
        spdlog::default_logger(),
        {}};

    auto tileLoadResultFuture = loader.loadTileContent(loadInput);

    asyncSystem.dispatchMainThreadTasks();

    auto tileLoadResult = tileLoadResultFuture.wait();
    CHECK(std::holds_alternative<TileEmptyContent>(tileLoadResult.contentKind));
    CHECK(!tileLoadResult.updatedBoundingVolume);
    CHECK(!tileLoadResult.updatedContentBoundingVolume);
    CHECK(!tileLoadResult.tileInitializer);
    CHECK(tileLoadResult.state == TileLoadResultState::Success);
  }

  SUBCASE("Load tile with render content") {
    // add subtree with all available tiles
    loader.addSubtreeAvailability(
        QuadtreeTileID{0, 0, 0},
        SubtreeAvailability{
            ImplicitTileSubdivisionScheme::Quadtree,
            5,
            SubtreeAvailability::SubtreeConstantAvailability{true},
            SubtreeAvailability::SubtreeConstantAvailability{false},
            {SubtreeAvailability::SubtreeConstantAvailability{true}},
            {}});

    // mock tile content b3dm
    auto pMockCompletedResponse = std::make_unique<SimpleAssetResponse>(
        static_cast<uint16_t>(200),
        "doesn't matter",
        CesiumAsync::HttpHeaders{},
        readFile(testDataPath / "BatchTables" / "batchedWithJson.b3dm"));

    auto pMockCompletedRequest = std::make_shared<SimpleAssetRequest>(
        "GET",
        "doesn't matter",
        CesiumAsync::HttpHeaders{},
        std::move(pMockCompletedResponse));

    pMockedAssetAccessor->mockCompletedRequests.insert(
        {"content/2.1.1.b3dm", std::move(pMockCompletedRequest)});

    // check that this tile has render content
    Tile tile(&loader);
    tile.setTileID(QuadtreeTileID{2, 1, 1});

    TileLoadInput loadInput{
        tile,
        {},
        asyncSystem,
        pMockedAssetAccessor,
        spdlog::default_logger(),
        {}};

    auto tileLoadResultFuture = loader.loadTileContent(loadInput);

    asyncSystem.dispatchMainThreadTasks();

    auto tileLoadResult = tileLoadResultFuture.wait();
    CHECK(
        std::holds_alternative<CesiumGltf::Model>(tileLoadResult.contentKind));
    CHECK(!tileLoadResult.updatedBoundingVolume);
    CHECK(!tileLoadResult.updatedContentBoundingVolume);
    CHECK(!tileLoadResult.tileInitializer);
    CHECK(tileLoadResult.state == TileLoadResultState::Success);
  }

  SUBCASE("load unknown content") {
    // add subtree with all available tiles
    loader.addSubtreeAvailability(
        QuadtreeTileID{0, 0, 0},
        SubtreeAvailability{
            ImplicitTileSubdivisionScheme::Quadtree,
            5,
            SubtreeAvailability::SubtreeConstantAvailability{true},
            SubtreeAvailability::SubtreeConstantAvailability{false},
            {SubtreeAvailability::SubtreeConstantAvailability{true}},
            {}});

    // mock tile content b3dm
    auto pMockCompletedResponse = std::make_unique<SimpleAssetResponse>(
        static_cast<uint16_t>(200),
        "doesn't matter",
        CesiumAsync::HttpHeaders{},
        std::vector<std::byte>(20));

    auto pMockCompletedRequest = std::make_shared<SimpleAssetRequest>(
        "GET",
        "doesn't matter",
        CesiumAsync::HttpHeaders{},
        std::move(pMockCompletedResponse));

    pMockedAssetAccessor->mockCompletedRequests.insert(
        {"content/2.1.1.b3dm", std::move(pMockCompletedRequest)});

    // check that this tile has render content
    Tile tile(&loader);
    tile.setTileID(QuadtreeTileID{2, 1, 1});

    TileLoadInput loadInput{
        tile,
        {},
        asyncSystem,
        pMockedAssetAccessor,
        spdlog::default_logger(),
        {}};

    auto tileLoadResultFuture = loader.loadTileContent(loadInput);

    asyncSystem.dispatchMainThreadTasks();

    auto tileLoadResult = tileLoadResultFuture.wait();
    CHECK(tileLoadResult.state == TileLoadResultState::Failed);
  }
}

namespace {

const Tile&
findTile(const std::span<const Tile>& children, const QuadtreeTileID& tileID) {
  auto it = std::find_if(
      children.begin(),
      children.end(),
      [tileID](const Tile& tile) {
        const QuadtreeTileID* pID =
            std::get_if<QuadtreeTileID>(&tile.getTileID());
        if (!pID)
          return false;
        return *pID == tileID;
      });
  REQUIRE(it != children.end());
  return *it;
}

const Tile&
findTile(const std::vector<Tile>& children, const QuadtreeTileID& tileID) {
  return findTile(std::span<const Tile>(children), tileID);
}

} // namespace

TEST_CASE("Test tile subdivision for implicit quadtree loader") {
  Cesium3DTilesContent::registerAllTileContentTypes();

  auto pMockedAssetAccessor = std::make_shared<SimpleAssetAccessor>(
      std::map<std::string, std::shared_ptr<SimpleAssetRequest>>{});

  CesiumAsync::AsyncSystem asyncSystem{std::make_shared<SimpleTaskProcessor>()};

  SUBCASE("Subdivide bounding box tile") {
    OrientedBoundingBox loaderBoundingVolume{glm::dvec3(0.0), glm::dmat3(20.0)};
    ImplicitQuadtreeLoader loader{
        "tileset.json",
        "content/{level}.{x}.{y}.b3dm",
        "subtrees/{level}.{x}.{y}.json",
        5,
        5,
        loaderBoundingVolume};

    // add subtree with all available tiles
    loader.addSubtreeAvailability(
        QuadtreeTileID{0, 0, 0},
        SubtreeAvailability{
            ImplicitTileSubdivisionScheme::Quadtree,
            5,
            SubtreeAvailability::SubtreeConstantAvailability{true},
            SubtreeAvailability::SubtreeConstantAvailability{false},
            {SubtreeAvailability::SubtreeConstantAvailability{true}},
            {}});

    // check subdivide root tile first
    Tile tile(&loader);
    tile.setTileID(QuadtreeTileID(0, 0, 0));
    tile.setBoundingVolume(loaderBoundingVolume);

    {
      auto tileChildrenResult = loader.createTileChildren(tile);
      CHECK(tileChildrenResult.state == TileLoadResultState::Success);

      const auto& tileChildren = tileChildrenResult.children;
      CHECK(tileChildren.size() == 4);

      const auto& tile_1_0_0 = findTile(tileChildren, QuadtreeTileID(1, 0, 0));
      const auto& box_1_0_0 =
          std::get<OrientedBoundingBox>(tile_1_0_0.getBoundingVolume());
      CHECK(box_1_0_0.getCenter() == glm::dvec3(-10.0, -10.0, 0.0));
      CHECK(box_1_0_0.getHalfAxes()[0] == glm::dvec3(10.0, 0.0, 0.0));
      CHECK(box_1_0_0.getHalfAxes()[1] == glm::dvec3(0.0, 10.0, 0.0));
      CHECK(box_1_0_0.getHalfAxes()[2] == glm::dvec3(0.0, 0.0, 20.0));

      const auto& tile_1_1_0 = findTile(tileChildren, QuadtreeTileID(1, 1, 0));
      const auto& box_1_1_0 =
          std::get<OrientedBoundingBox>(tile_1_1_0.getBoundingVolume());
      CHECK(box_1_1_0.getCenter() == glm::dvec3(10.0, -10.0, 0.0));
      CHECK(box_1_1_0.getHalfAxes()[0] == glm::dvec3(10.0, 0.0, 0.0));
      CHECK(box_1_1_0.getHalfAxes()[1] == glm::dvec3(0.0, 10.0, 0.0));
      CHECK(box_1_1_0.getHalfAxes()[2] == glm::dvec3(0.0, 0.0, 20.0));

      const auto& tile_1_0_1 = findTile(tileChildren, QuadtreeTileID(1, 0, 1));
      const auto& box_1_0_1 =
          std::get<OrientedBoundingBox>(tile_1_0_1.getBoundingVolume());
      CHECK(box_1_0_1.getCenter() == glm::dvec3(-10.0, 10.0, 0.0));
      CHECK(box_1_0_1.getHalfAxes()[0] == glm::dvec3(10.0, 0.0, 0.0));
      CHECK(box_1_0_1.getHalfAxes()[1] == glm::dvec3(0.0, 10.0, 0.0));
      CHECK(box_1_0_1.getHalfAxes()[2] == glm::dvec3(0.0, 0.0, 20.0));

      const auto& tile_1_1_1 = findTile(tileChildren, QuadtreeTileID(1, 1, 1));
      const auto& box_1_1_1 =
          std::get<OrientedBoundingBox>(tile_1_1_1.getBoundingVolume());
      CHECK(box_1_1_1.getCenter() == glm::dvec3(10.0, 10.0, 0.0));
      CHECK(box_1_1_1.getHalfAxes()[0] == glm::dvec3(10.0, 0.0, 0.0));
      CHECK(box_1_1_1.getHalfAxes()[1] == glm::dvec3(0.0, 10.0, 0.0));
      CHECK(box_1_1_1.getHalfAxes()[2] == glm::dvec3(0.0, 0.0, 20.0));

      tile.createChildTiles(std::move(tileChildrenResult.children));
    }

    // check subdivide one of the root children
    {
      auto& tile_1_1_0 = tile.getChildren()[1];
      auto tileChildrenResult = loader.createTileChildren(tile_1_1_0);
      CHECK(tileChildrenResult.state == TileLoadResultState::Success);

      const auto& tileChildren = tileChildrenResult.children;
      CHECK(tileChildren.size() == 4);

      const auto& tile_2_2_0 = findTile(tileChildren, QuadtreeTileID(2, 2, 0));
      const auto& box_2_2_0 =
          std::get<OrientedBoundingBox>(tile_2_2_0.getBoundingVolume());
      CHECK(box_2_2_0.getCenter() == glm::dvec3(5.0, -15.0, 0.0));
      CHECK(box_2_2_0.getHalfAxes()[0] == glm::dvec3(5.0, 0.0, 0.0));
      CHECK(box_2_2_0.getHalfAxes()[1] == glm::dvec3(0.0, 5.0, 0.0));
      CHECK(box_2_2_0.getHalfAxes()[2] == glm::dvec3(0.0, 0.0, 20.0));

      const auto& tile_2_3_0 = findTile(tileChildren, QuadtreeTileID(2, 3, 0));
      const auto& box_2_3_0 =
          std::get<OrientedBoundingBox>(tile_2_3_0.getBoundingVolume());
      CHECK(box_2_3_0.getCenter() == glm::dvec3(15.0, -15.0, 0.0));
      CHECK(box_2_3_0.getHalfAxes()[0] == glm::dvec3(5.0, 0.0, 0.0));
      CHECK(box_2_3_0.getHalfAxes()[1] == glm::dvec3(0.0, 5.0, 0.0));
      CHECK(box_2_3_0.getHalfAxes()[2] == glm::dvec3(0.0, 0.0, 20.0));

      const auto& tile_2_2_1 = findTile(tileChildren, QuadtreeTileID(2, 2, 1));
      const auto& box_2_2_1 =
          std::get<OrientedBoundingBox>(tile_2_2_1.getBoundingVolume());
      CHECK(box_2_2_1.getCenter() == glm::dvec3(5.0, -5.0, 0.0));
      CHECK(box_2_2_1.getHalfAxes()[0] == glm::dvec3(5.0, 0.0, 0.0));
      CHECK(box_2_2_1.getHalfAxes()[1] == glm::dvec3(0.0, 5.0, 0.0));
      CHECK(box_2_2_1.getHalfAxes()[2] == glm::dvec3(0.0, 0.0, 20.0));

      const auto& tile_2_3_1 = findTile(tileChildren, QuadtreeTileID(2, 3, 1));
      const auto& box_2_3_1 =
          std::get<OrientedBoundingBox>(tile_2_3_1.getBoundingVolume());
      CHECK(box_2_3_1.getCenter() == glm::dvec3(15.0, -5.0, 0.0));
      CHECK(box_2_3_1.getHalfAxes()[0] == glm::dvec3(5.0, 0.0, 0.0));
      CHECK(box_2_3_1.getHalfAxes()[1] == glm::dvec3(0.0, 5.0, 0.0));
      CHECK(box_2_3_1.getHalfAxes()[2] == glm::dvec3(0.0, 0.0, 20.0));
    }
  }

  SUBCASE("Subdivide bounding region tile") {
    BoundingRegion loaderBoundingVolume{
        GlobeRectangle{
            -Math::OnePi,
            -Math::PiOverTwo,
            Math::OnePi,
            Math::PiOverTwo},
        0.0,
        100.0,
        Ellipsoid::WGS84};
    ImplicitQuadtreeLoader loader{
        "tileset.json",
        "content/{level}.{x}.{y}.b3dm",
        "subtrees/{level}.{x}.{y}.json",
        5,
        5,
        loaderBoundingVolume};

    // add subtree with all available tiles
    loader.addSubtreeAvailability(
        QuadtreeTileID{0, 0, 0},
        SubtreeAvailability{
            ImplicitTileSubdivisionScheme::Quadtree,
            5,
            SubtreeAvailability::SubtreeConstantAvailability{true},
            SubtreeAvailability::SubtreeConstantAvailability{false},
            {SubtreeAvailability::SubtreeConstantAvailability{true}},
            {}});

    // check subdivide root tile first
    Tile tile(&loader);
    tile.setTileID(QuadtreeTileID(0, 0, 0));
    tile.setBoundingVolume(loaderBoundingVolume);

    {
      auto tileChildrenResult = loader.createTileChildren(tile);
      CHECK(tileChildrenResult.state == TileLoadResultState::Success);

      const auto& tileChildren = tileChildrenResult.children;
      CHECK(tileChildren.size() == 4);

      const auto& tile_1_0_0 = findTile(tileChildren, QuadtreeTileID(1, 0, 0));
      const auto& region_1_0_0 =
          std::get<BoundingRegion>(tile_1_0_0.getBoundingVolume());
      CHECK(region_1_0_0.getRectangle().getWest() == Approx(-Math::OnePi));
      CHECK(region_1_0_0.getRectangle().getSouth() == Approx(-Math::PiOverTwo));
      CHECK(region_1_0_0.getRectangle().getEast() == Approx(0.0));
      CHECK(region_1_0_0.getRectangle().getNorth() == Approx(0.0));
      CHECK(region_1_0_0.getMinimumHeight() == Approx(0.0));
      CHECK(region_1_0_0.getMaximumHeight() == Approx(100.0));

      const auto& tile_1_1_0 = findTile(tileChildren, QuadtreeTileID(1, 1, 0));
      const auto& region_1_1_0 =
          std::get<BoundingRegion>(tile_1_1_0.getBoundingVolume());
      CHECK(region_1_1_0.getRectangle().getWest() == Approx(0.0));
      CHECK(region_1_1_0.getRectangle().getSouth() == Approx(-Math::PiOverTwo));
      CHECK(region_1_1_0.getRectangle().getEast() == Approx(Math::OnePi));
      CHECK(region_1_1_0.getRectangle().getNorth() == Approx(0.0));
      CHECK(region_1_1_0.getMinimumHeight() == Approx(0.0));
      CHECK(region_1_1_0.getMaximumHeight() == Approx(100.0));

      const auto& tile_1_0_1 = findTile(tileChildren, QuadtreeTileID(1, 0, 1));
      const auto& region_1_0_1 =
          std::get<BoundingRegion>(tile_1_0_1.getBoundingVolume());
      CHECK(region_1_0_1.getRectangle().getWest() == Approx(-Math::OnePi));
      CHECK(region_1_0_1.getRectangle().getSouth() == Approx(0.0));
      CHECK(region_1_0_1.getRectangle().getEast() == Approx(0.0));
      CHECK(region_1_0_1.getRectangle().getNorth() == Approx(Math::PiOverTwo));
      CHECK(region_1_0_1.getMinimumHeight() == Approx(0.0));
      CHECK(region_1_0_1.getMaximumHeight() == Approx(100.0));

      const auto& tile_1_1_1 = findTile(tileChildren, QuadtreeTileID(1, 1, 1));
      const auto& region_1_1_1 =
          std::get<BoundingRegion>(tile_1_1_1.getBoundingVolume());
      CHECK(region_1_1_1.getRectangle().getWest() == Approx(0.0));
      CHECK(region_1_1_1.getRectangle().getSouth() == Approx(0.0));
      CHECK(region_1_1_1.getRectangle().getEast() == Approx(Math::OnePi));
      CHECK(region_1_1_1.getRectangle().getNorth() == Approx(Math::PiOverTwo));
      CHECK(region_1_1_1.getMinimumHeight() == Approx(0.0));
      CHECK(region_1_1_1.getMaximumHeight() == Approx(100.0));

      tile.createChildTiles(std::move(tileChildrenResult.children));
    }

    // check subdivide one of the root children
    {
      auto& tile_1_1_0 = tile.getChildren()[1];
      auto tileChildrenResult = loader.createTileChildren(tile_1_1_0);
      CHECK(tileChildrenResult.state == TileLoadResultState::Success);

      const auto& tileChildren = tileChildrenResult.children;
      CHECK(tileChildren.size() == 4);

      const auto& tile_2_2_0 = findTile(tileChildren, QuadtreeTileID(2, 2, 0));
      const auto& region_2_2_0 =
          std::get<BoundingRegion>(tile_2_2_0.getBoundingVolume());
      CHECK(region_2_2_0.getRectangle().getWest() == Approx(0.0));
      CHECK(region_2_2_0.getRectangle().getSouth() == Approx(-Math::PiOverTwo));
      CHECK(region_2_2_0.getRectangle().getEast() == Approx(Math::PiOverTwo));
      CHECK(
          region_2_2_0.getRectangle().getNorth() == Approx(-Math::OnePi / 4.0));
      CHECK(region_2_2_0.getMinimumHeight() == Approx(0.0));
      CHECK(region_2_2_0.getMaximumHeight() == Approx(100.0));

      const auto& tile_2_3_0 = findTile(tileChildren, QuadtreeTileID(2, 3, 0));
      const auto& region_2_3_0 =
          std::get<BoundingRegion>(tile_2_3_0.getBoundingVolume());
      CHECK(region_2_3_0.getRectangle().getWest() == Approx(Math::PiOverTwo));
      CHECK(region_2_3_0.getRectangle().getSouth() == Approx(-Math::PiOverTwo));
      CHECK(region_2_3_0.getRectangle().getEast() == Approx(Math::OnePi));
      CHECK(
          region_2_3_0.getRectangle().getNorth() == Approx(-Math::OnePi / 4.0));
      CHECK(region_2_3_0.getMinimumHeight() == Approx(0.0));
      CHECK(region_2_3_0.getMaximumHeight() == Approx(100.0));

      const auto& tile_2_2_1 = findTile(tileChildren, QuadtreeTileID(2, 2, 1));
      const auto& region_2_2_1 =
          std::get<BoundingRegion>(tile_2_2_1.getBoundingVolume());
      CHECK(region_2_2_1.getRectangle().getWest() == Approx(0.0));
      CHECK(
          region_2_2_1.getRectangle().getSouth() == Approx(-Math::OnePi / 4.0));
      CHECK(region_2_2_1.getRectangle().getEast() == Approx(Math::OnePi / 2.0));
      CHECK(region_2_2_1.getRectangle().getNorth() == Approx(0.0));
      CHECK(region_2_2_1.getMinimumHeight() == Approx(0.0));
      CHECK(region_2_2_1.getMaximumHeight() == Approx(100.0));

      const auto& tile_2_3_1 = findTile(tileChildren, QuadtreeTileID(2, 3, 1));
      const auto& region_2_3_1 =
          std::get<BoundingRegion>(tile_2_3_1.getBoundingVolume());
      CHECK(region_2_3_1.getRectangle().getWest() == Approx(Math::PiOverTwo));
      CHECK(
          region_2_3_1.getRectangle().getSouth() == Approx(-Math::OnePi / 4.0));
      CHECK(region_2_3_1.getRectangle().getEast() == Approx(Math::OnePi));
      CHECK(region_2_3_1.getRectangle().getNorth() == Approx(0.0));
      CHECK(region_2_3_1.getMinimumHeight() == Approx(0.0));
      CHECK(region_2_3_1.getMaximumHeight() == Approx(100.0));
    }
  }

  SUBCASE("Subdivide S2 volume tile") {
    S2CellID rootID = S2CellID::fromToken("1");
    CHECK(rootID.getFace() == 0);

    S2CellBoundingVolume loaderBoundingVolume{
        rootID,
        0,
        1000.0,
        Ellipsoid::WGS84};

    ImplicitQuadtreeLoader loader{
        "tileset.json",
        "content/{level}.{x}.{y}.b3dm",
        "subtrees/{level}.{x}.{y}.json",
        5,
        5,
        loaderBoundingVolume};

    // add subtree with all available tiles
    loader.addSubtreeAvailability(
        QuadtreeTileID{0, 0, 0},
        SubtreeAvailability{
            ImplicitTileSubdivisionScheme::Quadtree,
            5,
            SubtreeAvailability::SubtreeConstantAvailability{true},
            SubtreeAvailability::SubtreeConstantAvailability{false},
            {SubtreeAvailability::SubtreeConstantAvailability{true}},
            {}});

    Tile tile(&loader);
    tile.setTileID(QuadtreeTileID(0, 0, 0));
    tile.setBoundingVolume(loaderBoundingVolume);

    auto tileChildrenResult = loader.createTileChildren(tile);
    CHECK(tileChildrenResult.state == TileLoadResultState::Success);

    const auto& tileChildren = tileChildrenResult.children;
    CHECK(tileChildren.size() == 4);

    const auto& tile_1_0_0 = findTile(tileChildren, QuadtreeTileID(1, 0, 0));
    const auto& box_1_0_0 =
        std::get<S2CellBoundingVolume>(tile_1_0_0.getBoundingVolume());
    CHECK(box_1_0_0.getCellID().toToken() == "04");

    const auto& tile_1_1_0 = findTile(tileChildren, QuadtreeTileID(1, 1, 0));
    const auto& box_1_1_0 =
        std::get<S2CellBoundingVolume>(tile_1_1_0.getBoundingVolume());
    CHECK(box_1_1_0.getCellID().toToken() == "1c");

    const auto& tile_1_0_1 = findTile(tileChildren, QuadtreeTileID(1, 0, 1));
    ;
    const auto& box_1_0_1 =
        std::get<S2CellBoundingVolume>(tile_1_0_1.getBoundingVolume());
    CHECK(box_1_0_1.getCellID().toToken() == "0c");

    const auto& tile_1_1_1 = findTile(tileChildren, QuadtreeTileID(1, 1, 1));
    const auto& box_1_1_1 =
        std::get<S2CellBoundingVolume>(tile_1_1_1.getBoundingVolume());
    CHECK(box_1_1_1.getCellID().toToken() == "14");
  }

  SUBCASE("Child tiles without content initially have empty content") {
    OrientedBoundingBox loaderBoundingVolume{glm::dvec3(0.0), glm::dmat3(20.0)};
    ImplicitQuadtreeLoader loader{
        "tileset.json",
        "content/{level}.{x}.{y}.b3dm",
        "subtrees/{level}.{x}.{y}.json",
        5,
        5,
        loaderBoundingVolume};

    // add subtree with all tiles, but no content available.
    std::optional<SubtreeAvailability> maybeAvailability =
        SubtreeAvailability::createEmpty(
            ImplicitTileSubdivisionScheme::Quadtree,
            5,
            true);
    REQUIRE(maybeAvailability);
    SubtreeAvailability availability = std::move(*maybeAvailability);

    // Mark content for a tile available
    availability.setContentAvailable(
        QuadtreeTileID{0, 0, 0},
        QuadtreeTileID{1, 1, 1},
        0,
        true);
    loader.addSubtreeAvailability(
        QuadtreeTileID{0, 0, 0},
        std::move(availability));

    // check subdivide root tile first
    Tile tile(&loader);
    tile.setTileID(QuadtreeTileID(0, 0, 0));
    tile.setBoundingVolume(loaderBoundingVolume);

    auto tileChildrenResult = loader.createTileChildren(tile);
    CHECK(tileChildrenResult.state == TileLoadResultState::Success);

    REQUIRE(tileChildrenResult.children.size() == 4);

    size_t childrenWithReferencingContent = 0;

    for (Tile& child : tileChildrenResult.children) {
      if (child.hasReferencingContent()) {
        ++childrenWithReferencingContent;

        REQUIRE(child.getReferenceCount() == 1);
        child.getContent().setContentKind(TileUnknownContent());

        // In a perfect world the above might release the reference, but it
        // won't. Release it manually so we won't assert when the Tile goes out
        // of scope.
        REQUIRE(child.getReferenceCount() == 1);
        child.releaseReference();
      } else {
        REQUIRE(child.getReferenceCount() == 0);
      }
    }

    // The three tiles without content should have referencing EmptyContent.
    // The fourth should not yet have any referencing content because it has not
    // been loaded.
    CHECK(childrenWithReferencingContent == 3);
  }
}
