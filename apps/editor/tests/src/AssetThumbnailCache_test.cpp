#include "Plugins/EditorHost.h"
#include "Render/Renderer.h"
#include "Thumbnails/AssetThumbnailCache.h"
#include "util/held_open_assets.h"
#include <assetlib/AssetStore.h>
#include <assetlib/codecs.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <bgl/ISceneView.h>
#include <bgl/types/BackdropGradient.h>
#include <bgl/types/SceneDesc.h>
#include <bgpu/GpuContext.h>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_plugin_api/IThumbnailProvider.h>
#include <editor_sdk/StampedPixmapCache.h>
#include <memory>

#include "util/QtSupport.h"

#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <assetlib_structs/BMaterial.h>
#include <bgl/IGraphics.h>
#include <catch2/catch_message.hpp>
#include <condition_variable>
#include <core/file/file.h>
#include <core/glm.h>
#include <core/settings/Settings.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <gamelib/AssetManager.h>

#include "StoreAt.h"
#include <catch2/catch_test_macros.hpp>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/Thumbnail.h>
#include <ios>
#include <mutex>
#include <optional>
#include <qcolor.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qrgb.h>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using editor::test::WaitFor;

namespace
{
	// The shared asset directory doubles as a data root: apples.bmesh names its materials by paths
	// relative to it ("Authored/Materials/apples/Apple1.bmaterial"), and they are there.
	constexpr auto c_DataRoot     = "assets/Data";
	constexpr auto c_MeshPath     = "assets/Data/Derived/Meshes/apples.bmesh";
	constexpr auto c_MaterialPath = "assets/Data/Authored/Materials/apples/Apple1.bmaterial";

	// Where the renders are left for a human to look at, following bgl_tests' convention of writing a
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
		assetlib::AssetStore              store{ c_DataRoot };
		std::optional<Renderer>           renderer;
		std::optional<game::AssetManager> assets;

		// `shaderDir` is the client shader directory, for a surface the test registers itself.
		explicit Fixture(std::filesystem::path shaderDir = {})
		{
			auto ctxDesc             = bgpu::GpuContextDesc();
			ctxDesc.enableDebugLayer = true;
			ctxDesc.clientShaderDir  = std::move(shaderDir);

			renderer.emplace(ctxDesc, bgl::GraphicsOptions(), MakeSceneDesc());

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
		material.layer.alphaMode     = alphaMode;

		const std::string relative = "Authored/Materials/" + name + ".bmaterial";
		SaveAt(material, std::filesystem::path(c_DataRoot) / relative);
		return relative;
	}

	// Writes an opaque `.bmaterial` of one flat colour under the shared data root and returns its key.
	std::string
	WriteFlatMaterial(const std::string& name, const glm::vec4& colour)
	{
		auto material                = assetlib::BMaterial();
		material.name                = name;
		material.pbr.baseColorFactor = colour;
		material.pbr.metallicFactor  = 0.0f;
		material.pbr.roughnessFactor = 0.6f;

		const std::string key = "Authored/Materials/" + name + ".bmaterial";
		SaveAt(material, std::filesystem::path(c_DataRoot) / key);
		return key;
	}

	// The path the Content Explorer would ask for the asset at `key`.
	QString
	PathOf(const std::string& key)
	{
		return QString::fromStdString(std::string(c_DataRoot) + "/" + key);
	}

	// Whether the middle of a material's sphere reads blue rather than red.
	bool
	LooksBlue(const QPixmap& thumbnail)
	{
		const QColor middle =
			thumbnail.toImage().pixelColor(thumbnail.width() / 2, thumbnail.height() / 2);
		return middle.blueF() > middle.redF();
	}

	const auto c_Red  = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	const auto c_Blue = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

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

TEST_CASE("The plugin host owns headless viewport rendering", "[plugins][viewport][render]")
{
	Fixture                           fixture;
	assetlib::AssetStore              store(c_DataRoot);
	editor::plugins::EditorHost       host(store, &*fixture.renderer, &*fixture.assets, true, {});
	QPointer<editor::IEditorViewport> observed;
	{
		QWidget root;
		observed = host.CreateViewport(
			&root,
			editor::ViewportDesc().SetInitialInstances(4).SetTaaEnabled(false));
		REQUIRE(observed != nullptr);
		CHECK(observed->parentWidget() == &root);
		bool invoked = false;
		observed->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
			invoked = true;
			CHECK(&context.scene == fixture.renderer->GetScene().Get());
			CHECK(&context.assets == &*fixture.assets);
			CHECK(view.Get() != nullptr);
		});
		CHECK(invoked);
		observed->SetRenderingEnabled(false);
	}
	CHECK(observed.isNull());
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

