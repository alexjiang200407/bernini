#pragma once
#include "idl/idl.h"
#include "resource/ResourceManager.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/RangeBuffer.h"
#include "types/BaseInstance.h"
#include <bgl/IScene.h>

namespace bgl
{
	class ICommandList;
	class FrameGraph;

	struct GeomMeta
	{
		uint32_t refCount = 0;
	};

	class Scene : public core::RefCounter<IScene>
	{
	public:
		Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager);
		~Scene() noexcept override { logger::trace("~Scene"); }
		Scene(const Scene&) noexcept = delete;
		Scene(Scene&&) noexcept      = delete;

		Scene&
		operator=(const Scene&) noexcept = delete;

		Scene&
		operator=(Scene&&) noexcept = delete;

		const SceneDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		auto
		GetAllBuffers()
		{
			return std::tie(
				m_InstanceBuffer,
				m_StaticMeshInstanceBuffer,
				m_StaticGeom,
				m_MeshletBuffer,
				m_VertexMapBuffer,
				m_VertexBuffer,
				m_IndexBuffer);
		}
		[[nodiscard]] const std::string&
		ResourceNamespace() const noexcept
		{
			return m_NamePrefix;
		}

		std::vector<std::string>
		ImportResources(FrameGraph& fg);

		void
		Update(ICommandList* cmdList);

		[[nodiscard]]
		bool
		IsFirstFrame() const
		{
			return m_FirstFrame;
		}

		[[nodiscard]]
		uint32_t
		GetInstanceCount() const noexcept override
		{
			return m_InstanceBuffer.Size();
		}

		GeomHandle
		AddCubeGeom() override;

		GeomHandle
		AddSphereGeom(uint32_t xSegments, uint32_t ySegments, float radius) override;

		MeshInstanceHandle
		CreateStaticMeshInstance(GeomHandle geom, MaterialHandle material, glm::mat4 transform)
			override;

		void
		DeleteMeshInstance(MeshInstanceHandle instance) override;

		void
		DeleteGeom(GeomHandle geom) override;

	private:
		static constexpr std::array<std::string_view, 7> c_BufferNames = { {
			"scene.instanceBuffer",
			"scene.meshInstanceBuffer",
			"scene.geomBuffer",
			"scene.meshletBuffer",
			"scene.vertexMapBuffer",
			"scene.vertexBuffer",
			"scene.indexBuffer",
		} };

		SceneDesc   m_Desc;
		std::string m_NamePrefix;

		PackedBuffer<BaseInstance>             m_InstanceBuffer;
		EntryBuffer<idl::StaticMeshInstance>   m_StaticMeshInstanceBuffer;
		EntryBuffer<idl::StaticGeom, GeomMeta> m_StaticGeom;
		RangeBuffer<idl::Meshlet>              m_MeshletBuffer;
		RangeBuffer<uint32_t>                  m_VertexMapBuffer;
		RangeBuffer<idl::Vertex>               m_VertexBuffer;
		RangeBuffer<uint32_t>                  m_IndexBuffer;
		bool                                   m_FirstFrame = true;

		core::SharedRef<IResourceManager> m_ResourceManager;
	};
}
