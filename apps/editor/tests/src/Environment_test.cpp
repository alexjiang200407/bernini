#include "Render/Renderer.h"
#include "Render/environment.h"

#include <assetlib_structs/ImageData.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
	// Nothing samples these: the cases are about which slots a swap hands back, so a one-texel image
	// keeps the scene small and needs no file on disk.
	assetlib::ImageData
	OneTexel()
	{
		auto img     = assetlib::ImageData();
		img.width    = 1;
		img.height   = 1;
		img.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
		img.pixels   = core::fixed_buffer<std::byte>(4);
		img.subresources.push_back({ 0, 4, 4 });
		return img;
	}

	// No geometry is drawn, so only the texture pool has to be big enough for two environments.
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

	// A device and a scene, reached the way the editor reaches them -- through the render thread that
	// is the only one allowed to touch either.
	struct Fixture
	{
		std::optional<Renderer> renderer;

		Fixture()
		{
			auto opts             = bgl::GraphicsOptions();
			opts.enableDebugLayer = true;

			renderer.emplace(opts, MakeSceneDesc());
		}

		[[nodiscard]] bgl::IScene*
		Scene() const
		{
			return renderer->GetScene().Get();
		}

		[[nodiscard]] editor::AppliedEnvironment
		AddEnvironment() const
		{
			auto env       = editor::AppliedEnvironment();
			env.irradiance = Scene()->AddTextureAsset(OneTexel());
			env.prefilter  = Scene()->AddTextureAsset(OneTexel());
			env.skybox     = Scene()->AddTextureAsset(OneTexel());
			return env;
		}

		// A texture asset can be deleted exactly once, so a delete that throws is the proof that
		// ReplaceEnvironment already handed the slot back.
		[[nodiscard]] bool
		StillAlive(bgl::TextureAssetHandle texture) const
		{
			try
			{
				Scene()->DeleteTextureAsset(texture);
				return true;
			}
			catch (const bgl::SceneError&)
			{
				return false;
			}
		}
	};
}

TEST_CASE("An environment that failed to load releases nothing", "[environment][render]")
{
	Fixture fixture;

	// The closure is skipped outright if the render thread died, which would pass every case in it.
	const bool ran = fixture.renderer->Invoke([&] {
		const editor::AppliedEnvironment previous = fixture.AddEnvironment();

		// What ApplyEnvironment returns when the `.benv` cannot be resolved: the view is left naming
		// every previous map, so releasing one would strand a live binding on a retired slot.
		const editor::AppliedEnvironment bound =
			editor::ReplaceEnvironment(fixture.Scene(), previous, editor::AppliedEnvironment());

		REQUIRE(bound.irradiance.textureSlot == previous.irradiance.textureSlot);
		REQUIRE(bound.prefilter.textureSlot == previous.prefilter.textureSlot);
		REQUIRE(bound.skybox.textureSlot == previous.skybox.textureSlot);

		REQUIRE(fixture.StillAlive(previous.irradiance));
		REQUIRE(fixture.StillAlive(previous.prefilter));
		REQUIRE(fixture.StillAlive(previous.skybox));
		return true;
	});
	REQUIRE(ran);
}

TEST_CASE("Only the maps an environment replaced are released", "[environment][render]")
{
	Fixture fixture;

	const bool ran = fixture.renderer->Invoke([&] {
		const editor::AppliedEnvironment previous = fixture.AddEnvironment();

		// The IBL pair loaded and the skybox did not -- ApplyEnvironment binds each independently.
		auto applied       = editor::AppliedEnvironment();
		applied.irradiance = fixture.Scene()->AddTextureAsset(OneTexel());
		applied.prefilter  = fixture.Scene()->AddTextureAsset(OneTexel());

		const editor::AppliedEnvironment bound =
			editor::ReplaceEnvironment(fixture.Scene(), previous, applied);

		REQUIRE(bound.irradiance.textureSlot == applied.irradiance.textureSlot);
		REQUIRE(bound.prefilter.textureSlot == applied.prefilter.textureSlot);
		REQUIRE(bound.skybox.textureSlot == previous.skybox.textureSlot);

		REQUIRE_FALSE(fixture.StillAlive(previous.irradiance));
		REQUIRE_FALSE(fixture.StillAlive(previous.prefilter));
		REQUIRE(fixture.StillAlive(previous.skybox));
		return true;
	});
	REQUIRE(ran);
}

