#include "Render/Renderer.h"
#include "Thumbnails/AssetThumbnailCache.h"
#include "util/held_open_assets.h"
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Node.h>

#include "util/QtSupport.h"

#include <QImage>
#include <QSignalSpy>

#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/magic.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <core/file/file.h>
#include <core/settings/Settings.h>
#include <gamelib/AssetManager.h>

#include "StoreAt.h"
#include <catch2/catch_test_macros.hpp>

using editor::test::WaitFor;

namespace
{
	// The shared asset directory doubles as a data root: apples.bmesh names its materials by paths
	// relative to it ("Authored/Materials/apples/Apple1.bmaterial"), and they are there.
	constexpr auto c_DataRoot     = "assets/Data";
	constexpr auto c_MeshPath     = "assets/Data/Derived/Meshes/apples.bmesh";
	constexpr auto c_MaterialPath = "assets/Data/Authored/Materials/apples/Apple1.bmaterial";

	// Where the renders are left for a human to look at, following bgl_extended_tests' convention of writing a
	// `.got.png` beside the goldens.
	constexpr auto c_MeshGot     = "assets/golden/thumbnail_mesh.got.png";
	constexpr auto c_MaterialGot = "assets/golden/thumbnail_material.got.png";

	// The scene budget the cache renders against -- small, since a thumbnail holds one mesh at a time.
	bgl::SceneDesc
	MakeSceneDesc()
	{
		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 32;
		sceneDesc.initialMeshlets             = 8192;
		sceneDesc.initialSubmeshes            = 64;
		sceneDesc.initialVertexBufferByteSize = 8'000'000;
		sceneDesc.initialIndices              = 500'000;
		sceneDesc.initialPbrMaterials         = 32;
		sceneDesc.initialLoosePbrMaterials    = 32;
		return sceneDesc;
	}

	// The lighting the editor itself renders thumbnails with, rather than a second copy of those paths
	// here: config.json is deployed beside the test binary as it is beside editor.exe, and a config
	// naming a different environment has to exercise that one.
	[[nodiscard]] const core::Settings&
	EditorConfig()
	{
		static auto g_Settings =
			core::Settings(core::file::get_executable_path().parent_path() / "config.json");
		return g_Settings;
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

		// ~AssetManager hands every asset it still holds back to the scene, and a scene is driven by
		// one thread -- the render thread. MainWindow tears its manager down the same way.
		~Fixture()
		{
			renderer->Invoke([&] { assets.reset(); });
		}

		[[nodiscard]] AssetThumbnailDesc
		Desc()
		{
			auto thumbSettings      = EditorConfig()["thumbnails"];
			auto desc               = AssetThumbnailDesc();
			desc.renderer           = &*renderer;
			desc.env.environmentMap = thumbSettings["environmentMap"].GetOrDefault(std::string());
			REQUIRE_FALSE(desc.env.environmentMap.empty());

			// Read from the same config the editor uses. It is no longer derived from the `.benv`'s
			// path, so a caller that does not say goes unlit -- which is what this asserts against.
			desc.env.dataRoot = thumbSettings["dataRoot"].GetOrDefault(std::string());
			REQUIRE_FALSE(desc.env.dataRoot.empty());

			return desc;
		}
	};

	// Mean squared difference between horizontally adjacent pixels: how grainy the image is. A
	// stochastic material that the accumulation has not resolved is speckle, and speckle is precisely
	// a large difference between neighbours. `DistinctColours` cannot see it -- noise passes that with
	// room to spare.
	double
	Grain(const QImage& image)
	{
		double sum   = 0.0;
		size_t count = 0;

		for (int y = 0; y < image.height(); ++y)
		{
			for (int x = 0; x + 1 < image.width(); ++x)
			{
				const QColor a = image.pixelColor(x, y);
				const QColor b = image.pixelColor(x + 1, y);

				for (const double d :
				     { a.redF() - b.redF(), a.greenF() - b.greenF(), a.blueF() - b.blueF() })
				{
					sum += d * d;
					++count;
				}
			}
		}

		return count > 0 ? sum / static_cast<double>(count) : 0.0;
	}

	// Writes a `.bmaterial` under the shared data root and returns its path relative to it. The alpha
	// lives in the factor rather than in a texture, so the material needs no companion files: hashed
	// alpha reads `baseColor.a` whatever produced it.
	std::string
	WriteMaterial(const std::string& name, assetlib::AlphaMode alphaMode, float alpha)
	{
		auto material                = assetlib::BMaterial();
		material.name                = name;
		material.pbr.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, alpha);
		material.pbr.metallicFactor  = 0.0f;
		material.pbr.roughnessFactor = 0.6f;
		material.pbr.alphaMode       = alphaMode;

		const std::string relative = "Authored/Materials/" + name + ".bmaterial";
		SaveAt(material, std::filesystem::path(c_DataRoot) / relative);
		return relative;
	}

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

