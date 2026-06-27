#include "scene/Scene.h"
#include "constants/constants.h"
#include "fg/FrameGraph.h"
#include "idl/Meshlet.h"
#include "idl/Vertex.h"
#include "types/BaseInstance.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <numbers>

namespace bgl
{
	namespace
	{
		// Hands each Scene a process-unique namespace so two scenes drawn in the same
		// frame import their buffers under distinct graph names.
		std::atomic<uint32_t> g_NextSceneId{ 0 };
	}

	Scene::Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager) :
		m_Desc(std::move(desc)), m_ResourceManager(std::move(resourceManager))
	{
		m_NamePrefix = std::format("s{}:", g_NextSceneId.fetch_add(1));

		{
			PackedBufferDesc instanceBufferDesc;
			instanceBufferDesc.maxCount  = m_Desc.maxInstances;
			instanceBufferDesc.debugName = "Instance Buffer";
			instanceBufferDesc.blockSize = sizeof(BaseInstance) * 256;

			m_InstanceBuffer.Init(std::move(instanceBufferDesc), m_ResourceManager);
		}

		{
			EntryBufferDesc staticMeshInstanceBufferDesc;
			staticMeshInstanceBufferDesc.maxCount  = m_Desc.maxInstances;
			staticMeshInstanceBufferDesc.debugName = "Static Mesh Instance Buffer";
			staticMeshInstanceBufferDesc.blockSize = sizeof(idl::StaticMeshInstance) * 256;

			m_StaticMeshInstanceBuffer.Init(
				std::move(staticMeshInstanceBufferDesc),
				m_ResourceManager);
		}

		{
			EntryBufferDesc staticGeomBufferDesc;
			staticGeomBufferDesc.maxCount  = m_Desc.maxGeom;
			staticGeomBufferDesc.debugName = "Static Mesh Buffer";
			staticGeomBufferDesc.blockSize = sizeof(idl::StaticGeom) * 128;

			m_StaticGeom.Init(std::move(staticGeomBufferDesc), m_ResourceManager);
		}

		{
			RangeBufferDesc meshletBufferDesc;
			meshletBufferDesc.maxCount  = m_Desc.maxMeshlets;
			meshletBufferDesc.debugName = "Meshlet Buffer";

			m_MeshletBuffer.Init(std::move(meshletBufferDesc), m_ResourceManager);
		}

		{
			RangeBufferDesc vertexMapBufferDesc;
			vertexMapBufferDesc.maxCount  = m_Desc.maxVertices;
			vertexMapBufferDesc.debugName = "Vertex Map Buffer";

			m_VertexMapBuffer.Init(std::move(vertexMapBufferDesc), m_ResourceManager);
		}

		{
			RangeBufferDesc vertexBufferDesc;
			vertexBufferDesc.maxCount  = m_Desc.maxVertices;
			vertexBufferDesc.debugName = "Vertex Buffer";

			m_VertexBuffer.Init(std::move(vertexBufferDesc), m_ResourceManager);
		}

		{
			RangeBufferDesc indexBufferDesc;
			indexBufferDesc.maxCount  = m_Desc.maxIndices;
			indexBufferDesc.debugName = "Index Buffer";

			m_IndexBuffer.Init(std::move(indexBufferDesc), m_ResourceManager);
		}
	}

	void
	Scene::Update(ICommandList* cmdList)
	{
		auto buffers = GetAllBuffers();
		std::apply([cmdList](auto&... buffer) { (..., buffer.Update(cmdList)); }, buffers);

		// Update runs (deferred) during the graph's execute, after all imports for
		// this frame; flipping here keeps the first-frame initial state correct.
		m_FirstFrame = false;
	}

	std::vector<std::string>
	Scene::ImportResources(FrameGraph& fg)
	{
		// All scene buffers end a frame in the shader-resource state (the render
		// pass reads them) and start the very first frame freshly created (kNone).
		const AccessState initial =
			m_FirstFrame ?
				AccessState{} :
				AccessState{ BarrierSyncFlag::kVertexShader, BarrierAccessFlag::kShaderResource };

		std::vector<std::string> names;
		names.reserve(c_BufferNames.size());

		auto   buffers = GetAllBuffers();
		size_t i       = 0;
		std::apply(
			[&](auto&... buffer) {
				(..., [&] {
					std::string name(c_BufferNames[i++]);
					fg.ImportBuffer(name, buffer.GetBufferHandle(), initial);
					names.push_back(std::move(name));
				}());
			},
			buffers);

		return names;
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

			const auto baseVertexGlobal = m_VertexBuffer.Add(cubeVertices);
			auto       mapIndices       = std::vector<uint32_t>(std::size(cubeVertices));
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

			auto staticGeom      = idl::StaticGeom();
			staticGeom.vertices  = baseVertexGlobal;
			staticGeom.indices   = baseIndexGlobal;
			staticGeom.meshlets  = baseMeshletGlobal;
			staticGeom.vertexMap = baseMapGlobal;

			auto retVal     = GeomHandle();
			retVal.handle   = m_StaticGeom.Add(std::move(staticGeom));
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

			const auto baseVertexGlobal = m_VertexBuffer.Add(sphereVerts);

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

			auto staticGeom      = idl::StaticGeom();
			staticGeom.vertices  = baseVertexGlobal;
			staticGeom.vertexMap = baseMapGlobal;
			staticGeom.indices   = baseIndexGlobal;
			staticGeom.meshlets  = baseMeshletGlobal;

			auto retVal     = GeomHandle();
			retVal.handle   = m_StaticGeom.Add(std::move(staticGeom));
			retVal.geomType = GeomType::kStaticMesh;

			return retVal;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	MeshInstanceHandle
	Scene::CreateStaticMeshInstance(GeomHandle geom, MaterialHandle material, glm::mat4 transform)
	{
		if (!material.IsValid())
		{
			throw SceneError("Invalid MaterialHandle passed to CreateStaticMeshInstance");
		}

		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError(
				"GeomHandle passed to CreateStaticMeshInstance must be of type kStaticMesh");
		}

		if (!m_StaticGeom.IsValid(geom.handle))
		{
			throw SceneError(
				"GeomHandle passed to CreateStaticMeshInstance has expired or is invalid");
		}

		try
		{
			auto staticMeshInstance      = idl::StaticMeshInstance();
			staticMeshInstance.base      = geom.handle;
			staticMeshInstance.transform = transform;

			auto staticMeshInstanceHandle = m_StaticMeshInstanceBuffer.Add(staticMeshInstance);

			auto instance             = BaseInstance();
			instance.meshInstance     = staticMeshInstanceHandle;
			instance.materialInstance = material.handle;

			auto instanceHandle   = MeshInstanceHandle();
			instanceHandle.handle = m_InstanceBuffer.Add(std::move(instance));
			instanceHandle.psoType =
				GetPsoFromGeomAndMaterial(geom.geomType, material.materialType);

			++m_StaticGeom.MetaAt(geom.handle.index).refCount;

			return instanceHandle;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	void
	Scene::DeleteMeshInstance(MeshInstanceHandle instance)
	{
		if (!instance.IsValid() || !m_InstanceBuffer.IsValid(instance.handle))
		{
			throw SceneError(
				"MeshInstanceHandle passed to DeleteMeshInstance is invalid or already removed");
		}

		const auto& baseInstance = m_InstanceBuffer[instance.handle];

		// Walk instance -> static-mesh-instance -> geom to drop the geom's
		// reference. These links are internal invariants, so a broken one is a
		// bgl logic error rather than caller misuse.
		const uint32_t staticMeshInstanceIndex = baseInstance.meshInstance.offset;
		gassert(
			m_StaticMeshInstanceBuffer.IsIndexValid(staticMeshInstanceIndex),
			"Mesh instance references a missing static mesh instance");

		const uint32_t geomIndex =
			m_StaticMeshInstanceBuffer.AtIndex(staticMeshInstanceIndex).base.offset;
		gassert(
			m_StaticGeom.IsIndexValid(geomIndex),
			"Static mesh instance references a missing geom");

		auto& geomMeta = m_StaticGeom.MetaAt(geomIndex);
		gassert(geomMeta.refCount > 0, "Geom reference count underflow in DeleteMeshInstance");
		--geomMeta.refCount;

		m_StaticMeshInstanceBuffer.EraseByIndex(staticMeshInstanceIndex);
		m_InstanceBuffer.Erase(instance.handle);
	}

	void
	Scene::DeleteGeom(GeomHandle geom)
	{
		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError("GeomHandle passed to DeleteGeom must be of type kStaticMesh");
		}

		if (!m_StaticGeom.IsValid(geom.handle))
		{
			throw SceneError("GeomHandle passed to DeleteGeom refers to a deleted or unknown geom");
		}

		const uint32_t refCount = m_StaticGeom.MetaAt(geom.handle.index).refCount;
		if (refCount != 0)
		{
			throw SceneError(
				std::format(
					"Cannot delete geom still referenced by {} live mesh instance(s)",
					refCount));
		}

		const auto& staticGeom = m_StaticGeom[geom.handle];

		m_VertexBuffer.EraseByIndex(staticGeom.vertices.offsetStart);
		m_VertexMapBuffer.EraseByIndex(staticGeom.vertexMap.offsetStart);
		m_IndexBuffer.EraseByIndex(staticGeom.indices.offsetStart);
		m_MeshletBuffer.EraseByIndex(staticGeom.meshlets.range.offsetStart);

		m_StaticGeom.Erase(geom.handle);
	}
}
