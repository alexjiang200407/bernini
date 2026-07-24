#include "Render/Renderer.h"
#include "Thumbnails/AssetThumbnailCache.h"
#include "util/QtSupport.h"

#include <QImage>
#include <QSignalSpy>

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <gamelib/AssetManager.h>

#include <catch2/catch_test_macros.hpp>

using editor::test::WaitFor;

namespace
{
	// The shared asset directory doubles as a data root: apples.bmesh names its materials by paths
	// relative to it ("Materials/apples/Apple1.bmaterial"), and they are there.
	constexpr auto c_DataRoot     = "assets";
	constexpr auto c_MeshPath     = "assets/Meshes/apples.bmesh";
	constexpr auto c_MaterialPath = "assets/Materials/apples/Apple1.bmaterial";

	// Where the renders are left for a human to look at, following bgl_tests' convention of writing a
	// `.got.png` beside the goldens.
	constexpr auto c_MeshGot     = "assets/golden/thumbnail_mesh.got.png";
	constexpr auto c_MaterialGot = "assets/golden/thumbnail_material.got.png";

	// The scene budget the cache renders against -- small, since a thumbnail holds one mesh at a time.
	bgl::SceneDesc
	MakeSceneDesc()
	{
		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxGeom                 = 32;
		sceneDesc.maxMeshlets             = 8192;
		sceneDesc.maxSubmeshes            = 64;
		sceneDesc.maxVertexBufferByteSize = 8'000'000;
		sceneDesc.maxIndices              = 500'000;
		sceneDesc.maxPbrMaterials         = 32;
		sceneDesc.maxLoosePbrMaterials    = 32;
		return sceneDesc;
	}

	// Everything a cache needs to actually render: a renderer (which owns the device and scene, as
	// MainWindow's does) and the editor's shared asset manager over that scene.
	struct Fixture
	{
		std::optional<Renderer>           renderer;
		std::optional<game::AssetManager> assets;

		Fixture()
		{
			auto opts             = bgl::GraphicsOptions();
			opts.enableDebugLayer = true;

			renderer.emplace(opts, MakeSceneDesc());

			// The editor shares one manager over its one scene, so a material loaded twice is one
			// upload and one reference count however it is drawn.
			assets.emplace(renderer->GetScene(), std::filesystem::path(c_DataRoot));
		}

		[[nodiscard]] AssetThumbnailDesc
		Desc()
		{
			auto desc       = AssetThumbnailDesc();
			desc.renderer   = &*renderer;
			desc.skybox     = "assets/skybox.ktx2";
			desc.irradiance = "assets/iem.ktx2";
			desc.prefilter  = "assets/pmrem.ktx2";
			desc.brdfLut    = "assets/brdf_lut.ktx2";
			return desc;
		}
	};

	// How many distinct colours an image holds, capped -- a render that produced nothing (a cleared
	// buffer, geometry that never made it into the scene) is one flat colour.
	int
	DistinctColours(const QImage& image)
	{
		auto seen = std::set<QRgb>();
		for (int y = 0; y < image.height() && seen.size() < 64; ++y)
		{
			for (int x = 0; x < image.width() && seen.size() < 64; ++x)
				seen.insert(image.pixel(x, y));
		}
		return static_cast<int>(seen.size());
	}
}

TEST_CASE("Only the assets the editor can draw are thumbnailed", "[thumbnails]")
{
	REQUIRE(AssetThumbnailCache::CanThumbnail("Meshes/tree.bmesh"));
	REQUIRE(AssetThumbnailCache::CanThumbnail("Materials/bark.bmaterial"));

	// The suffix decides, case-insensitively -- a file's name has nothing to do with what it is.
	REQUIRE(AssetThumbnailCache::CanThumbnail("Meshes/TREE.BMESH"));

	// A texture already has TexturePreviewCache, and nothing else is drawable at all.
	REQUIRE(!AssetThumbnailCache::CanThumbnail("Textures/bark.ktx2"));
	REQUIRE(!AssetThumbnailCache::CanThumbnail("Levels/main.blevel"));
	REQUIRE(!AssetThumbnailCache::CanThumbnail("Meshes"));
}