// The end of the wiring the Content Explorer's deletion guard depends on: a real holder, parented
// into a tree nobody registered it in, answering with the `.benv` it is actually lit by. Nothing on
// disk references a `.benv`, so this answer is the only thing that refuses its deletion.
TEST_CASE("A live environment is held by the cache lit from it", "[thumbnails][render]")
{
	Fixture       fixture;
	const QString environment = QString::fromStdString(fixture.Desc().env.environmentMap);

	QObject root;
	auto*   cache = new AssetThumbnailCache(fixture.Desc(), &root);

	REQUIRE(cache->IsReady());
	CHECK(cache->GetHeldOpenPaths() == QStringList{ environment });
	CHECK(editor::GetAssetsHeldOpen(&root) == QStringList{ environment });
}

TEST_CASE("Only the assets the editor can draw are thumbnailed", "[thumbnails]")
{
	REQUIRE(AssetThumbnailCache::CanThumbnail("Derived/Meshes/tree.bmesh"));
	REQUIRE(AssetThumbnailCache::CanThumbnail("Authored/Materials/bark.bmaterial"));

	// The suffix decides, case-insensitively -- a file's name has nothing to do with what it is.
	REQUIRE(AssetThumbnailCache::CanThumbnail("Derived/Meshes/TREE.BMESH"));

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

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

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

TEST_CASE(
	"A repaint while a thumbnail renders does not start a second render",
	"[thumbnails][render]")
{
	// The end-to-end half of what StampedPixmapCache_test pins directly. A render takes several turns
	// of the event loop -- a read on a worker, a draw, then a readback resolved on a later turn -- and
	// the grid repaints throughout, looking the tile up and asking again on every miss.
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());

	cache.SetAssets(&*fixture.assets);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request(c_MeshPath);

	// Exactly what AssetFileModel::data does on every paint: look it up, and ask again on a miss.
	REQUIRE(WaitFor([&] {
		if (cache.Lookup(c_MeshPath).isNull())
			cache.Request(c_MeshPath);
		return ready.count() >= 1;
	}));

	// A duplicate would be a whole second read and render, so give it room to surface. This bounds
	// how much of the duplicate it can catch, not whether the claim rule holds -- that is the unit
	// test's job, and it needs no clock.
	static_cast<void>(WaitFor([&] { return ready.count() >= 2; }, 3000));

	REQUIRE(ready.count() == 1);
}

TEST_CASE("A .bmaterial renders to a thumbnail on a sphere", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());

	cache.SetAssets(&*fixture.assets);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request(c_MaterialPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));

	const QPixmap thumbnail = cache.Lookup(c_MaterialPath);
	REQUIRE(!thumbnail.isNull());

	const QImage image = thumbnail.toImage();
	REQUIRE(image.save(c_MaterialGot));
	REQUIRE(DistinctColours(image) > 1);
}

// What makes a stochastic material safe to thumbnail, and the gate on the cache's reroute: it
// renders one frame, where hashed coverage cannot accumulate into a surface, so its private
// manager loads a hashed material as the blend it converges to (hashedAsBlend). This is the test
// that fails if that reroute is lost.
//
// A hashed material's coverage is a per-pixel random decision. One frame of it is a speckle
// pattern, not a picture of the material, and every assertion the other thumbnail tests make -- it
// rendered, it has more than one colour -- passes on speckle. Grain is what tells them apart, and
// the same material at kOpaque is the reference for how smooth a resolved sphere is: it is the
// same geometry under the same light, differing only in the coverage decision.
TEST_CASE("A hashed material thumbnails as a surface rather than as noise", "[thumbnails][render]")
{
	Fixture fixture;

	const std::string opaquePath =
		WriteMaterial("thumb_opaque", assetlib::AlphaMode::kOpaque, 1.0f);
	const std::string hashedPath =
		WriteMaterial("thumb_hashed", assetlib::AlphaMode::kHashed, 0.5f);

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetAssets(&*fixture.assets);

	const auto thumbnailOf = [&](const std::string& relative) {
		QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

		const std::string path = std::string(c_DataRoot) + "/" + relative;
		cache.Request(QString::fromStdString(path));
		REQUIRE(WaitFor([&] { return ready.count() == 1; }));

		const QPixmap thumbnail = cache.Lookup(QString::fromStdString(path));
		REQUIRE(!thumbnail.isNull());
		return thumbnail.toImage();
	};

	const QImage opaque = thumbnailOf(opaquePath);
	const QImage hashed = thumbnailOf(hashedPath);

	REQUIRE(opaque.save("assets/golden/thumbnail_opaque.got.png"));
	REQUIRE(hashed.save("assets/golden/thumbnail_hashed.got.png"));

	const double opaqueGrain = Grain(opaque);
	const double hashedGrain = Grain(hashed);

	INFO("thumbnail grain: opaque = " << opaqueGrain << ", hashed = " << hashedGrain);

	// Both drew something with an edge in it, or "smooth" would be satisfied by an empty frame.
	REQUIRE(DistinctColours(opaque) > 1);
	REQUIRE(DistinctColours(hashed) > 1);
	REQUIRE(opaqueGrain > 1e-5);

	// The margin covers what hashed alpha legitimately costs -- it thins the silhouette, and a
	// half-covered sphere edge is a busier edge -- without leaving room for an unresolved pattern,
	// which measures orders of magnitude above this rather than a factor of three.
	CHECK(hashedGrain < opaqueGrain * 3.0);
}

