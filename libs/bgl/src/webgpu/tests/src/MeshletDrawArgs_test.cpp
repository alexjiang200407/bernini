#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "idl/BaseTable.h"
#include "idl/Constants.h"
#include "idl/DispatchArgs.h"
#include "idl/DrawIndirectArgs.h"
#include "idl/InstanceVisibility.h"
#include "idl/Mesh.h"
#include "idl/Meshlet.h"
#include "idl/MeshletInstance.h"
#include "idl/Submesh.h"
#include "idl/VertexLayout.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include "types/SubmeshInstance.h"

#include <bgl/IGraphics.h>
#include <bgl/PsoType.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// Giving each PSO bucket its own run of meshlet records, which is what lets one drawIndirect per
// bucket cover a contiguous range. The chain is HistogramMeshlets -> PrefixSumInstances ->
// MeshletDrawArgs -> ExpandMeshlets, and every number in it is a GPU value: the CPU supplies the
// scene buffers and nothing else.
//
// Two buckets with *different* meshlet counts (2 and 1) is the smallest case that catches a wrong
// region base -- bucket 1's records must start after bucket 0's two, and its triangle only appears
// if firstVertex carried that offset into the draw.
TEST_CASE("Per-bucket meshlet regions come out of the scan", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	// Three triangles: two in the lower half for bucket 0's submesh, one up top for bucket 1's.
	// Nothing covers the centre.
	const float vertexData[27] = {
		-0.6f, -0.1f, 0.0f, -0.9f, -0.7f, 0.0f, -0.3f, -0.7f, 0.0f,  // left
		0.6f,  -0.1f, 0.0f, 0.3f,  -0.7f, 0.0f, 0.9f,  -0.7f, 0.0f,  // right
		0.0f,  0.9f,  0.0f, -0.3f, 0.3f,  0.0f, 0.3f,  0.3f,  0.0f,  // top
	};

	auto layout                     = idl::VertexLayout{};
	layout.attributes[0].semantic   = idl::VertexSemantic::kPosition;
	layout.attributes[0].format     = idl::VertexFormat::kFloat32x3;
	layout.attributes[0].byteOffset = 0;
	layout.attributeCount           = 1;
	layout.stride                   = 3 * sizeof(float);

	const auto makeSubmesh = [&](uint32_t meshletStart,
	                             uint32_t meshletCount,
	                             uint32_t mapStart,
	                             uint32_t dataWordStart,
	                             uint32_t indexStart,
	                             uint32_t vertexCount) {
		auto s           = idl::Submesh{};
		s.layout         = layout;
		s.meshlets.range = idl::Range{ .offsetStart = meshletStart };
		s.meshlets.count = meshletCount;
		s.vertexMap      = idl::Range{ .offsetStart = mapStart };
		s.vertexData     = idl::Range{ .offsetStart = dataWordStart };
		s.indices        = idl::Range{ .offsetStart = indexStart };
		s.vertexCount    = vertexCount;
		s.boundingCenter = glm::vec3(0.0f);
		s.boundingRadius = 2.0f;
		return s;
	};

	// vertexData offsets are in 4-byte words: the second submesh's six predecessors are 72 bytes.
	const std::array<idl::Submesh, 2> submeshes = {
		makeSubmesh(0, 2, 0, 0, 0, 6),
		makeSubmesh(2, 1, 6, 18, 6, 3),
	};

	const auto makeMeshlet = [](uint32_t relVertex, uint32_t relIndex) {
		auto m                 = idl::Meshlet{};
		m.relativeVertexOffset = relVertex;
		m.relativeIndexOffset  = relIndex;
		m.vertexCount          = 3;
		m.triangleCount        = 1;
		m.boundingCenter       = glm::vec3(0.0f);
		m.boundingRadius       = 2.0f;
		return m;
	};

	const std::array<idl::Meshlet, 3> meshlets = {
		makeMeshlet(0, 0),  // submesh 0, left
		makeMeshlet(3, 3),  // submesh 0, right
		makeMeshlet(0, 0),  // submesh 1, top -- the submesh's own ranges place it
	};

	auto mesh            = idl::Mesh{};
	mesh.transform       = glm::mat4(1.0f);
	mesh.submeshes.range = idl::Range{ .offsetStart = 0 };
	mesh.submeshes.count = 2;

	const auto makeInstance = [](uint32_t submeshIndex, uint32_t pso) {
		auto i         = SubmeshInstance{};
		i.meshInstance = idl::Entry{ .offset = 0 };
		i.submeshIndex = submeshIndex;
		i.material     = idl::Entry{ .offset = 0 };
		i.pso          = pso;
		return i;
	};

	const std::array<SubmeshInstance, 2> instances = { makeInstance(0, 0), makeInstance(1, 1) };

	const uint32_t vertexMap[9] = { 0, 1, 2, 3, 4, 5, 0, 1, 2 };
	const uint32_t indices[9]   = { 0, 1, 2, 0, 1, 2, 0, 1, 2 };

	const std::array<idl::InstanceVisibility, 2> visible = { { { 1u }, { 1u } } };

	// What the counting sort would have left: one instance per bucket, bucket 1 behind bucket 0.
	const uint32_t                   compacted[2] = { 0, 1 };
	std::array<uint32_t, c_PsoCount> instancePrefixSum{};
	instancePrefixSum.fill(2u);
	instancePrefixSum[0] = 1u;

	std::array<idl::DispatchArgs, c_PsoCount> dispatchArgs{};
	dispatchArgs[0] = idl::DispatchArgs{ 1u, 1u, 1u };
	dispatchArgs[1] = idl::DispatchArgs{ 1u, 1u, 1u };

	const auto make = [&](size_t bytes, const char* name) {
		return resources->CreateComputeBuffer(
			ComputeBufferDesc{}
				.SetElement<uint32_t>()
				.SetInitialCount(static_cast<uint32_t>(bytes / 4))
				.SetDebugName(name));
	};

	const auto instanceBuffer   = make(sizeof(instances), "instances");
	const auto meshBuffer       = make(sizeof(mesh), "meshes");
	const auto submeshBuffer    = make(sizeof(submeshes), "submeshes");
	const auto meshletBuffer    = make(sizeof(meshlets), "meshlets");
	const auto vertexMapBuffer  = make(sizeof(vertexMap), "vertexMap");
	const auto vertexDataBuffer = make(sizeof(vertexData), "vertexData");
	const auto indexBuffer      = make(sizeof(indices), "indices");
	const auto visibilityBuffer = make(sizeof(visible), "visibility");
	const auto compactedBuffer  = make(sizeof(compacted), "compactedInstances");
	const auto prefixSumBuffer  = make(sizeof(instancePrefixSum), "psoPrefixSum");
	const auto partitionBuffer  = make(sizeof(uint32_t), "partitionBase");
	const auto dispatchBuffer   = make(sizeof(dispatchArgs), "dispatchArgs");

	const auto meshletCountBuffer = make(c_PsoCount * sizeof(uint32_t), "meshletCounts");
	const auto drawArgsBuffer     = make(c_PsoCount * sizeof(idl::DrawIndirectArgs), "drawArgs");
	const auto recordBuffer       = make(3 * sizeof(idl::MeshletInstance), "meshletInstances");

	const auto kernelFor = [&](const char* module, const char* name) {
		return device->CreateComputeKernel(
			ComputePipelineDesc{}
				.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName(module)))
				.SetDebugName(name));
	};

	// An unbound handle reads descriptor slot zero, which is the first buffer created -- Dawn
	// rejects the command list for aliasing it read-write against its read-only binding.
	const auto bindDebug = [&](ComputeKernel& kernel) {
		auto found = kernel.uniforms.find("gDebug");
		if (found != kernel.uniforms.end())
		{
			found->second["buffer"] = make(64, "debug");
		}
	};

	auto  histogram            = kernelFor("HistogramMeshlets", "histogram-meshlets");
	auto& histData             = histogram["gUniforms"];
	histData["instanceBuffer"] = instanceBuffer;
	histData["meshBuffer"]     = meshBuffer;
	histData["submeshBuffer"]  = submeshBuffer;
	histData["visibility"]     = visibilityBuffer;
	histData["outBuffer"]      = meshletCountBuffer;

	auto scan                        = kernelFor("PrefixSumInstances", "prefix-sum-meshlets");
	scan["gUniforms"]["inOutBuffer"] = meshletCountBuffer;

	auto  drawArgsKernel         = kernelFor("MeshletDrawArgs", "meshlet-draw-args");
	auto& argsData               = drawArgsKernel["gUniforms"];
	argsData["meshletPrefixSum"] = meshletCountBuffer;
	argsData["drawArgs"]         = drawArgsBuffer;

	auto  expand                   = kernelFor("ExpandMeshlets", "expand-meshlets");
	auto& expandData               = expand["gExpand"];
	expandData["instanceBuffer"]   = instanceBuffer;
	expandData["meshBuffer"]       = meshBuffer;
	expandData["submeshBuffer"]    = submeshBuffer;
	expandData["meshletInstances"] = recordBuffer;
	expandData["drawArgs"]         = drawArgsBuffer;
	expandData["dispatchArgs"]     = dispatchBuffer;

	bindDebug(histogram);
	bindDebug(scan);
	bindDebug(drawArgsKernel);
	bindDebug(expand);

	auto& expansion                       = expand["expansionData"];
	expansion["baseTable"]                = idl::BaseTable::kPsoBucketed;
	expansion["partitionIndex"]           = 0u;
	expansion["compactedInstances"]       = compactedBuffer;
	expansion["psoPrefixSum"]             = prefixSumBuffer;
	expansion["transparentPartitionBase"] = partitionBuffer;

	const auto meshShader  = device->CreateShader("Forward_StaticMesh", "MSMain");
	const auto pixelShader = device->CreateShader("MultiModulePixel", "PSMain");

	auto gfxDesc = MeshletPipelineDesc{};
	gfxDesc.SetMeshShader(meshShader).SetPixelShader(pixelShader).AddRtvFormat(Format::RGBA8_UNORM);
	gfxDesc.renderState.rasterState.SetCullNone();

	auto gfx = device->CreateMeshletKernel(gfxDesc);

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
		ReadbackBufferDesc{ .byteSize  = sizeof(idl::DrawIndirectArgs) * c_PsoCount,
	                        .debugName = "args-readback" });
	const auto recordReadback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize  = 3 * sizeof(idl::MeshletInstance),
	                        .debugName = "record-readback" });

	float clear[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

	auto* wgpuList = static_cast<CommandList*>(list.Get());

	list->Open(queue.Get(), allocator.Get());
	list->WriteBuffer(instanceBuffer, instances.data(), 0, sizeof(instances));
	list->WriteBuffer(meshBuffer, &mesh, 0, sizeof(mesh));
	list->WriteBuffer(submeshBuffer, submeshes.data(), 0, sizeof(submeshes));
	list->WriteBuffer(meshletBuffer, meshlets.data(), 0, sizeof(meshlets));
	list->WriteBuffer(vertexMapBuffer, vertexMap, 0, sizeof(vertexMap));
	list->WriteBuffer(vertexDataBuffer, vertexData, 0, sizeof(vertexData));
	list->WriteBuffer(indexBuffer, indices, 0, sizeof(indices));
	list->WriteBuffer(visibilityBuffer, visible.data(), 0, sizeof(visible));
	list->WriteBuffer(compactedBuffer, compacted, 0, sizeof(compacted));
	list->WriteBuffer(prefixSumBuffer, instancePrefixSum.data(), 0, sizeof(instancePrefixSum));
	list->WriteBuffer(dispatchBuffer, dispatchArgs.data(), 0, sizeof(dispatchArgs));

	const auto dispatch = [&](ComputeKernel& kernel, uint32_t groups) {
		auto state   = ComputeState{};
		state.kernel = &kernel;
		list->SetComputeState(state);
		list->Dispatch(groups, 1, 1);
	};

	dispatch(histogram, 1);
	dispatch(scan, 1);
	dispatch(drawArgsKernel, 1);

	// One expansion dispatch per bucket, so psoIndex is rewritten between them -- which only works
	// because each SetComputeState takes a fresh uniform buffer from the pool.
	for (uint32_t pso = 0; pso < 2; ++pso)
	{
		expansion["psoIndex"] = pso;
		dispatch(expand, 1);
	}

	resources->ClearRtv(list.Get(), rtv, clear);
	wgpuList->BeginRenderPass(fb, viewport);
	wgpuList->SetGraphicsKernel(gfx);
	for (uint32_t pso = 0; pso < 2; ++pso)
	{
		wgpuList->DrawIndirect(drawArgsBuffer, pso * sizeof(idl::DrawIndirectArgs));
	}
	wgpuList->EndRenderPass();

	list->CopyTextureToReadback(readback, texture);
	list->CopyBufferToReadback(argsReadback, drawArgsBuffer);
	list->CopyBufferToReadback(recordReadback, recordBuffer);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	constexpr uint32_t c_Stride = idl::cVerticesPerMeshletRecord;

	const auto* args =
		static_cast<const idl::DrawIndirectArgs*>(resources->MapReadback(argsReadback));
	REQUIRE(args != nullptr);

	// Bucket 0 owns two records from the start; bucket 1 owns one, beginning after them.
	CHECK(args[0].firstVertex == 0u);
	CHECK(args[0].vertexCount == 2 * c_Stride);
	CHECK(args[1].firstVertex == 2 * c_Stride);
	CHECK(args[1].vertexCount == 1 * c_Stride);

	// An empty bucket still gets a valid record: zero vertices at the end of bucket 1's run.
	CHECK(args[2].firstVertex == 3 * c_Stride);
	CHECK(args[2].vertexCount == 0u);
	CHECK(args[2].instanceCount == 1u);
	resources->UnmapReadback(argsReadback);

	const auto* record =
		static_cast<const idl::MeshletInstance*>(resources->MapReadback(recordReadback));
	REQUIRE(record != nullptr);
	CHECK(record[0].instanceIndex == 0u);
	CHECK(record[0].meshletIndex == 0u);
	CHECK(record[1].instanceIndex == 0u);
	CHECK(record[1].meshletIndex == 1u);
	CHECK(record[2].instanceIndex == 1u);
	CHECK(record[2].meshletIndex == 0u);
	resources->UnmapReadback(recordReadback);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	const auto at = [&](uint32_t x, uint32_t y) { return mapped[(y * c_Size) + x]; };
	CHECK(at(12, 48) == 0xFF00FF00u);  // bucket 0, meshlet 0
	CHECK(at(51, 48) == 0xFF00FF00u);  // bucket 0, meshlet 1
	CHECK(at(32, 16) == 0xFF00FF00u);  // bucket 1, through its firstVertex offset
	CHECK(at(32, 32) == 0xFF0000FFu);

	resources->UnmapReadback(readback);
	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}