TEST_CASE("A .bmesh renders to a thumbnail wearing its own materials", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());

	cache.SetAssets(&*fixture.assets);

	// Nothing has been asked for yet, so nothing is cached.
	REQUIRE(cache.Lookup(c_MeshPath).isNull());

	QSignalSpy ready(&cache, &AssetThumbnailCache::ThumbnailReady);

	cache.Request(c_MeshPath);

	// The read happens on a worker and the render on the next turn of the event loop, so neither has
	// landed by the time Request returns.
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));

	const QPixmap thumbnail = cache.Lookup(c_MeshPath);
	REQUIRE(!thumbnail.isNull());
	REQUIRE(thumbnail.width() == static_cast<int>(cache.Dimension()));
	REQUIRE(thumbnail.height() == static_cast<int>(cache.Dimension()));

	const QImage image = thumbnail.toImage();
	REQUIRE(image.save(c_MeshGot));

	// The mesh actually drew. A blank or cleared target would be a single colour.
	REQUIRE(DistinctColours(image) > 1);
}

TEST_CASE("A .bmaterial renders to a thumbnail on a sphere", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());

	cache.SetAssets(&*fixture.assets);

	QSignalSpy ready(&cache, &AssetThumbnailCache::ThumbnailReady);

	cache.Request(c_MaterialPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));

	const QPixmap thumbnail = cache.Lookup(c_MaterialPath);
	REQUIRE(!thumbnail.isNull());

	const QImage image = thumbnail.toImage();
	REQUIRE(image.save(c_MaterialGot));
	REQUIRE(DistinctColours(image) > 1);
}

TEST_CASE("A material cannot be drawn without an asset manager", "[thumbnails][render]")
{
	Fixture fixture;

	// A `.bmaterial` is nothing but references relative to the data root, and the manager is the only
	// thing that resolves them. Without one -- no project open -- there is nothing to draw.
	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());

	QSignalSpy ready(&cache, &AssetThumbnailCache::ThumbnailReady);

	cache.Request(c_MaterialPath);
	REQUIRE(!WaitFor([&] { return ready.count() > 0; }, 1000));

	// And it draws once one arrives, so it was the manager that was missing and nothing else.
	cache.SetAssets(&*fixture.assets);
	cache.Request(c_MaterialPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
}

TEST_CASE("A second request for an unchanged asset does not re-render", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetAssets(&*fixture.assets);

	QSignalSpy ready(&cache, &AssetThumbnailCache::ThumbnailReady);

	cache.Request(c_MeshPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));

	// A hit on a current entry is served from the cache: rendering is a GPU stall, and browsing a
	// folder repaints its tiles constantly.
	cache.Request(c_MeshPath);
	REQUIRE(!WaitFor([&] { return ready.count() == 2; }, 500));
}

TEST_CASE("An asset that cannot be read yields no thumbnail", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetAssets(&*fixture.assets);

	QSignalSpy ready(&cache, &AssetThumbnailCache::ThumbnailReady);

	cache.Request("assets/Meshes/does_not_exist.bmesh");

	REQUIRE(!WaitFor([&] { return ready.count() > 0; }, 2000));
	REQUIRE(cache.Lookup("assets/Meshes/does_not_exist.bmesh").isNull());
}

TEST_CASE("Without a graphics device the cache stays inert", "[thumbnails]")
{
	// The editor is constructed without a device in most of this suite; a cache that threw or crashed
	// there would take every other test with it.
	AssetThumbnailCache cache(AssetThumbnailDesc{});

	REQUIRE(!cache.IsReady());

	cache.SetAssets(nullptr);
	cache.Request(c_MeshPath);
	REQUIRE(cache.Lookup(c_MeshPath).isNull());
}
