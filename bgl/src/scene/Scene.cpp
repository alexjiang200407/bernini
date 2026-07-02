#include "scene/Scene.h"
#include "constants/constants.h"
#include "fg/FrameGraph.h"
#include "idl/Meshlet.h"
#include "idl/Vertex.h"
#include "types/SubmeshInstance.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <bgl/PsoType.h>
#include <core/math.h>
#include <numbers>

namespace bgl
{
	namespace
	{
		struct BufferInfo
		{
			std::string_view name;
		};

		// Order MUST stay in lockstep with Scene::GetGeometryBuffers() and with
		// ForwardPass's c_ForwardDataBuffers.
		static constexpr std::array<BufferInfo, 5> c_BufferInfo = { {
			{ "scene.submeshBuffer" },
			{ "scene.meshletBuffer" },
			{ "scene.vertexMapBuffer" },
			{ "scene.vertexDataBuffer" },
			{ "scene.indexBuffer" },
		} };

		// The interleaved vertex layout the procedural geometry emits: position,
		// normal, uv (no tangent), tightly packed at a 32-byte stride. This is
		// exactly the first 32 bytes of idl::Vertex, and is decoded on the GPU via
		// each submesh's VertexLayout descriptor.
		constexpr uint32_t c_ProceduralStride   = 32;
		constexpr uint32_t c_ProceduralVtxWords = c_ProceduralStride / 4;

		idl::VertexLayout
		MakeProceduralLayout()
		{
			auto layout           = idl::VertexLayout();
			layout.attributeCount = 3;
			layout.stride         = c_ProceduralStride;
			layout.attributes[0]  = { idl::VertexSemantic::kPosition,
				                      idl::VertexFormat::kFloat32x3,
				                      0 };
			layout.attributes[1]  = { idl::VertexSemantic::kNormal,
				                      idl::VertexFormat::kFloat32x3,
				                      12 };
			layout.attributes[2]  = { idl::VertexSemantic::kTexCoord0,
				                      idl::VertexFormat::kFloat32x2,
				                      24 };
			return layout;
		}

		// Interleave each vertex's position/normal/uv into the raw byte layout
		// above, returned as uint words for the StructuredBuffer<uint> data buffer.
		std::vector<uint32_t>
		PackVertices(std::span<const idl::Vertex> verts)
		{
			auto words = std::vector<uint32_t>(verts.size() * c_ProceduralVtxWords);
			for (size_t i = 0; i < verts.size(); ++i)
			{
				std::memcpy(&words[i * c_ProceduralVtxWords], &verts[i], c_ProceduralStride);
			}
			return words;
		}

		std::atomic<uint32_t> g_NextSceneId{ 0 };
	}