TEST_CASE("A plugin thumbnail is resolved before the built-in renderer", "[thumbnails][plugins]")
{
	Fixture              fixture;
	assetlib::AssetStore store(c_DataRoot);
	class ImageProvider final : public editor::IThumbnailProvider
	{
	public:
		editor::Thumbnail
		Describe(const assetlib::AssetStore&, std::string_view) const override
		{
			return editor::Thumbnail(QImage(24, 24, QImage::Format_RGBA8888));
		}
	};
	const auto provider = editor::ThumbnailProviderDesc()
	                          .SetId("sample.thumbnail")
	                          .AddExtension(".bmaterial")
	                          .AddProvider<ImageProvider>();
	auto       desc     = fixture.Desc();
	desc.pluginProvider = [&](const std::string_view extension) {
		return extension == ".bmaterial" ? &provider : nullptr;
	};
	AssetThumbnailCache cache(std::move(desc));
	cache.SetStore(&store);
	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request(c_MaterialPath);

	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
	CHECK_FALSE(cache.Lookup(c_MaterialPath).isNull());
}

TEST_CASE("Changing projects drains plugin thumbnail work", "[thumbnails][plugins][lifetime]")
{
	Fixture              fixture;
	assetlib::AssetStore store(c_DataRoot);
	struct State
	{
		std::mutex              mutex;
		std::condition_variable changed;
		bool                    entered   = false;
		bool                    released  = false;
		bool                    completed = false;
		int                     calls     = 0;
	};
	class BlockingProvider final : public editor::IThumbnailProvider
	{
	public:
		explicit BlockingProvider(std::shared_ptr<State> state) : m_State(std::move(state)) {}
		editor::Thumbnail
		Describe(const assetlib::AssetStore&, std::string_view) const override
		{
			std::unique_lock lock(m_State->mutex);
			if (m_State->calls == 0)
			{
				m_State->entered = true;
				m_State->changed.notify_all();
				m_State->changed.wait(lock, [&] { return m_State->released; });
			}
			QImage image(8, 8, QImage::Format_RGBA8888);
			image.fill(m_State->calls++ == 0 ? Qt::red : Qt::blue);
			m_State->completed = true;
			return editor::Thumbnail(std::move(image));
		}

	private:
		std::shared_ptr<State> m_State;
	};
	auto       state    = std::make_shared<State>();
	const auto provider = editor::ThumbnailProviderDesc()
	                          .SetId("sample.thumbnail")
	                          .AddExtension(".bmaterial")
	                          .AddProvider<BlockingProvider>(state);
	auto       desc     = fixture.Desc();
	desc.pluginProvider = [&](const std::string_view extension) {
		return extension == ".bmaterial" ? &provider : nullptr;
	};
	AssetThumbnailCache cache(std::move(desc));
	cache.SetStore(&store);
	cache.Request(c_MaterialPath);

	std::jthread release([&] {
		std::unique_lock lock(state->mutex);
		state->changed.wait(lock, [&] { return state->entered; });
		state->released = true;
		state->changed.notify_all();
	});
	cache.SetStore(nullptr);
	cache.SetStore(&store);
	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);
	cache.Request(c_MaterialPath);

	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
	CHECK(state->completed);
	CHECK(state->calls == 2);
	CHECK(cache.Lookup(c_MaterialPath).toImage().pixelColor(0, 0) == QColor(Qt::blue));
}

