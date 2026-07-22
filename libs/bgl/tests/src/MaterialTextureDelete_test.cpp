#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "scene/Scene.h"
#include "util/TestOptions.h"
#include <assetlib/image_io.h>
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

	bgl::SceneDesc
	MaterialSceneDesc()
	{
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 2;
		desc.maxSubmeshes            = 2;
		desc.maxMeshlets             = 8;
		desc.maxVertexBufferByteSize = 4096;
		desc.maxIndices              = 128;
		desc.maxPbrMaterials         = 4;
		desc.maxLoosePbrMaterials    = 4;
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

	auto  buffers     = scene->GetBuffers();
	auto& pbrBuffer   = std::get<5>(buffers);
	auto& looseBuffer = std::get<6>(buffers);

	SECTION("A PBR material's slot is released and recycled")
	{
		const bgl::MaterialHandle material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		REQUIRE(material.IsValid());
		REQUIRE(pbrBuffer.IsValid(material.handle));

		REQUIRE_NOTHROW(scene->DeleteMaterial(material));
		CHECK_FALSE(pbrBuffer.IsValid(material.handle));

		// The freed slot is handed to the next material, which is the point of freeing it.
		const bgl::MaterialHandle next = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		CHECK(next.handle.index == material.handle.index);
		CHECK(pbrBuffer.IsValid(next.handle));

		// ...but the stale handle does not resurrect: its generation is behind the slot's.
		CHECK_FALSE(pbrBuffer.IsValid(material.handle));
	}

	SECTION("A loose PBR material's slot is released and recycled")
	{
		const bgl::MaterialHandle material =
			scene->CreateLoosePbrMaterial(bgl::LoosePbrMaterialDesc());
		REQUIRE(looseBuffer.IsValid(material.handle));

		REQUIRE_NOTHROW(scene->DeleteMaterial(material));
		CHECK_FALSE(looseBuffer.IsValid(material.handle));

		const bgl::MaterialHandle next = scene->CreateLoosePbrMaterial(bgl::LoosePbrMaterialDesc());
		CHECK(next.handle.index == material.handle.index);
	}

	SECTION("Deleting the same material twice throws")
	{
		const bgl::MaterialHandle material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		REQUIRE_NOTHROW(scene->DeleteMaterial(material));
		REQUIRE_THROWS_AS(scene->DeleteMaterial(material), bgl::SceneError);
	}

	SECTION("A material type with no storage throws rather than freeing someone else's slot")
	{
		// kNull and kAssert name shading behaviour; they own no entry in either material buffer.
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

	const bgl::TextureAssetHandle texture =
		scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2"));
	const bgl::TextureHandle gpuTexture = bgl::TextureHandle::From(texture);
	REQUIRE(resourceManager->ValidTextureHandle(gpuTexture));

	SECTION("The handle dies at once; the descriptor slot outlives it, then is reclaimed")
	{
		REQUIRE_NOTHROW(scene->DeleteTextureAsset(texture));

		// Retired: nothing can reach the texture through its handle any more.
		CHECK_FALSE(resourceManager->ValidTextureHandle(gpuTexture));

		// But frames already submitted may still sample it, so the descriptor index is *not* on the
		// free list yet -- a texture created now must land somewhere else.
		const bgl::TextureAssetHandle other =
			scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2"));
		CHECK(other.textureSlot.index != texture.textureSlot.index);

		// Stand in for the GPU reaching the fence the release was scheduled against. Now the index
		// returns to the free list, and the next texture reuses it.
		gfxBase->WaitIdle();
		resourceManager->CleanupExpiredResources();

		const bgl::TextureAssetHandle recycled =
			scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2"));
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
	const bgl::TextureAssetHandle doomed =
		scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2"));
	const bgl::TextureAssetHandle kept =
		scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2"));
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