TEST_CASE("Tearing the cache down mid-batch leaves the renderer alive", "[thumbnails][render]")
{
	// A shot advances inside the renderer's frame loop over many ticks, so teardown routinely
	// interrupts a batch with one shot mid-warmup and more queued. It must detach from the loop and
	// hand back what the shot placed in the scene, or the render thread is left ticking a dead
	// object.
	Fixture fixture;

	auto paths = std::vector<QString>();
	for (int i = 0; i < 3; ++i)
	{
		const std::string relative = WriteMaterial(
			"thumb_teardown_" + std::to_string(i),
			assetlib::AlphaMode::kHashed,
			0.5f);
		paths.push_back(QString::fromStdString(std::string(c_DataRoot) + "/" + relative));
	}

	{
		AssetThumbnailCache cache(fixture.Desc());
		REQUIRE(cache.IsReady());
		cache.SetAssets(&*fixture.assets);

		for (const QString& path : paths) cache.Request(path);

		// Pump long enough for the batch to reach the render thread, so the teardown interrupts
		// warmups in progress rather than an empty queue.
		static_cast<void>(WaitFor([] { return false; }, 50));
	}

	// The renderer outlives the cache and must still be serving closures.
	REQUIRE(fixture.renderer->Invoke([] { return 42; }) == 42);
}

TEST_CASE("A read finishing after its project closed does not land", "[thumbnails][render]")
{
	// The worker's read always outlives Request's turn of the event loop, so a project switch can
	// always slip in between -- and the read then resolves against a data root that is gone. Its
	// render must fail quietly rather than cache an image under the closed project's path.
	Fixture fixture;

	const std::string hashedPath =
		WriteMaterial("thumb_switch", assetlib::AlphaMode::kHashed, 0.5f);
	const QString hashedFile = QString::fromStdString(std::string(c_DataRoot) + "/" + hashedPath);

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetAssets(&*fixture.assets);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	// No pumping in between: the read is still on the worker when the project closes.
	cache.Request(hashedFile);
	cache.SetAssets(nullptr);

	REQUIRE(!WaitFor([&] { return ready.count() > 0; }, 1000));

	// And the cache still renders once a project is back, so the cancel released everything the
	// next shot needs.
	cache.SetAssets(&*fixture.assets);
	cache.Request(c_MeshPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
}

TEST_CASE("A material cannot be drawn without an asset manager", "[thumbnails][render]")
{
	Fixture fixture;

	// A `.bmaterial` is nothing but references relative to the data root, and the manager is the only
	// thing that resolves them. Without one -- no project open -- there is nothing to draw.
	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

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

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

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

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request("assets/Data/Derived/Meshes/does_not_exist.bmesh");

	REQUIRE(!WaitFor([&] { return ready.count() > 0; }, 2000));
	REQUIRE(cache.Lookup("assets/Data/Derived/Meshes/does_not_exist.bmesh").isNull());
}

TEST_CASE("An asset that cannot be read says why", "[thumbnails][render]")
{
	// A container written at another bake revision: the reader refuses it with a message that
	// names the reason, and the tile gets that message rather than a shell icon and silence.
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetAssets(&*fixture.assets);

	const QString path = "assets/Data/Derived/Meshes/foreign_token_test.bmesh";
	{
		assetlib::BMesh mesh;
		assetlib::Node  root{};
		root.parent = root.firstChild = root.nextSibling = assetlib::c_InvalidIndex;
		root.mesh                                        = assetlib::c_InvalidIndex;
		mesh.nodes                                       = { root };
		mesh.meshes                                      = { assetlib::Mesh{ 0, 0, 0 } };

		auto bytes = assetlib::AssetCodec<assetlib::BMesh>::Serialize(mesh);
		bytes[8] ^= std::byte{ 1 };  // the bake token: bytes 8..16 of the frozen header
		std::ofstream out(path.toStdString(), std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	QSignalSpy rejected(&cache, &StampedPixmapCache::Rejected);
	cache.Request(path);
	REQUIRE(WaitFor([&] { return rejected.count() == 1; }, 5000));

	const QString reason = rejected.at(0).at(1).toString();
	CHECK(reason.contains("another bake revision"));
	CHECK(cache.GetRejection(path) == reason);
	CHECK(cache.Lookup(path).isNull());

	std::filesystem::remove(path.toStdString());
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
