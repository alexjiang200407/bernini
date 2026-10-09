#include "Plugins/EditorHost.h"
#include "Render/Renderer.h"
#include "Windows/MeshEditor/TextureUploads.h"
#include "util/QtSupport.h"  // IWYU pragma: keep

#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include <assetlib/AssetStore.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/IGraphics.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/TextureAssetHandle.h>
#include <bgpu/GpuContext.h>
#include <catch2/catch_test_macros.hpp>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <editor_plugin_api/IEditorViewport.h>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <optional>
#include <string_view>

using editor::test::WaitFor;

namespace
{
	namespace fs = std::filesystem;

	constexpr std::string_view c_Key = "Derived/SourceTextures/flat.ktx2";

	bgl::SceneDesc
	MakeSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 2;
		desc.initialSubmeshes            = 2;
		desc.initialMeshlets             = 8;
		desc.initialVertexBufferByteSize = 4096;
		desc.initialIndices              = 128;
		desc.initialPbrMaterials         = 4;
		desc.initialLoosePbrMaterials    = 4;
		return desc;
	}

	/** A project with one map in it, a device, and the host the Mesh Editor is given. */
	struct Fixture
	{
		QTemporaryDir                       temp;
		fs::path                            dataRoot = fs::path(temp.path().toStdString()) / "Data";
		std::optional<assetlib::AssetStore> store;
		std::optional<Renderer>             renderer;
		std::optional<game::AssetManager>   assets;
		std::optional<editor::plugins::EditorHost> host;

		Fixture()
		{
			fs::create_directories(dataRoot / fs::path(c_Key).parent_path());

			auto image     = assetlib::ImageData();
			image.width    = 4;
			image.height   = 4;
			image.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
			image.pixels   = core::fixed_buffer<std::byte>(4 * 4 * 4);
			image.subresources.push_back({ 0, 16, 64 });
			assetlib::writeKTX2(image, dataRoot / c_Key, false, assetlib::Ktx2Compression::kNone);

			store.emplace(dataRoot);
			renderer.emplace(bgpu::GpuContextDesc(), bgl::GraphicsOptions(), MakeSceneDesc());
			assets.emplace(renderer->GetScene(), dataRoot);
			host.emplace(*store, &*renderer, &*assets, true, editor::plugins::EditorHostDispatch());
		}

		~Fixture()
		{
			renderer->Invoke([&] { assets.reset(); });
		}

		[[nodiscard]] QString
		Path() const
		{
			return QString::fromStdWString((dataRoot / c_Key).wstring());
		}

		// Whether the shared manager still holds an upload of the map: an empty prefetch shares one
		// it holds, and answers a null handle for one it does not, rather than reading the file.
		[[nodiscard]] bool
		ManagerHoldsTheMap()
		{
			bool held = false;
			host->InvokeRender([&](editor::RenderContext& context) {
				auto       nothing = game::TexturePrefetch();
				const auto handle  = context.assets.AcquireTexture(c_Key, &nothing);
				held               = static_cast<bool>(handle.textureSlot);
				if (held)
					context.assets.ReleaseTexture(handle);
			});
			return held;
		}
	};
}

TEST_CASE(
	"A texture arrives after it is asked for, and every node naming it shares one upload",
	"[textureuploads][render]")
{
	Fixture        fixture;
	TextureUploads uploads(*fixture.host);
	QSignalSpy     settled(&uploads, &TextureUploads::Settled);

	uploads.Acquire(fixture.Path());
	uploads.Acquire(fixture.Path());

	// Nothing has been waited for: the decode is a worker's, and only the event loop delivers it.
	CHECK(uploads.IsLoading(fixture.Path()));
	CHECK_FALSE(uploads.Handle(fixture.Path()).textureSlot);

	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
	CHECK(uploads.Handle(fixture.Path()).textureSlot);
	CHECK(settled.count() == 1);

	uploads.Release(fixture.Path());
	uploads.Release(fixture.Path());
	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
}

TEST_CASE("The last node to let go of a texture releases its upload", "[textureuploads][render]")
{
	Fixture        fixture;
	TextureUploads uploads(*fixture.host);

	uploads.Acquire(fixture.Path());
	uploads.Acquire(fixture.Path());
	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
	REQUIRE(fixture.ManagerHoldsTheMap());

	uploads.Release(fixture.Path());
	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
	CHECK(fixture.ManagerHoldsTheMap());

	uploads.Release(fixture.Path());
	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));

	// Retired, not released: a material compiled from it is drawn until the panel recompiles.
	CHECK(fixture.ManagerHoldsTheMap());

	uploads.ReleaseRetired();
	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
	CHECK_FALSE(fixture.ManagerHoldsTheMap());
	CHECK_FALSE(uploads.Handle(fixture.Path()).textureSlot);
}

TEST_CASE(
	"A retired texture taken again is the same upload, decoded no second time",
	"[textureuploads][render]")
{
	Fixture        fixture;
	TextureUploads uploads(*fixture.host);
	QSignalSpy     settled(&uploads, &TextureUploads::Settled);

	uploads.Acquire(fixture.Path());
	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
	const bgl::TextureAssetHandle first = uploads.Handle(fixture.Path());

	uploads.Release(fixture.Path());
	uploads.Acquire(fixture.Path());
	uploads.ReleaseRetired();

	CHECK_FALSE(uploads.IsLoading(fixture.Path()));
	CHECK(uploads.Handle(fixture.Path()).textureSlot == first.textureSlot);
	CHECK(settled.count() == 1);

	uploads.Release(fixture.Path());
	uploads.ReleaseRetired();
	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
}

TEST_CASE("A texture let go of while it decodes is never left uploaded", "[textureuploads][render]")
{
	Fixture        fixture;
	TextureUploads uploads(*fixture.host);
	QSignalSpy     settled(&uploads, &TextureUploads::Settled);

	uploads.Acquire(fixture.Path());
	uploads.Release(fixture.Path());

	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
	CHECK_FALSE(fixture.ManagerHoldsTheMap());
	CHECK(settled.count() == 0);
}

TEST_CASE("A texture that will not decode settles with no upload", "[textureuploads][render]")
{
	Fixture        fixture;
	TextureUploads uploads(*fixture.host);
	QSignalSpy     settled(&uploads, &TextureUploads::Settled);

	const QString missing =
		QString::fromStdWString((fixture.dataRoot / "Derived/SourceTextures/gone.ktx2").wstring());
	uploads.Acquire(missing);

	REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
	CHECK(settled.count() == 1);
	CHECK_FALSE(uploads.IsLoading(missing));
	CHECK_FALSE(uploads.Handle(missing).textureSlot);

	uploads.Release(missing);
}

TEST_CASE("Uploads still held are released when the panel goes", "[textureuploads][render]")
{
	Fixture fixture;
	{
		TextureUploads uploads(*fixture.host);
		uploads.Acquire(fixture.Path());
		REQUIRE(WaitFor([&] { return uploads.IsIdle(); }));
		REQUIRE(fixture.ManagerHoldsTheMap());
	}
	CHECK_FALSE(fixture.ManagerHoldsTheMap());
}
