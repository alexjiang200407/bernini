#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "scene/Scene.h"
#include "util/TestOptions.h"
#include <assetlib_structs/ImageData.h>
#include <bgl/IGraphics.h>

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	// Synthesized rather than loaded: these cases are about descriptor-slot lifetime, so the pixels
	// are irrelevant and a file dependency would only be one more thing to keep alive.
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

	bgl::SceneDesc
	MaterialSceneDesc()
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
}

TEST_CASE("DeleteMaterial frees a material slot for reuse", "[material][delete][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(MaterialSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto& arena = scene->GetMaterialArena();

	SECTION("A PBR material's record is released and its bytes recycled")
	{
		const bgl::MaterialHandle material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		REQUIRE(material.IsValid());
		REQUIRE(arena.IsOffsetValid(material.byteOffset));
		REQUIRE(arena.GetTagAt(material.byteOffset) == bgl::MaterialType::kPBR);

		REQUIRE_NOTHROW(scene->DeleteMaterial(material));
		CHECK_FALSE(arena.IsOffsetValid(material.byteOffset));

		// The freed bytes are handed to the next material, which is the point of freeing them.
		const bgl::MaterialHandle next = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		CHECK(next.byteOffset == material.byteOffset);
		CHECK(arena.IsOffsetValid(next.byteOffset));
	}

	SECTION("A loose PBR material's record is released and its bytes recycled")
	{
		const bgl::MaterialHandle material =
			scene->CreateLoosePbrMaterial(bgl::LoosePbrMaterialDesc());
		REQUIRE(arena.IsOffsetValid(material.byteOffset));
		REQUIRE(arena.GetTagAt(material.byteOffset) == bgl::MaterialType::kLoosePbr);

		REQUIRE_NOTHROW(scene->DeleteMaterial(material));
		CHECK_FALSE(arena.IsOffsetValid(material.byteOffset));

		const bgl::MaterialHandle next = scene->CreateLoosePbrMaterial(bgl::LoosePbrMaterialDesc());
		CHECK(next.byteOffset == material.byteOffset);
	}

	SECTION("The two kinds share one arena, so their records never overlap")
	{
		const bgl::MaterialHandle pbr = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		const bgl::MaterialHandle loose =
			scene->CreateLoosePbrMaterial(bgl::LoosePbrMaterialDesc());

		CHECK(pbr.byteOffset != loose.byteOffset);
		CHECK(arena.GetTagAt(pbr.byteOffset) == bgl::MaterialType::kPBR);
		CHECK(arena.GetTagAt(loose.byteOffset) == bgl::MaterialType::kLoosePbr);
	}

	SECTION("Deleting the same material twice throws")
	{
		const bgl::MaterialHandle material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		REQUIRE_NOTHROW(scene->DeleteMaterial(material));
		REQUIRE_THROWS_AS(scene->DeleteMaterial(material), bgl::SceneError);
	}

	SECTION("A handle whose type disagrees with the record it names throws")
	{
		// The one stale handle the offset check cannot see: the bytes are live, they just hold a
		// record of the other kind now. Only the tag in the header separates the two.
		const bgl::MaterialHandle loose =
			scene->CreateLoosePbrMaterial(bgl::LoosePbrMaterialDesc());

		const auto mislabelled = bgl::MaterialHandle{ .materialType = bgl::MaterialType::kPBR,
			                                          .byteOffset   = loose.byteOffset };

		REQUIRE_THROWS_AS(
			scene->UpdatePbrMaterial(mislabelled, bgl::PbrMaterialDesc()),
			bgl::SceneError);
		REQUIRE_THROWS_AS(scene->DeleteMaterial(mislabelled), bgl::SceneError);

		// ...and the record it named is untouched by the refusal.
		REQUIRE_NOTHROW(scene->DeleteMaterial(loose));
	}

	SECTION("A material type with no storage throws rather than freeing someone else's slot")
	{
		// kNull and kAssert name shading behaviour; they own no record in the arena.
		REQUIRE_THROWS_AS(
			scene->DeleteMaterial(bgl::MaterialHandle{ .materialType = bgl::MaterialType::kNull }),
			bgl::SceneError);
		REQUIRE_THROWS_AS(scene->DeleteMaterial(bgl::MaterialHandle{}), bgl::SceneError);
	}
}