TEST_CASE(
	"Invalidation regenerates unchanged plugin thumbnails with external dependencies",
	"[thumbnails][plugins]")
{
	Fixture              fixture;
	assetlib::AssetStore store(c_DataRoot);
	struct State
	{
		QColor colour{ Qt::red };
		int    calls = 0;
	};
	class ColourProvider final : public editor::IThumbnailProvider
	{
	public:
		explicit ColourProvider(std::shared_ptr<State> state) : m_State(std::move(state)) {}
		editor::Thumbnail
		Describe(const assetlib::AssetStore&, std::string_view) const override
		{
			QImage image(8, 8, QImage::Format_RGBA8888);
			image.fill(m_State->colour);
			++m_State->calls;
			return editor::Thumbnail(std::move(image));
		}

	private:
		std::shared_ptr<State> m_State;
	};
	auto       state    = std::make_shared<State>();
	const auto provider = editor::ThumbnailProviderDesc()
	                          .SetId("sample.thumbnail")
	                          .AddExtension(".bmesh")
	                          .AddProvider<ColourProvider>(state);
	auto       desc     = fixture.Desc();
	desc.pluginProvider = [&](const std::string_view extension) {
		return extension == ".bmesh" ? &provider : nullptr;
	};
	AssetThumbnailCache cache(std::move(desc));
	cache.SetStore(&store);
	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request(c_MeshPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
	CHECK(cache.Lookup(c_MeshPath).toImage().pixelColor(0, 0) == QColor(Qt::red));

	// A key the thumbnail never names: a provider describes from whatever it reads in the store, so
	// any write may have changed what it would say.
	state->colour = Qt::blue;
	cache.Invalidate("Authored/Materials/unrelated.bmaterial");
	CHECK(cache.Lookup(c_MeshPath).isNull());
	cache.Request(c_MeshPath);
	REQUIRE(WaitFor([&] { return ready.count() == 2; }));
	CHECK(state->calls == 2);
	CHECK(cache.Lookup(c_MeshPath).toImage().pixelColor(0, 0) == QColor(Qt::blue));
}

TEST_CASE("Any write retries a plugin thumbnail that failed", "[thumbnails][plugins]")
{
	Fixture              fixture;
	assetlib::AssetStore store(c_DataRoot);
	class FailOnceProvider final : public editor::IThumbnailProvider
	{
	public:
		explicit FailOnceProvider(std::shared_ptr<int> calls) : m_Calls(std::move(calls)) {}
		editor::Thumbnail
		Describe(const assetlib::AssetStore&, std::string_view) const override
		{
			if ((*m_Calls)++ == 0)
				throw std::runtime_error("what it describes from is not written yet");
			QImage image(8, 8, QImage::Format_RGBA8888);
			image.fill(Qt::green);
			return editor::Thumbnail(std::move(image));
		}

	private:
		std::shared_ptr<int> m_Calls;
	};
	auto       calls    = std::make_shared<int>(0);
	const auto provider = editor::ThumbnailProviderDesc()
	                          .SetId("sample.thumbnail")
	                          .AddExtension(".bmesh")
	                          .AddProvider<FailOnceProvider>(calls);
	auto       desc     = fixture.Desc();
	desc.pluginProvider = [&](const std::string_view extension) {
		return extension == ".bmesh" ? &provider : nullptr;
	};
	AssetThumbnailCache cache(std::move(desc));
	cache.SetStore(&store);
	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);
	QSignalSpy rejected(&cache, &StampedPixmapCache::Rejected);

	cache.Request(c_MeshPath);
	REQUIRE(WaitFor([&] { return rejected.count() == 1; }));

	// The mesh file itself is unchanged, so only the write can be what lets it through.
	cache.Invalidate("Authored/Materials/unrelated.bmaterial");
	cache.Request(c_MeshPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
	CHECK(*calls == 2);
}

TEST_CASE("A plugin scene thumbnail releases its preview geometry", "[thumbnails][plugins][render]")
{
	Fixture              fixture;
	assetlib::AssetStore store(c_DataRoot);
	class SceneProvider final : public editor::IThumbnailProvider
	{
	public:
		editor::Thumbnail
		Describe(const assetlib::AssetStore&, std::string_view key) const override
		{
			return editor::Thumbnail(
				editor::ThumbnailScene{
					.geometry = editor::ThumbnailPrimitive::kSphere,
					.material = std::string(key),
				});
		}
	};
	const auto provider = editor::ThumbnailProviderDesc()
	                          .SetId("sample.thumbnail")
	                          .AddExtension(".bmaterial")
	                          .AddProvider<SceneProvider>();
	auto       desc     = fixture.Desc();
	desc.pluginProvider = [&](const std::string_view extension) {
		return extension == ".bmaterial" ? &provider : nullptr;
	};
	AssetThumbnailCache cache(std::move(desc));
	cache.SetStore(&store);
	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request(c_MaterialPath);

	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
	CHECK_FALSE(cache.Lookup(c_MaterialPath).isNull());
}

TEST_CASE("A .bmesh renders to a thumbnail wearing its own materials", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());

	cache.SetStore(&fixture.store);

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

	cache.SetStore(&fixture.store);

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

	cache.SetStore(&fixture.store);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request(c_MaterialPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));

	const QPixmap thumbnail = cache.Lookup(c_MaterialPath);
	REQUIRE(!thumbnail.isNull());

	const QImage image = thumbnail.toImage();
	REQUIRE(image.save(c_MaterialGot));
	REQUIRE(DistinctColours(image) > 1);
}

