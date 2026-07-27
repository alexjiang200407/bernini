#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "idl/Constants.h"
#include "idl/Mesh.h"
#include "idl/Meshlet.h"
#include "idl/MeshletInstance.h"
#include "idl/Submesh.h"
#include "idl/VertexLayout.h"
#include "resource/ResourceManager.h"
#include "types/SubmeshInstance.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The BGL_WGSL arm of the real forward shader: Forward_StaticMesh's VSMain walking the same
// meshlet -> vertexMap -> byte-decode chain MSMain walks, addressed off SV_VertexID instead of a
// mesh-shader payload. The geometry is hand-built rather than cooked so the whole chain is pinned by
// known values -- one submesh, one meshlet, one triangle, positions already in NDC with an identity
// transform, so a wrong link anywhere in the chain moves or drops the triangle.
//
// The draw covers a full meshlet (cMaxPrimsPerMeshlet * 3), so all but the first primitive take the
// degenerate path; red corners prove those collapsed instead of rasterizing garbage.
TEST_CASE("The forward vertex shader pulls a meshlet through the decode chain", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	// Positions are NDC, so an identity viewProj and mesh transform leave them untouched.
	const float vertexData[9] = {
		0.0f, 0.8f, 0.0f, -0.8f, -0.8f, 0.0f, 0.8f, -0.8f, 0.0f,
	};

	auto layout                     = idl::VertexLayout{};
	layout.attributes[0].semantic   = idl::VertexSemantic::kPosition;
	layout.attributes[0].format     = idl::VertexFormat::kFloat32x3;
	layout.attributes[0].byteOffset = 0;
	layout.attributeCount           = 1;
	layout.stride                   = 3 * sizeof(float);

	auto submesh           = idl::Submesh{};
	submesh.layout         = layout;
	submesh.meshlets.range = idl::Range{ .offsetStart = 0 };
	submesh.meshlets.count = 1;
	submesh.vertexMap      = idl::Range{ .offsetStart = 0 };
	submesh.vertexData     = idl::Range{ .offsetStart = 0 };
	submesh.indices        = idl::Range{ .offsetStart = 0 };
	submesh.vertexCount    = 3;
	submesh.boundingCenter = glm::vec3(0.0f);
	submesh.boundingRadius = 1.0f;

	auto meshlet                 = idl::Meshlet{};
	meshlet.relativeVertexOffset = 0;
	meshlet.relativeIndexOffset  = 0;
	meshlet.vertexCount          = 3;
	meshlet.triangleCount        = 1;
	meshlet.boundingCenter       = glm::vec3(0.0f);
	meshlet.boundingRadius       = 1.0f;

	auto mesh            = idl::Mesh{};
	mesh.transform       = glm::mat4(1.0f);
	mesh.submeshes.range = idl::Range{ .offsetStart = 0 };
	mesh.submeshes.count = 1;

	auto instance         = SubmeshInstance{};
	instance.meshInstance = idl::Entry{ .offset = 0 };
	instance.submeshIndex = 0;
	instance.material     = idl::Entry{ .offset = 0 };
	instance.pso          = 0;

	const auto record = idl::MeshletInstance{ .instanceIndex = 0, .meshletIndex = 0 };

	const uint32_t vertexMap[3] = { 0, 1, 2 };
	const uint32_t indices[3]   = { 0, 1, 2 };

	const auto make = [&](size_t bytes, const char* name) {
		return resources->CreateComputeBuffer(
			ComputeBufferDesc{}
				.SetElement<uint32_t>()
				.SetInitialCount(static_cast<uint32_t>(bytes / 4))
				.SetDebugName(name));
	};

	const auto instanceBuffer   = make(sizeof(instance), "instances");
	const auto meshBuffer       = make(sizeof(mesh), "meshes");
	const auto submeshBuffer    = make(sizeof(submesh), "submeshes");
	const auto meshletBuffer    = make(sizeof(meshlet), "meshlets");
	const auto vertexMapBuffer  = make(sizeof(vertexMap), "vertexMap");
	const auto vertexDataBuffer = make(sizeof(vertexData), "vertexData");
	const auto indexBuffer      = make(sizeof(indices), "indices");
	const auto recordBuffer     = make(sizeof(record), "meshletInstances");

	const auto meshShader =
		device->CreateShader(ShaderDesc{}.SetSlangModuleName("Forward_StaticMesh"));
	const auto pixelShader =
		device->CreateShader(ShaderDesc{}.SetSlangModuleName("MultiModulePixel"));

	auto desc = MeshletPipelineDesc{};
	desc.SetMeshShader(meshShader).SetPixelShader(pixelShader).AddRtvFormat(Format::RGBA8_UNORM);
	desc.renderState.rasterState.SetCullNone();

	auto kernel = device->CreateMeshletKernel(desc);
	REQUIRE(kernel.ContainsUniforms("forwardData"));
	REQUIRE(kernel.ContainsUniforms("viewData"));

	// The amplification stage is the only reader of expansionData, and this arm has no such stage,
	// so its read-write buffers never reach a vertex-stage binding.
	REQUIRE_FALSE(kernel.ContainsUniforms("expansionData"));

	auto& viewData           = kernel["viewData"];
	viewData["viewProj"]     = glm::mat4(1.0f);
	viewData["prevViewProj"] = glm::mat4(1.0f);

	auto& forwardData               = kernel["forwardData"];
	forwardData["instanceBuffer"]   = instanceBuffer;
	forwardData["meshBuffer"]       = meshBuffer;
	forwardData["submeshBuffer"]    = submeshBuffer;
	forwardData["meshletBuffer"]    = meshletBuffer;
	forwardData["vertexMapBuffer"]  = vertexMapBuffer;
	forwardData["vertexDataBuffer"] = vertexDataBuffer;
	forwardData["indexBuffer"]      = indexBuffer;
	forwardData["meshletInstances"] = recordBuffer;

	// Declared unconditionally by debug.dbg and so always a binding, even though nothing in this
	// pipeline raises: an unwritten handle reads slot zero and would alias a buffer bound elsewhere.
	if (auto* debug = kernel.FindUniforms("gDebug"))
	{
		(*debug)["buffer"] = make(64, "debug");
	}

	auto texDesc      = TextureDesc{};
	texDesc.width     = c_Size;
	texDesc.height    = c_Size;
	texDesc.format    = Format::RGBA8_UNORM;
	texDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	texDesc.debugName = "rt";

	const auto texture = resources->CreateTexture(texDesc);
	const auto rtv     = resources->CreateRtv(texture, RtvDesc{ .format = Format::RGBA8_UNORM });

	const auto fb = FrameBuffer().AddColorAttachment(rtv);

	auto viewport = ViewportState();
	viewport.AddViewportAndScissorRect(
		Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)));

	const auto rbLayout = resources->GetTextureReadbackLayout(texture);
	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = rbLayout.totalBytes, .debugName = "rt-readback" });

	float clear[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

	auto* wgpuList = static_cast<CommandList*>(list.Get());

	list->Open(queue.Get(), allocator.Get());
	list->WriteBuffer(instanceBuffer, &instance, 0, sizeof(instance));
	list->WriteBuffer(meshBuffer, &mesh, 0, sizeof(mesh));
	list->WriteBuffer(submeshBuffer, &submesh, 0, sizeof(submesh));
	list->WriteBuffer(meshletBuffer, &meshlet, 0, sizeof(meshlet));
	list->WriteBuffer(vertexMapBuffer, vertexMap, 0, sizeof(vertexMap));
	list->WriteBuffer(vertexDataBuffer, vertexData, 0, sizeof(vertexData));
	list->WriteBuffer(indexBuffer, indices, 0, sizeof(indices));
	list->WriteBuffer(recordBuffer, &record, 0, sizeof(record));

	resources->ClearRtv(list.Get(), rtv, clear);
	wgpuList->BeginRenderPass(fb, viewport);
	wgpuList->SetGraphicsKernel(kernel);
	wgpuList->Draw(idl::cMaxPrimsPerMeshlet * 3);
	wgpuList->EndRenderPass();
	list->CopyTextureToReadback(readback, texture);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	const auto at = [&](uint32_t x, uint32_t y) { return mapped[(y * c_Size) + x]; };
	CHECK(at(c_Size / 2, c_Size / 2) == 0xFF00FF00u);
	CHECK(at(0, 0) == 0xFF0000FFu);
	CHECK(at(c_Size - 1, 0) == 0xFF0000FFu);

	resources->UnmapReadback(readback);
	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}