// What a preview leaving its panel has to do about the backdrop, decided without a device: the
// restore exists to undo a *drop*, and re-applying costs three cube map uploads. A panel is left
// and returned to far more often than an environment is dropped on it, so the ordinary case must
// come back with nothing to do.

TEST_CASE("A preview nobody dropped on has nothing to restore", "[environment]")
{
	auto binding                      = editor::EnvironmentBinding();
	binding.configured.environmentMap = "Authored/Environments/studio.benv";
	binding.boundPath                 = "Authored/Environments/studio.benv";

	CHECK_FALSE(editor::GetEnvironmentToRestore(binding).has_value());
}

TEST_CASE("A dropped environment is restored to the configured one", "[environment]")
{
	auto binding                      = editor::EnvironmentBinding();
	binding.configured.environmentMap = "Authored/Environments/studio.benv";
	binding.boundPath                 = "Authored/Environments/sunset.benv";

	const std::optional<std::string> restore = editor::GetEnvironmentToRestore(binding);
	REQUIRE(restore.has_value());
	CHECK(*restore == "Authored/Environments/studio.benv");
}

TEST_CASE("A preview configured with no environment keeps the drop", "[environment]")
{
	// Restoring "nothing" is an apply that binds nothing, so it would displace nothing and the drop
	// would stay lit anyway -- and the only other reading of it is an unlit preview, which is worse
	// than somebody else's backdrop.
	auto binding      = editor::EnvironmentBinding();
	binding.boundPath = "Authored/Environments/sunset.benv";

	CHECK_FALSE(editor::GetEnvironmentToRestore(binding).has_value());
}

// What a viewport has to answer when the Content Explorer asks whether a file may be deleted. A
// `.benv` is the one end of the environment chain nothing on disk references -- assetlib's graph
// refuses a `.bsky` or a `.benvl` a `.benv` names, but nothing names the `.benv` -- so a panel
// saying it is lit by one is the only thing standing between a live environment and its deletion.

TEST_CASE("A viewport holds the environment it is lit by", "[environment]")
{
	auto binding                      = editor::EnvironmentBinding();
	binding.configured.environmentMap = "Authored/Environments/studio.benv";
	binding.boundPath                 = "Authored/Environments/studio.benv";

	CHECK(
		editor::GetHeldOpenEnvironment(binding) ==
		QStringList{ "Authored/Environments/studio.benv" });
}

TEST_CASE("A dropped environment is what is held, not the configured one", "[environment]")
{
	// The drop is what is on screen. Naming the configured one instead would refuse a deletion of a
	// file nothing is drawing, and allow one of the file every view is.
	auto binding                      = editor::EnvironmentBinding();
	binding.configured.environmentMap = "Authored/Environments/studio.benv";
	binding.boundPath                 = "Authored/Environments/sunset.benv";

	CHECK(
		editor::GetHeldOpenEnvironment(binding) ==
		QStringList{ "Authored/Environments/sunset.benv" });
}

TEST_CASE("A view that bound nothing holds nothing", "[environment]")
{
	// Configured is not bound: a viewport built without a renderer, or one whose `.benv` would not
	// load, is lit by nothing and must not refuse a deletion on the strength of a config entry.
	auto binding                      = editor::EnvironmentBinding();
	binding.configured.environmentMap = "Authored/Environments/studio.benv";

	CHECK(editor::GetHeldOpenEnvironment(binding).isEmpty());
	CHECK(editor::GetHeldOpenEnvironment(editor::EnvironmentBinding()).isEmpty());
}