namespace
{
	// A toon character surface drawing its colour flat, so the sphere is one tone and the corners
	// are background.
	constexpr std::string_view c_FlatToon = R"(import bgl.MaterialReader;
import bgl.ToonCharacterSurface;

struct FlatToonParams
{
    [Color]
    [Default(0.8, 0.35, 0.1, 1.0)]
    float4 baseColorFactor;
};

struct FlatToon : IToonCharacterSurfaceSource
{
    typealias MaterialParams = FlatToonParams;

    static float Coverage<R : IMaterialReader>(R reader, FlatToonParams params) { return 1.0; }

    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, FlatToonParams params)
    {
        ToonCharacterSurface surface = ToonCharacterSurface();
        surface.baseColor = params.baseColorFactor;
        return surface;
    }
};
)";

	float
	SrgbEncode(float linear)
	{
		return linear <= 0.0031308f ? 12.92f * linear :
		                              1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}

	// The mean colour of a 4x4 box, each channel in [0,1].
	glm::vec3
	MeanColour(const QImage& image, int x, int y)
	{
		auto sum = glm::vec3(0.0f);
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 4; ++dx)
			{
				const QColor c = image.pixelColor(x + dx, y + dy);
				sum += glm::vec3(c.redF(), c.greenF(), c.blueF());
			}
		return sum / 16.0f;
	}
}

// A toon asset's thumbnail stands against the toon backdrop, as its preview does, and anything else
// against the sky: the background follows what is shown. Read at the corners, which the sphere does
// not reach -- through the toon post-process the gradient lands there as its own sRGB encoding.
TEST_CASE(
	"A toon material thumbnails against the toon backdrop, and a PBR one against the sky",
	"[thumbnails][backdrop][render]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const std::filesystem::path root = std::filesystem::path(temp.path().toStdWString());

	const std::filesystem::path shaders = root / "shaders";
	std::filesystem::create_directories(shaders);
	std::ofstream(shaders / "FlatToon.slang", std::ios::binary) << c_FlatToon;
	std::filesystem::create_directories(root / "Data");

	auto toon                = assetlib::BMaterial();
	toon.name                = "toon";
	toon.shadingModel        = assetlib::ShadingModel::kToonCharacterSurface;
	toon.surface.surfaceName = "FlatToon";
	toon.surface.values      = { { "baseColorFactor", { 0.8f, 0.35f, 0.1f, 1.0f } } };
	auto toonStore           = assetlib::AssetStore(root / "Data");
	toonStore.Save(toon, "Authored/Materials/toon.bmaterial");
	const QString toonPath =
		QString::fromStdString((root / "Data/Authored/Materials/toon.bmaterial").string());

	Fixture fixture(shaders);

	auto desc = fixture.Desc();
	REQUIRE(desc.toonBackdrop == bgl::BackdropGradient());
	AssetThumbnailCache cache(desc);
	REQUIRE(cache.IsReady());

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.SetStore(&toonStore);
	cache.Request(toonPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
	const QImage toonImage = cache.Lookup(toonPath).toImage();
	REQUIRE(!toonImage.isNull());

	// The corner rows sit within 2% of the frame's edges, so the gradient there is its end colour to
	// well inside this margin.
	constexpr float c_Margin = 0.03f;
	const int       last     = toonImage.height() - 4;
	const glm::vec3 top      = MeanColour(toonImage, 0, 0);
	const glm::vec3 bottom   = MeanColour(toonImage, 0, last);
	const auto      gradient = bgl::BackdropGradient();
	for (int i = 0; i < 3; ++i)
	{
		INFO("channel " << i);
		CHECK(top[i] == Catch::Approx(SrgbEncode(gradient.top[i])).margin(c_Margin));
		CHECK(bottom[i] == Catch::Approx(SrgbEncode(gradient.bottom[i])).margin(c_Margin));
	}

	cache.SetStore(&fixture.store);
	cache.Request(c_MaterialPath);
	REQUIRE(WaitFor([&] { return ready.count() == 2; }));
	const QImage pbrImage = cache.Lookup(c_MaterialPath).toImage();
	REQUIRE(!pbrImage.isNull());

	const glm::vec3 pbrTop = MeanColour(pbrImage, 0, 0);
	CHECK(glm::abs(pbrTop.b - SrgbEncode(gradient.top.b)) > 0.1f);
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
	cache.SetStore(&fixture.store);

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
		cache.SetStore(&fixture.store);

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
	cache.SetStore(&fixture.store);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	// No pumping in between: the read is still on the worker when the project closes.
	cache.Request(hashedFile);
	cache.SetStore(nullptr);

	REQUIRE(!WaitFor([&] { return ready.count() > 0; }, 1000));

	// And the cache still renders once a project is back, so the cancel released everything the
	// next shot needs.
	cache.SetStore(&fixture.store);
	cache.Request(c_MeshPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
}

TEST_CASE("A material cannot be drawn without a project store", "[thumbnails][render]")
{
	Fixture fixture;

	// A `.bmaterial` is nothing but references relative to the data root, and the store is the only
	// thing that resolves them. Without one -- no project open -- there is nothing to draw.
	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request(c_MaterialPath);
	REQUIRE(!WaitFor([&] { return ready.count() > 0; }, 1000));

	// And it draws once one arrives, so it was the store that was missing and nothing else.
	cache.SetStore(&fixture.store);
	cache.Request(c_MaterialPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
}

TEST_CASE("A second request for an unchanged asset does not re-render", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetStore(&fixture.store);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);

	cache.Request(c_MeshPath);
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));

	// A hit on a current entry is served from the cache: rendering is a GPU stall, and browsing a
	// folder repaints its tiles constantly.
	cache.Request(c_MeshPath);
	REQUIRE(!WaitFor([&] { return ready.count() == 2; }, 500));
}

