#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "idl/BaseTable.h"
#include "idl/Constants.h"
#include "idl/DispatchArgs.h"
#include "idl/DrawIndirectArgs.h"
#include "idl/Mesh.h"
#include "idl/Meshlet.h"
#include "idl/MeshletInstance.h"
#include "idl/Submesh.h"
#include "idl/VertexLayout.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include "types/SubmeshInstance.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The whole mesh-shader replacement in one command list: ExpandMeshlets turns a compacted instance
// into meshlet records and the draw arguments that cover them, and Forward_StaticMesh's vertex arm
// then pulls those records and rasterizes -- so nothing between the instance list and the pixels is
// supplied by the CPU. The amplification stage does exactly this on D3D12.
//
// The geometry is hand-built so every link is pinned by a known value: one submesh, one meshlet, one
// triangle already in NDC under an identity transform. The kernel's own outputs are checked as well
// as the picture, so a break says whether the kernel or the draw was wrong.
TEST_CASE("The expansion kernel drives a vertex-pulling indirect draw", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	// Two triangles, one per meshlet, in the left and right halves of NDC. Nothing covers the
	// centre, so a draw that ignored the records and smeared geometry everywhere would show.
	const float vertexData[18] = {
		-0.5f, 0.6f, 0.0f, -0.9f, -0.6f, 0.0f, -0.1f, -0.6f, 0.0f,
		0.5f,  0.6f, 0.0f, 0.1f,  -0.6f, 0.0f, 0.9f,  -0.6f, 0.0f,
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
	submesh.meshlets.count = 2;
	submesh.vertexMap      = idl::Range{ .offsetStart = 0 };
	submesh.vertexData     = idl::Range{ .offsetStart = 0 };
	submesh.indices        = idl::Range{ .offsetStart = 0 };
	submesh.vertexCount    = 6;
	submesh.boundingCenter = glm::vec3(0.0f);
	submesh.boundingRadius = 1.0f;

	// Meshlet 1's non-zero offsets are what prove a record's meshletIndex is honoured: read it as 0
	// and the right-hand triangle would be drawn twice on the left.
	auto meshlets = std::array<idl::Meshlet, 2>{};
	for (uint32_t m = 0; m < 2; ++m)
	{
		meshlets[m].relativeVertexOffset = m * 3;
		meshlets[m].relativeIndexOffset  = m * 3;
		meshlets[m].vertexCount          = 3;
		meshlets[m].triangleCount        = 1;
		meshlets[m].boundingCenter       = glm::vec3(0.0f);
		meshlets[m].boundingRadius       = 1.0f;
	}

	auto mesh            = idl::Mesh{};
	mesh.transform       = glm::mat4(1.0f);
	mesh.submeshes.range = idl::Range{ .offsetStart = 0 };
	mesh.submeshes.count = 1;

	auto instance         = SubmeshInstance{};
	instance.meshInstance = idl::Entry{ .offset = 0 };
	instance.submeshIndex = 0;
	instance.material     = idl::Entry{ .offset = 0 };
	instance.pso          = 0;

	const uint32_t vertexMap[6] = { 0, 1, 2, 3, 4, 5 };

	// Meshlet-local, so both meshlets index 0..2; relativeIndexOffset is what separates them.
	const uint32_t indices[6] = { 0, 1, 2, 0, 1, 2 };

	// One instance in bucket 0, sitting at the front of the compacted list.
	const uint32_t compacted[1] = { 0 };
	const uint32_t prefixSum[1] = { 1 };

	auto dispatchArgs         = idl::DispatchArgs{};
	dispatchArgs.threadCountX = 1;
	dispatchArgs.threadCountY = 1;
	dispatchArgs.threadCountZ = 1;

	// vertexCount is the kernel's allocator, so it starts at zero; the rest is what drawIndirect
	// consumes untouched.
	auto drawArgs          = idl::DrawIndirectArgs{};
	drawArgs.vertexCount   = 0;
	drawArgs.instanceCount = 1;
	drawArgs.firstVertex   = 0;
	drawArgs.firstInstance = 0;

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
	const auto meshletBuffer    = make(sizeof(meshlets), "meshlets");
	const auto vertexMapBuffer  = make(sizeof(vertexMap), "vertexMap");
	const auto vertexDataBuffer = make(sizeof(vertexData), "vertexData");
	const auto indexBuffer      = make(sizeof(indices), "indices");
	const auto compactedBuffer  = make(sizeof(compacted), "compactedInstances");
	const auto prefixSumBuffer  = make(sizeof(prefixSum), "psoPrefixSum");
	const auto partitionBuffer  = make(sizeof(uint32_t), "partitionBase");
	const auto recordBuffer     = make(sizeof(idl::MeshletInstance) * 2, "meshletInstances");
	const auto drawArgsBuffer   = make(sizeof(drawArgs), "drawArgs");
	const auto dispatchBuffer   = make(sizeof(dispatchArgs), "dispatchArgs");

	auto expand = device->CreateComputeKernel(
		ComputePipelineDesc{}
			.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName("ExpandMeshlets")))
			.SetDebugName("expand-meshlets"),
		resources);

	REQUIRE(expand.uniforms.contains("gExpand"));
	REQUIRE(expand.uniforms.contains("expansionData"));

	auto& expandData               = expand["gExpand"];
	expandData["instanceBuffer"]   = instanceBuffer;
	expandData["meshBuffer"]       = meshBuffer;
	expandData["submeshBuffer"]    = submeshBuffer;
	expandData["meshletInstances"] = recordBuffer;
	expandData["drawArgs"]         = drawArgsBuffer;
	expandData["dispatchArgs"]     = dispatchBuffer;

	auto& expansion                       = expand["expansionData"];
	expansion["psoIndex"]                 = 0u;
	expansion["baseTable"]                = idl::BaseTable::kPsoBucketed;
	expansion["partitionIndex"]           = 0u;
	expansion["compactedInstances"]       = compactedBuffer;
	expansion["psoPrefixSum"]             = prefixSumBuffer;
	expansion["transparentPartitionBase"] = partitionBuffer;

	auto computeState   = ComputeState{};
	computeState.kernel = &expand;

	const auto meshShader =
		device->CreateShader(ShaderDesc{}.SetSlangModuleName("Forward_StaticMesh"));
	const auto pixelShader =
		device->CreateShader(ShaderDesc{}.SetSlangModuleName("MultiModulePixel"));

	auto gfxDesc = MeshletPipelineDesc{};
	gfxDesc.SetMeshShader(meshShader).SetPixelShader(pixelShader).AddRtvFormat(Format::RGBA8_UNORM);
	gfxDesc.renderState.rasterState.SetCullNone();

	auto gfx = device->CreateMeshletKernel(gfxDesc, resources);

	auto& viewData           = gfx["viewData"];
	viewData["viewProj"]     = glm::mat4(1.0f);
	viewData["prevViewProj"] = glm::mat4(1.0f);

	auto& forwardData               = gfx["forwardData"];
	forwardData["instanceBuffer"]   = instanceBuffer;
	forwardData["meshBuffer"]       = meshBuffer;
	forwardData["submeshBuffer"]    = submeshBuffer;
	forwardData["meshletBuffer"]    = meshletBuffer;
	forwardData["vertexMapBuffer"]  = vertexMapBuffer;
	forwardData["vertexDataBuffer"] = vertexDataBuffer;
	forwardData["indexBuffer"]      = indexBuffer;
	forwardData["meshletInstances"] = recordBuffer;

	// An unwritten handle would read slot zero and alias a buffer bound elsewhere.
	if (auto* debug = gfx.FindUniforms("gDebug"))
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

	const auto argsReadback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = sizeof(drawArgs), .debugName = "args-readback" });
	const auto recordReadback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize  = sizeof(idl::MeshletInstance) * 2,
	                        .debugName = "record-readback" });

	float clear[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

	auto* wgpuList = static_cast<CommandList*>(list.Get());

	list->Open(queue.Get(), allocator.Get());
	list->WriteBuffer(instanceBuffer, &instance, 0, sizeof(instance));
	list->WriteBuffer(meshBuffer, &mesh, 0, sizeof(mesh));
	list->WriteBuffer(submeshBuffer, &submesh, 0, sizeof(submesh));
	list->WriteBuffer(meshletBuffer, meshlets.data(), 0, sizeof(meshlets));
	list->WriteBuffer(vertexMapBuffer, vertexMap, 0, sizeof(vertexMap));
	list->WriteBuffer(vertexDataBuffer, vertexData, 0, sizeof(vertexData));
	list->WriteBuffer(indexBuffer, indices, 0, sizeof(indices));
	list->WriteBuffer(compactedBuffer, compacted, 0, sizeof(compacted));
	list->WriteBuffer(prefixSumBuffer, prefixSum, 0, sizeof(prefixSum));
	list->WriteBuffer(drawArgsBuffer, &drawArgs, 0, sizeof(drawArgs));
	list->WriteBuffer(dispatchBuffer, &dispatchArgs, 0, sizeof(dispatchArgs));

	list->SetComputeState(computeState);
	list->Dispatch(1, 1, 1);

	resources->ClearRtv(list.Get(), rtv, clear);
	wgpuList->BeginRenderPass(fb, viewport);
	wgpuList->SetGraphicsKernel(gfx);
	wgpuList->DrawIndirect(drawArgsBuffer, 0);
	wgpuList->EndRenderPass();

	list->CopyTextureToReadback(readback, texture);
	list->CopyBufferToReadback(argsReadback, drawArgsBuffer);
	list->CopyBufferToReadback(recordReadback, recordBuffer);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	// Two meshlets expanded, so the draw covers two records' worth of vertices.
	const auto* args =
		static_cast<const idl::DrawIndirectArgs*>(resources->MapReadback(argsReadback));
	REQUIRE(args != nullptr);
	CHECK(args->vertexCount == 2 * idl::cVerticesPerMeshletRecord);
	CHECK(args->instanceCount == 1u);
	CHECK(args->firstVertex == 0u);
	resources->UnmapReadback(argsReadback);

	const auto* record =
		static_cast<const idl::MeshletInstance*>(resources->MapReadback(recordReadback));
	REQUIRE(record != nullptr);
	CHECK(record[0].instanceIndex == 0u);
	CHECK(record[0].meshletIndex == 0u);
	CHECK(record[1].instanceIndex == 0u);
	CHECK(record[1].meshletIndex == 1u);
	resources->UnmapReadback(recordReadback);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	// One triangle per meshlet, and the gap between them still clear.
	const auto at = [&](uint32_t x, uint32_t y) { return mapped[(y * c_Size) + x]; };
	CHECK(at(c_Size / 4, c_Size / 2) == 0xFF00FF00u);
	CHECK(at((c_Size * 3) / 4, c_Size / 2) == 0xFF00FF00u);
	CHECK(at(c_Size / 2, c_Size / 2) == 0xFF0000FFu);
	CHECK(at(0, 0) == 0xFF0000FFu);

	resources->UnmapReadback(readback);
	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}
