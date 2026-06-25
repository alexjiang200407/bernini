#include "scene/Scene.h"
#include "db/Meshlet.h"
#include "db/Vertex.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"

namespace bgl
{
	Scene::Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager) :
		m_Desc(std::move(desc)), m_ResourceManager(std::move(resourceManager))
	{
		{
			PackedBufferDesc instanceBufferDesc;
			instanceBufferDesc.maxCount  = m_Desc.maxInstances;
			instanceBufferDesc.debugName = "Instance Buffer";
			instanceBufferDesc.blockSize = sizeof(db::BaseInstance) * 256;

			m_InstanceBuffer.Init(std::move(instanceBufferDesc), m_ResourceManager);
		}

		{
			EntryBufferDesc staticMeshInstanceBufferDesc;
			staticMeshInstanceBufferDesc.maxCount  = m_Desc.maxInstances;
			staticMeshInstanceBufferDesc.debugName = "Static Mesh Instance Buffer";
			staticMeshInstanceBufferDesc.blockSize = sizeof(db::StaticMeshInstance) * 256;

			m_StaticMeshInstanceBuffer.Init(
				std::move(staticMeshInstanceBufferDesc),
				m_ResourceManager);
		}

		{
			EntryBufferDesc staticGeomBufferDesc;
			staticGeomBufferDesc.maxCount  = m_Desc.maxGeom;
			staticGeomBufferDesc.debugName = "Static Mesh Buffer";
			staticGeomBufferDesc.blockSize = sizeof(db::StaticGeom) * 128;

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
	}

	void
	Scene::TransitionAll(
		ICommandList*    cmdList,
		EntryBufferState prevState,
		EntryBufferState newState)
	{
		auto buffers = GetAllBuffers();
		std::apply(
			[cmdList, prevState, newState](auto&... buffer) {
				(
					[cmdList, prevState, newState](auto& b) {
						if constexpr (is_entry_buffer_v<decltype(b)>)
						{
							b.Transition(cmdList, prevState, newState);
						}
					}(buffer),
					...);
			},
			buffers);
	}

	void
	Scene::TransitionAll(
		ICommandList*    cmdList,
		RangeBufferState prevState,
		RangeBufferState newState)
	{
		auto buffers = GetAllBuffers();
		std::apply(
			[cmdList, prevState, newState](auto&... buffer) {
				(
					[cmdList, prevState, newState](auto& b) {
						if constexpr (is_range_buffer_v<decltype(b)>)
						{
							b.Transition(cmdList, prevState, newState);
						}
					}(buffer),
					...);
			},
			buffers);
	}

	void
	Scene::TransitionAll(
		ICommandList*     cmdList,
		PackedBufferState prevState,
		PackedBufferState newState)
	{
		auto buffers = GetAllBuffers();
		std::apply(
			[cmdList, prevState, newState](auto&... buffer) {
				(
					[cmdList, prevState, newState](auto& b) {
						if constexpr (is_packed_buffer_v<decltype(b)>)
						{
							b.Transition(cmdList, prevState, newState);
						}
					}(buffer),
					...);
			},
			buffers);
	}

	void
	Scene::AttachUniforms(Uniforms& uniforms)
	{
		m_FirstFrame = false;

		auto SetEntryBuffer = [&uniforms](const std::string& key, DescriptorHandle handle) {
			auto valid =
				uniforms[key]["entryBuffer"].IsValid() &&
				uniforms[key]["entryBuffer"].GetValueType() == UniformValueType::kDescriptorHandle;

			if (valid)
			{
				uniforms[key]["entryBuffer"] = handle;
			}

			return valid;
		};

		auto SetRangeBuffer = [&uniforms](const std::string& key, DescriptorHandle handle) {
			auto valid =
				uniforms[key]["rangeBuffer"].IsValid() &&
				uniforms[key]["rangeBuffer"].GetValueType() == UniformValueType::kDescriptorHandle;

			if (valid)
			{
				uniforms[key]["rangeBuffer"] = handle;
			}

			return valid;
		};

		auto SetPackedBuffer = [&uniforms](const std::string& key, DescriptorHandle handle) {
			auto valid =
				uniforms[key]["packedBuffer"].IsValid() &&
				uniforms[key]["packedBuffer"].GetValueType() == UniformValueType::kDescriptorHandle;

			if (valid)
			{
				uniforms[key]["packedBuffer"] = handle;
			}

			return valid;
		};

		if (!SetPackedBuffer("instanceBuffer", m_InstanceBuffer.GetDescriptorHandle()))
		{
			gfatal("instanceBuffer key doesn't exist in uniforms. Most likely an error");
		}

		// Bind static mesh buffers only if pipeline expects them
		{
			SetEntryBuffer("meshBuffer", m_StaticMeshInstanceBuffer.GetDescriptorHandle());
			SetEntryBuffer("geomBuffer", m_StaticGeom.GetDescriptorHandle());
		}

		if (!SetRangeBuffer("meshletBuffer", m_MeshletBuffer.GetDescriptorHandle()))
		{
			gfatal("meshletBuffer key doesn't exist in uniforms. Most likely an error");
		}

		if (!SetRangeBuffer("vertexMapBuffer", m_VertexMapBuffer.GetDescriptorHandle()))
		{
			gfatal("vertexMapBuffer key doesn't exist in uniforms. Most likely an error");
		}

		if (!SetRangeBuffer("vertexBuffer", m_VertexBuffer.GetDescriptorHandle()))
		{
			gfatal("vertexBuffer key doesn't exist in uniforms. Most likely an error");
		}

		if (!SetRangeBuffer("indexBuffer", m_IndexBuffer.GetDescriptorHandle()))
		{
			gfatal("indexBuffer key doesn't exist in uniforms. Most likely an error");
		}
	}

	GeomHandle
	Scene::AddCubeGeom()
	{
		static const db::Vertex cubeVertices[] = {
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

		const auto baseVertexGlobal = m_VertexBuffer.Add(std::span(cubeVertices));
		auto       mapIndices       = std::vector<uint32_t>(std::size(cubeVertices));
		for (uint32_t i = 0; i < mapIndices.size(); ++i)
		{
			mapIndices[i] = i;
		}

		const auto baseMapGlobal   = m_VertexMapBuffer.Add(mapIndices);
		const auto baseIndexGlobal = m_IndexBuffer.Add(cubeIndices);

		auto m = db::Meshlet();

		m.relativeVertexOffset = 0;
		m.vertexCount          = static_cast<uint8_t>(std::size(cubeVertices));

		m.relativeIndexOffset = 0;
		m.triangleCount       = static_cast<uint8_t>(std::size(cubeIndices)) / 3;

		m.boundingCenter = glm::vec3{ 0.0f };
		m.boundingRadius = glm::sqrt(3.0f);

		const auto meshletSpan       = std::span<const db::Meshlet>(&m, 1);
		const auto baseMeshletGlobal = m_MeshletBuffer.Add(meshletSpan);

		auto staticMesh      = db::StaticGeom();
		staticMesh.vertices  = baseVertexGlobal;
		staticMesh.indices   = baseIndexGlobal;
		staticMesh.meshlets  = baseMeshletGlobal;
		staticMesh.vertexMap = baseMapGlobal;

		auto retVal     = GeomHandle();
		retVal.handle   = m_StaticGeom.Add(std::move(staticMesh));
		retVal.geomType = GeomType::kStaticMesh;

		return retVal;
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

		auto staticMeshInstance      = db::StaticMeshInstance();
		staticMeshInstance.base      = geom.handle;
		staticMeshInstance.transform = transform;

		auto staticMeshInstanceHandle = m_StaticMeshInstanceBuffer.Add(staticMeshInstance);

		auto instance             = db::BaseInstance();
		instance.meshInstance     = staticMeshInstanceHandle;
		instance.materialInstance = material.handle;

		auto instanceHandle    = MeshInstanceHandle();
		instanceHandle.handle  = m_InstanceBuffer.Add(std::move(instance));
		instanceHandle.psoType = GetPsoFromGeomAndMaterial(geom.geomType, material.materialType);

		return instanceHandle;
	}
}