TEST_CASE(
	"A write refreshes that asset's thumbnail and keeps every other one",
	"[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetStore(&fixture.store);

	const std::string written = WriteFlatMaterial("refresh_written", c_Red);
	const std::string kept    = WriteFlatMaterial("refresh_kept", c_Red);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);
	cache.Request(PathOf(written));
	cache.Request(PathOf(kept));
	REQUIRE(WaitFor([&] { return ready.count() == 2; }));

	cache.Invalidate(written);

	CHECK(cache.Lookup(PathOf(written)).isNull());
	CHECK_FALSE(cache.Lookup(PathOf(kept)).isNull());

	// Served as it was, with no second read or render behind it.
	cache.Request(PathOf(kept));
	CHECK_FALSE(WaitFor([&] { return ready.count() == 3; }, 500));
}

TEST_CASE("A material write refreshes the meshes drawn wearing it", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetStore(&fixture.store);

	const std::string kept = WriteFlatMaterial("wearer_kept", c_Red);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);
	cache.Request(c_MeshPath);
	cache.Request(PathOf(kept));
	REQUIRE(WaitFor([&] { return ready.count() == 2; }));

	// apples.bmesh binds this material; the mesh file itself is untouched.
	cache.Invalidate("Authored/Materials/apples/Apple1.bmaterial");

	CHECK(cache.Lookup(c_MeshPath).isNull());
	CHECK_FALSE(cache.Lookup(PathOf(kept)).isNull());
}

TEST_CASE(
	"A write landing while a thumbnail is produced stores the new look, not the one read before it",
	"[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetStore(&fixture.store);

	const std::string key = WriteFlatMaterial("midflight", c_Red);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);
	cache.Request(PathOf(key));

	// The read's result is queued to this thread, which has not turned yet, so the write always
	// lands after the claim and before anything is stored.
	WriteFlatMaterial("midflight", c_Blue);
	cache.Invalidate(key);

	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
	CHECK(LooksBlue(cache.Lookup(PathOf(key))));
}

TEST_CASE(
	"A rewritten material draws its new look, not the upload the cache already made",
	"[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetStore(&fixture.store);

	const std::string key = WriteFlatMaterial("reupload", c_Red);

	QSignalSpy ready(&cache, &StampedPixmapCache::Ready);
	cache.Request(PathOf(key));
	REQUIRE(WaitFor([&] { return ready.count() == 1; }));
	REQUIRE_FALSE(LooksBlue(cache.Lookup(PathOf(key))));

	WriteFlatMaterial("reupload", c_Blue);
	cache.Invalidate(key);
	cache.Request(PathOf(key));

	REQUIRE(WaitFor([&] { return ready.count() == 2; }));
	CHECK(LooksBlue(cache.Lookup(PathOf(key))));
}

TEST_CASE("An asset that cannot be read yields no thumbnail", "[thumbnails][render]")
{
	Fixture fixture;

	AssetThumbnailCache cache(fixture.Desc());
	REQUIRE(cache.IsReady());
	cache.SetStore(&fixture.store);

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
	cache.SetStore(&fixture.store);

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

	cache.SetStore(nullptr);
	cache.Request(c_MeshPath);
	REQUIRE(cache.Lookup(c_MeshPath).isNull());
}