	Scene::Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager) :
		m_Desc(std::move(desc)), m_ResourceManager(std::move(resourceManager))
	{
		m_NamePrefix = std::format("s{}:", g_NextSceneId.fetch_add(1));

		const uint32_t maxSubmeshes =
			m_Desc.maxSubmeshes != 0 ? m_Desc.maxSubmeshes : m_Desc.maxMeshlets;
		const uint32_t maxVertexWords = m_Desc.maxVertexWords != 0 ?
		                                    m_Desc.maxVertexWords :
		                                    m_Desc.maxVertices * c_ProceduralVtxWords;

		// Geometry assets are CPU-only (they hold the shared submeshes descriptor +
		// refcount); the heavy data lives in the GPU range buffers below.
		m_GeomAssets.reset(m_Desc.maxGeom);

		{
			auto submeshBufferDesc      = RangeBufferDesc();
			submeshBufferDesc.maxCount  = maxSubmeshes;
			submeshBufferDesc.debugName = "Submesh Buffer";

			m_SubmeshBuffer.Init(std::move(submeshBufferDesc), m_ResourceManager);
		}

		{
			auto meshletBufferDesc      = RangeBufferDesc();
			meshletBufferDesc.maxCount  = m_Desc.maxMeshlets;
			meshletBufferDesc.debugName = "Meshlet Buffer";

			m_MeshletBuffer.Init(std::move(meshletBufferDesc), m_ResourceManager);
		}

		{
			auto vertexMapBufferDesc      = RangeBufferDesc();
			vertexMapBufferDesc.maxCount  = m_Desc.maxVertices;
			vertexMapBufferDesc.debugName = "Vertex Map Buffer";

			m_VertexMapBuffer.Init(std::move(vertexMapBufferDesc), m_ResourceManager);
		}

		{
			auto vertexDataBufferDesc      = RangeBufferDesc();
			vertexDataBufferDesc.maxCount  = maxVertexWords;
			vertexDataBufferDesc.debugName = "Vertex Data Buffer";

			m_VertexDataBuffer.Init(std::move(vertexDataBufferDesc), m_ResourceManager);
		}

		{
			auto indexBufferDesc      = RangeBufferDesc();
			indexBufferDesc.maxCount  = m_Desc.maxIndices;
			indexBufferDesc.debugName = "Index Buffer";

			m_IndexBuffer.Init(std::move(indexBufferDesc), m_ResourceManager);
		}
	}

	void
	Scene::Update(ICommandList* cmdList)
	{
		auto buffers = GetGeometryBuffers();
		std::apply([cmdList](auto&... buffer) { (..., buffer.Update(cmdList)); }, buffers);
	}

	void
	Scene::AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx)
	{
		std::vector<std::string> updateBuffers;
		ImportResources(fg, updateBuffers);

		PassDesc desc;
		desc.SetName(std::format("Scene Update {}", drawIdx));

		for (const std::string& buffer : updateBuffers)
		{
			desc.AddBufferArg(
				BufferArg{ buffer, BarrierSyncFlag::kCopy, BarrierAccessFlag::kCopyDest });
		}

		desc.SetExec([this](const PassContext& ctx) { Update(ctx.GetCommandList()); });

		fg.AddPass(std::move(desc));
	}

	void
	Scene::ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames)
	{
		resourceNames.reserve(resourceNames.size() + c_BufferInfo.size());

		auto   buffers = GetGeometryBuffers();
		size_t i       = 0;
		std::apply(
			[&](auto&... buffer) {
				(..., [&] {
					// Import every buffer (including the GPU-only compute buffer): the
					// Update pass declares them as copy-dest so the graph transitions
					// them, and the FrameGraph tracks the state each is left in.
					std::string name(c_BufferInfo[i++].name);
					fg.ImportBuffer(name, buffer.GetBufferHandle());
					resourceNames.push_back(std::move(name));
				}());
			},
			buffers);
	}

	GeomHandle
	Scene::AddCubeGeom()
	{
		try
		{
			static const idl::Vertex cubeVertices[] = {
				{ { -1, -1, -1 } },  // 0: left-bottom-back
				{ { 1, -1, -1 } },   // 1: right-bottom-back
				{ { 1, 1, -1 } },    // 2: right-top-back
				{ { -1, 1, -1 } },   // 3: left-top-back
				{ { -1, -1, 1 } },   // 4: left-bottom-front
				{ { 1, -1, 1 } },    // 5: right-bottom-front
				{ { 1, 1, 1 } },     // 6: right-top-front
				{ { -1, 1, 1 } }     // 7: left-top-front
			};

			static const uint32_t cubeIndices[] = { 4, 5, 6, 4, 6, 7, 1, 0, 3, 1, 3, 2,
				                                    0, 4, 7, 0, 7, 3, 5, 1, 2, 5, 2, 6,
				                                    7, 6, 2, 7, 2, 3, 0, 1, 5, 0, 5, 4 };

			const auto vertexWords      = PackVertices(cubeVertices);
			const auto baseVertexGlobal = m_VertexDataBuffer.Add(vertexWords);

			auto mapIndices = std::vector<uint32_t>(std::size(cubeVertices));
			for (uint32_t i = 0; i < mapIndices.size(); ++i)
			{
				mapIndices[i] = i;
			}

			const auto baseMapGlobal   = m_VertexMapBuffer.Add(mapIndices);
			const auto baseIndexGlobal = m_IndexBuffer.Add(cubeIndices);

			auto m = idl::Meshlet();

			m.relativeVertexOffset = 0;
			m.vertexCount          = static_cast<uint8_t>(std::size(cubeVertices));

			m.relativeIndexOffset = 0;
			m.triangleCount       = static_cast<uint8_t>(std::size(cubeIndices)) / 3;

			m.boundingCenter = glm::vec3{ 0.0f };
			m.boundingRadius = glm::sqrt(3.0f);

			const auto meshletSpan       = std::span<const idl::Meshlet>(&m, 1);
			const auto baseMeshletGlobal = m_MeshletBuffer.Add(meshletSpan);

			auto submesh        = idl::Submesh();
			submesh.layout      = MakeProceduralLayout();
			submesh.meshlets    = baseMeshletGlobal;
			submesh.vertexMap   = baseMapGlobal;
			submesh.vertexData  = baseVertexGlobal;
			submesh.indices     = baseIndexGlobal;
			submesh.vertexCount = static_cast<uint32_t>(std::size(cubeVertices));

			const auto submeshSpan       = std::span<const idl::Submesh>(&submesh, 1);
			const auto baseSubmeshGlobal = m_SubmeshBuffer.Add(submeshSpan);

			auto asset              = GeomAsset();
			asset.submeshes         = baseSubmeshGlobal;
			asset.totalMeshletCount = 1;

			auto retVal     = GeomHandle();
			retVal.handle   = m_GeomAssets.allocate_and_emplace(asset);
			retVal.geomType = GeomType::kStaticMesh;

			return retVal;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	GeomHandle
	Scene::AddSphereGeom(uint32_t xSegments, uint32_t ySegments, float radius)
	{
		try
		{
			std::vector<idl::Vertex> sphereVerts;
			std::vector<uint32_t>    sphereIndices;

			for (uint32_t y = 0u; y <= ySegments; ++y)
			{
				for (uint32_t x = 0u; x <= xSegments; ++x)
				{
					constexpr auto pi       = std::numbers::pi_v<float>;
					float          xSegment = static_cast<float>(x) / static_cast<float>(xSegments);
					float          ySegment = static_cast<float>(y) / static_cast<float>(ySegments);
					float          xPos = std::cos(xSegment * 2.0f * pi) * std::sin(ySegment * pi);
					float          yPos = std::cos(ySegment * pi);
					float          zPos = std::sin(xSegment * 2.0f * pi) * std::sin(ySegment * pi);

					auto v   = idl::Vertex();
					v.pos    = glm::vec3(xPos, yPos, zPos) * radius;
					v.normal = glm::normalize(v.pos);
					v.uv     = glm::vec2(xSegment, ySegment);
					sphereVerts.push_back(v);
				}
			}

			for (uint32_t y = 0u; y < ySegments; ++y)
			{
				for (uint32_t x = 0u; x < xSegments; ++x)
				{
					sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x);
					sphereIndices.push_back(y * (xSegments + 1u) + x);
					sphereIndices.push_back(y * (xSegments + 1u) + x + 1u);

					sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x);
					sphereIndices.push_back(y * (xSegments + 1u) + x + 1u);
					sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x + 1u);
				}
			}

			const auto vertexWords      = PackVertices(sphereVerts);
			const auto baseVertexGlobal = m_VertexDataBuffer.Add(vertexWords);

			auto                  meshlets = std::vector<idl::Meshlet>();
			std::vector<uint32_t> vertexMap;
			std::vector<uint32_t> localIndices;

			const uint32_t totalTriangles = static_cast<uint32_t>(sphereIndices.size() / 3u);
			uint32_t       trianglesDone  = 0u;

			while (trianglesDone < totalTriangles)
			{
				auto meshlet                 = idl::Meshlet();
				meshlet.relativeVertexOffset = static_cast<uint32_t>(vertexMap.size());
				meshlet.relativeIndexOffset  = static_cast<uint32_t>(localIndices.size());

				std::unordered_map<uint32_t, uint32_t> localRemap;
				uint32_t                               localVertexCount   = 0u;
				uint32_t                               localTriangleCount = 0u;

				while (trianglesDone < totalTriangles)
				{
					const uint32_t triBase = trianglesDone * 3u;
					const uint32_t tri[3]  = { sphereIndices[triBase],
						                       sphereIndices[triBase + 1u],
						                       sphereIndices[triBase + 2u] };

					uint32_t newVertices = 0u;
					for (uint32_t i = 0u; i < 3u; ++i)
					{
						if (!localRemap.contains(tri[i]))
						{
							++newVertices;
						}
					}

					if (localVertexCount + newVertices > c_MaxVerticesPerMeshlet ||
					    localTriangleCount + 1u > c_MaxPrimsPerMeshlet)
					{
						break;
					}

					for (uint32_t i = 0u; i < 3u; ++i)
					{
						const uint32_t geomVertexIdx = tri[i];
						if (!localRemap.contains(geomVertexIdx))
						{
							localRemap[geomVertexIdx] = localVertexCount++;
							vertexMap.push_back(geomVertexIdx);
						}
						localIndices.push_back(localRemap[geomVertexIdx]);
					}

					++localTriangleCount;
					++trianglesDone;
				}

				meshlet.vertexCount   = static_cast<uint16_t>(localVertexCount);
				meshlet.triangleCount = static_cast<uint16_t>(localTriangleCount);

				auto minBound = glm::vec3(std::numeric_limits<float>::max());
				auto maxBound = glm::vec3(std::numeric_limits<float>::lowest());
				for (const auto& [geomVertexIdx, localIdx] : localRemap)
				{
					minBound = glm::min(minBound, sphereVerts[geomVertexIdx].pos);
					maxBound = glm::max(maxBound, sphereVerts[geomVertexIdx].pos);
				}
				meshlet.boundingCenter = (minBound + maxBound) * 0.5f;
				meshlet.boundingRadius = glm::distance(maxBound, meshlet.boundingCenter);

				meshlets.push_back(meshlet);
			}

			const auto baseMapGlobal     = m_VertexMapBuffer.Add(vertexMap);
			const auto baseIndexGlobal   = m_IndexBuffer.Add(localIndices);
			const auto baseMeshletGlobal = m_MeshletBuffer.Add(meshlets);

			auto submesh        = idl::Submesh();
			submesh.layout      = MakeProceduralLayout();
			submesh.meshlets    = baseMeshletGlobal;
			submesh.vertexMap   = baseMapGlobal;
			submesh.vertexData  = baseVertexGlobal;
			submesh.indices     = baseIndexGlobal;
			submesh.vertexCount = static_cast<uint32_t>(sphereVerts.size());

			const auto submeshSpan       = std::span<const idl::Submesh>(&submesh, 1);
			const auto baseSubmeshGlobal = m_SubmeshBuffer.Add(submeshSpan);

			auto asset              = GeomAsset();
			asset.submeshes         = baseSubmeshGlobal;
			asset.totalMeshletCount = static_cast<uint32_t>(meshlets.size());

			auto retVal     = GeomHandle();
			retVal.handle   = m_GeomAssets.allocate_and_emplace(asset);
			retVal.geomType = GeomType::kStaticMesh;

			return retVal;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	void
	Scene::DeleteGeom(GeomHandle geom)
	{
		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError("GeomHandle passed to DeleteGeom must be of type kStaticMesh");
		}

		if (!m_GeomAssets.valid(geom.handle.index, geom.handle.generation))
		{
			throw SceneError("GeomHandle passed to DeleteGeom refers to a deleted or unknown geom");
		}

		const GeomAsset& asset = m_GeomAssets[geom.handle.index];
		if (asset.refCount != 0)
		{
			throw SceneError(
				std::format(
					"Cannot delete geom still referenced by {} live mesh instance(s)",
					asset.refCount));
		}

		// The geometry's per-part ranges live on each Submesh, so free them per
		// submesh before releasing the submesh range itself.
		const uint32_t submeshRoot  = asset.submeshes.range.offsetStart;
		const uint32_t submeshCount = asset.submeshes.count;
		for (uint32_t i = 0; i < submeshCount; ++i)
		{
			const auto& submesh = m_SubmeshBuffer.AtIndex(submeshRoot + i);

			m_VertexDataBuffer.EraseByIndex(submesh.vertexData.offsetStart);
			m_VertexMapBuffer.EraseByIndex(submesh.vertexMap.offsetStart);
			m_IndexBuffer.EraseByIndex(submesh.indices.offsetStart);
			m_MeshletBuffer.EraseByIndex(submesh.meshlets.range.offsetStart);
		}

		m_SubmeshBuffer.EraseByIndex(submeshRoot);
		m_GeomAssets.release_slot(geom.handle.index);
	}
}