TEST_CASE("DeleteTextureAsset defers the release to the GPU", "[texture][delete][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);
	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto  sceneHandle = gfx->CreateScene(MaterialSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const bgl::TextureAssetHandle texture    = scene->AddTextureAsset(OneTexel());
	const bgl::TextureHandle      gpuTexture = bgl::TextureHandle::From(texture);
	REQUIRE(resourceManager->ValidTextureHandle(gpuTexture));

	SECTION("The handle dies at once; the descriptor slot outlives it, then is reclaimed")
	{
		REQUIRE_NOTHROW(scene->DeleteTextureAsset(texture));

		// Retired: nothing can reach the texture through its handle any more.
		CHECK_FALSE(resourceManager->ValidTextureHandle(gpuTexture));

		// But frames already submitted may still sample it, so the descriptor index is *not* on the
		// free list yet -- a texture created now must land somewhere else.
		const bgl::TextureAssetHandle other = scene->AddTextureAsset(OneTexel());
		CHECK(other.textureSlot.index != texture.textureSlot.index);

		// Stand in for the GPU reaching the fence the release was scheduled against. Now the index
		// returns to the free list, and the next texture reuses it.
		gfxBase->WaitIdle();
		resourceManager->CleanupExpiredResources();

		const bgl::TextureAssetHandle recycled = scene->AddTextureAsset(OneTexel());
		CHECK(recycled.textureSlot.index == texture.textureSlot.index);

		// ...and the original handle stays dead: its generation is behind the slot's.
		CHECK_FALSE(resourceManager->ValidTextureHandle(gpuTexture));
	}

	SECTION("Deleting twice throws instead of double-freeing the descriptor slot")
	{
		REQUIRE_NOTHROW(scene->DeleteTextureAsset(texture));

		// Retiring on destroy is what makes this detectable. Letting it through would record the
		// slot for release twice, and the second reclaim inside the (noexcept) deletion sweep would
		// abort the process a frame later, far from here.
		REQUIRE_THROWS_AS(scene->DeleteTextureAsset(texture), bgl::SceneError);

		gfxBase->WaitIdle();
		resourceManager->CleanupExpiredResources();
		CHECK_FALSE(resourceManager->ValidTextureHandle(gpuTexture));
	}

	SECTION("A null texture handle throws")
	{
		REQUIRE_THROWS_AS(scene->DeleteTextureAsset(bgl::TextureAssetHandle{}), bgl::SceneError);
	}
}

// A texture can be deleted before Update ever flushed its upload -- a caller that fails between
// acquiring assets and drawing releases them with no frame in between. The queued write must die
// with the handle: flushing it later would write through a stale handle (the editor crash this
// pins), while an upload whose texture still lives must survive the same flush.
TEST_CASE("Deleting a texture cancels its pending upload", "[texture][delete][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);
	auto resourceManager = gfxBase->GetResourceManagerCpy();

	auto  sceneHandle = gfx->CreateScene(MaterialSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	// Two uploads queued, neither flushed. One texture dies before any frame runs.
	const bgl::TextureAssetHandle doomed = scene->AddTextureAsset(OneTexel());
	const bgl::TextureAssetHandle kept   = scene->AddTextureAsset(OneTexel());
	REQUIRE_NOTHROW(scene->DeleteTextureAsset(doomed));

	// The flush the next frame would run. Before the fix this wrote through the stale handle and
	// died on the validity assert.
	auto* device       = gfxBase->GetDevice();
	auto  cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);
	auto  cmdAllocator = device->CreateCommandAllocator();
	auto  cmdList =
		device->CreateCommandList({ bgl::QueueType::kGraphics }, cmdAllocator, resourceManager);

	cmdList->Open(cmdQueue.Get(), cmdAllocator.Get());
	scene->Update(cmdList.Get());
	cmdList->Close();
	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList.Get()));

	// The survivor was untouched by the cancellation.
	CHECK(resourceManager->ValidTextureHandle(bgl::TextureHandle::From(kept)));
}
