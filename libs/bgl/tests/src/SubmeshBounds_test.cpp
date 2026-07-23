#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "idl/idl.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>

namespace
{
	// One triangle, one meshlet per submesh: the geometry itself is irrelevant here, only the
	// cooked AABB each submesh carries.
	assetlib::BMesh
	MakeTwoSubmeshMesh(std::span<const std::pair<glm::vec3, glm::vec3>> aabbs)
	{
		constexpr uint16_t kStride = 12;

		auto mesh = assetlib::BMesh();
		mesh.stringPool.push_back('\0');
		mesh.vertexData.resize(static_cast<size_t>(aabbs.size()) * 3 * kStride);

		uint32_t vertexCursor = 0;
		for (const auto& [aabbMin, aabbMax] : aabbs)
		{
			auto meshlet            = assetlib::Meshlet();
			meshlet.vertexOffset    = static_cast<uint32_t>(mesh.meshletVertices.size());
			meshlet.triangleOffset  = static_cast<uint32_t>(mesh.meshletTriangles.size());
			meshlet.vertexCount     = 3;
			meshlet.triangleCount   = 1;
			meshlet.boundingCenter  = glm::vec3(0.0f);
			meshlet.boundingRadius  = 1.0f;
			const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
			mesh.meshlets.push_back(meshlet);

			for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(v);
			for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);

			auto submesh                  = assetlib::Submesh();
			submesh.layout.attributeCount = 1;
			submesh.layout.stride         = kStride;
			submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
				                              assetlib::VertexFormat::kFloat32x3,
				                              0 };
			submesh.vertexByteOffset      = vertexCursor * kStride;
			submesh.vertexCount           = 3;
			submesh.firstMeshlet          = firstMeshlet;
			submesh.meshletCount          = 1;
			submesh.material              = assetlib::c_InvalidIndex;
			submesh.aabbMin               = aabbMin;
			submesh.aabbMax               = aabbMax;
			submesh.nameOffset            = 0;
			mesh.submeshes.push_back(submesh);

			vertexCursor += 3;
		}

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = static_cast<uint32_t>(aabbs.size());
		entry.nameOffset   = 0;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	void
	CheckSphere(const bgl::idl::Submesh& submesh, const glm::vec3& center, float radius)
	{
		CHECK(submesh.boundingCenter.x == Catch::Approx(center.x));
		CHECK(submesh.boundingCenter.y == Catch::Approx(center.y));
		CHECK(submesh.boundingCenter.z == Catch::Approx(center.z));
		CHECK(submesh.boundingRadius == Catch::Approx(radius));
	}
}

TEST_CASE("A submesh's cooked AABB lands on the GPU as its bounding sphere", "[culling][scene]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 4;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxMeshlets             = 16;
	sceneDesc.maxVertexBufferByteSize = 32000;
	sceneDesc.maxIndices              = 1000;

	auto sceneHandle = gfx->CreateScene(sceneDesc);
	REQUIRE(sceneHandle != nullptr);

	auto* scene = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	// Submesh 0: a box around the origin. Submesh 1: an off-center box, so a bound that ignored
	// the AABB's position (or halved the wrong thing) cannot pass both.
	const std::pair<glm::vec3, glm::vec3> aabbs[] = {
		{ glm::vec3(-2.0f), glm::vec3(2.0f) },
		{ glm::vec3(2.0f, 3.0f, 4.0f), glm::vec3(4.0f, 7.0f, 10.0f) },
	};

	const auto geom = scene->AddStaticMesh(MakeTwoSubmeshMesh(aabbs), 0, {});
	REQUIRE(geom.IsValid());

	// Procedural path: bounds fold over the generated vertices instead of a cooked AABB. The cube
	// spans [-1, 1] on every axis.
	const auto cubeGeom = scene->AddCubeGeom(bgl::MaterialHandle());
	REQUIRE(cubeGeom.IsValid());

	auto  buffers       = scene->GetBuffers();
	auto& submeshBuffer = std::get<0>(buffers);

	const float bigBoxRadius = glm::length(glm::vec3(2.0f));
	const float offBoxRadius = glm::length(glm::vec3(1.0f, 2.0f, 3.0f));
	const float cubeRadius   = glm::length(glm::vec3(1.0f));

	// CPU mirror first: this is what Update uploads. Validity is tracked at range roots, so index 1
	// -- interior to the two-submesh range -- has no root of its own; AtIndex still reads it.
	REQUIRE(submeshBuffer.IsIndexValid(0));
	REQUIRE(submeshBuffer.IsIndexValid(2));
	CheckSphere(submeshBuffer.AtIndex(0), glm::vec3(0.0f), bigBoxRadius);
	CheckSphere(submeshBuffer.AtIndex(1), glm::vec3(3.0f, 5.0f, 7.0f), offBoxRadius);
	CheckSphere(submeshBuffer.AtIndex(2), glm::vec3(0.0f), cubeRadius);

	// Then the GPU buffer itself, after an Update: the bytes a cull kernel would read.
	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	auto device          = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = static_cast<uint64_t>(sceneDesc.maxSubmeshes) * sizeof(bgl::idl::Submesh);
	rbDesc.debugName = "Submesh Readback";
	auto readback    = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	submeshBuffer.Update(cmdList);

	cmdList->Barrier(
		submeshBuffer.GetBufferHandle(),
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));
	cmdList->CopyBufferToReadback(readback, submeshBuffer.GetBufferHandle());

	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* uploaded =
		static_cast<const bgl::idl::Submesh*>(resourceManager->MapReadback(readback));
	REQUIRE(uploaded != nullptr);

	CheckSphere(uploaded[0], glm::vec3(0.0f), bigBoxRadius);
	CheckSphere(uploaded[1], glm::vec3(3.0f, 5.0f, 7.0f), offBoxRadius);
	CheckSphere(uploaded[2], glm::vec3(0.0f), cubeRadius);

	resourceManager->UnmapReadback(readback);
	resourceManager->DestroyReadbackBuffer(readback, false);
}
