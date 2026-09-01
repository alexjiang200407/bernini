#include "scene/TextureAssetStore.h"
#include "util/FakeResourceManager.h"
#include "util/RecordingCommandList.h"
#include <bgl/IScene.h>

#include <catch2/catch_test_macros.hpp>

// The store is the one half of a scene that needs no device: it drives an IResourceManager and a
// command list, both faked here. Every case below would otherwise cost a CreateGraphics.

namespace
{
	using namespace bgl;

	// One 2x2 RGBA8 image, one subresource -- enough to be uploadable, nothing more.
	assetlib::ImageData
	MakeImage()
	{
		auto img     = assetlib::ImageData();
		img.width    = 2;
		img.height   = 2;
		img.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
		img.pixels   = core::fixed_buffer<std::byte>(16);
		img.subresources.push_back({ 0, 8, 16 });
		return img;
	}

	// The store and the fake behind it, with the fake still reachable for assertions. Make() is
	// what leaves the refcount at one: RefCounter starts there, and SharedRef's raw-pointer
	// constructor would add a second the fake never sheds.
	struct Fixture
	{
		// Spelled out because the store is non-copyable: MSVC errors on a special member it had to
		// delete implicitly (C4625/C5026/C4626/C5027 under /WX).
		Fixture()               = default;
		Fixture(const Fixture&) = delete;
		Fixture(Fixture&&)      = delete;

		Fixture&
		operator=(const Fixture&) = delete;

		Fixture&
		operator=(Fixture&&) = delete;

		core::SharedRef<test::FakeResourceManager> rm =
			core::SharedRef<test::FakeResourceManager>::Make();

		TextureAssetStore store{ rm };

		// The two 1x1 defaults the store builds in its constructor, which every count below is
		// measured against rather than assuming an empty store.
		static constexpr size_t c_DefaultCount = 2;
	};
}

TEST_CASE("The store builds its default textures up front", "[textures]")
{
	auto fixture = Fixture();

	REQUIRE(fixture.rm->createdTextures.size() == Fixture::c_DefaultCount);

	const auto white = fixture.store.GetDefaultSlot(TextureAssetStore::DefaultTexture::kWhite);
	const auto normal =
		fixture.store.GetDefaultSlot(TextureAssetStore::DefaultTexture::kFlatNormal);

	CHECK_FALSE(white.is_null());
	CHECK_FALSE(normal.is_null());
	CHECK(white != normal);

	// A material resolves a fallback channel through the descriptor, so a default with no view
	// would silently sample nothing.
	CHECK_FALSE(fixture.store.GetSrv(white).IsNull());
	CHECK_FALSE(fixture.store.GetSrv(normal).IsNull());
}

TEST_CASE("An added texture is uploaded once, on the first flush", "[textures]")
{
	auto fixture = Fixture();
	auto cmdList = core::SharedRef<test::RecordingCommandList>::Make();

	const TextureAssetHandle texture = fixture.store.Add(MakeImage(), "Test");
	REQUIRE_FALSE(texture.textureSlot.is_null());

	fixture.store.Flush(cmdList.Get());

	CHECK(cmdList->WroteTexture(TextureHandle::From(texture)));
	CHECK(cmdList->textureWrites.size() == Fixture::c_DefaultCount + 1);

	// The queue is consumed, not replayed: a second flush would re-upload into a texture the
	// frames in flight are already sampling.
	cmdList->textureWrites.clear();
	fixture.store.Flush(cmdList.Get());
	CHECK(cmdList->textureWrites.empty());
}

TEST_CASE("A texture deleted before the flush never reaches the command list", "[textures]")
{
	auto fixture = Fixture();
	auto cmdList = core::SharedRef<test::RecordingCommandList>::Make();

	const TextureAssetHandle doomed = fixture.store.Add(MakeImage(), "Doomed");
	const TextureAssetHandle kept   = fixture.store.Add(MakeImage(), "Kept");
	REQUIRE_FALSE(doomed.textureSlot.is_null());
	REQUIRE_FALSE(kept.textureSlot.is_null());

	// No frame in between, so the upload queued by Add is still pending when the release lands.
	fixture.store.Delete(doomed);

	fixture.store.Flush(cmdList.Get());

	// The slot retired on delete, so writing it would write whatever now owns it. This is the
	// assertion the whole store exists to make provable.
	CHECK_FALSE(cmdList->WroteTexture(TextureHandle::From(doomed)));
	CHECK(cmdList->WroteTexture(TextureHandle::From(kept)));

	// Nor may it be barriered: a barrier names the retired slot just as a write does.
	const auto barriered =
		std::ranges::find(cmdList->barrieredTextures, TextureHandle::From(doomed));
	CHECK(barriered == cmdList->barrieredTextures.end());
}

TEST_CASE("Deleting a texture releases the view created with it", "[textures]")
{
	auto fixture = Fixture();

	const TextureAssetHandle texture = fixture.store.Add(MakeImage(), "Test");
	const SrvHandle          srv     = fixture.store.GetSrv(texture.textureSlot);
	REQUIRE_FALSE(srv.IsNull());

	fixture.store.Delete(texture);

	// DestroyTexture does not cascade to views, so an unreleased view outlives its texture and
	// leaks a descriptor for the life of the scene.
	REQUIRE(fixture.rm->destroyedSrvs.size() == 1);
	CHECK(fixture.rm->destroyedSrvs.front().idx == srv.idx);
	CHECK(fixture.rm->destroyedTextures.size() == 1);

	CHECK(fixture.store.GetSrv(texture.textureSlot).IsNull());
}

TEST_CASE("A texture whose view cannot be created is taken back down", "[textures]")
{
	auto fixture = Fixture();

	const size_t liveBefore = fixture.rm->LiveTextureCount();

	fixture.rm->failNextSrv          = true;
	const TextureAssetHandle texture = fixture.store.Add(MakeImage(), "No View");

	CHECK(texture.textureSlot.is_null());

	// The texture was created before the view failed, so it is the store's to destroy -- nobody
	// else holds a handle to it.
	CHECK(fixture.rm->LiveTextureCount() == liveBefore);
	CHECK(fixture.rm->destroyedTextures.size() == 1);
}

TEST_CASE("A texture that could not be created queues no upload", "[textures]")
{
	auto fixture = Fixture();
	auto cmdList = core::SharedRef<test::RecordingCommandList>::Make();

	fixture.rm->failNextTexture      = true;
	const TextureAssetHandle texture = fixture.store.Add(MakeImage(), "No Texture");

	CHECK(texture.textureSlot.is_null());

	fixture.store.Flush(cmdList.Get());

	// Only the defaults, so the failed Add left nothing behind to write into a null handle.
	CHECK(cmdList->textureWrites.size() == Fixture::c_DefaultCount);
}

TEST_CASE("Deleting a texture twice is refused", "[textures]")
{
	auto fixture = Fixture();

	const TextureAssetHandle texture = fixture.store.Add(MakeImage(), "Test");
	fixture.store.Delete(texture);

	CHECK_THROWS_AS(fixture.store.Delete(texture), SceneError);
	CHECK_THROWS_AS(fixture.store.Delete(TextureAssetHandle{}), SceneError);
}
