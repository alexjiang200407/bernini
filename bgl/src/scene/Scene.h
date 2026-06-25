#pragma once
#include "db/db.h"
#include "resource/ResourceManager.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/RangeBuffer.h"
#include <bgl/IScene.h>

namespace bgl
{
	class ICommandList;
	class Uniforms;

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

		void
		TransitionAll(ICommandList* cmdList, EntryBufferState prevState, EntryBufferState newState);

		void
		TransitionAll(ICommandList* cmdList, RangeBufferState prevState, RangeBufferState newState);

		void
		TransitionAll(
			ICommandList*     cmdList,
			PackedBufferState prevState,
			PackedBufferState newState);

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
		GetInstanceCount() const
		{
			return 1;
		}

		void
		AttachUniforms(Uniforms& uniforms);

		GeomHandle
		AddCubeGeom() override;

		MeshInstanceHandle
		CreateStaticMeshInstance(GeomHandle geom, MaterialHandle material, glm::mat4 transform)
			override;

	private:
		SceneDesc m_Desc;

		PackedBuffer<db::BaseInstance>      m_InstanceBuffer;
		EntryBuffer<db::StaticMeshInstance> m_StaticMeshInstanceBuffer;
		EntryBuffer<db::StaticGeom>         m_StaticGeom;
		RangeBuffer<db::Meshlet>            m_MeshletBuffer;
		RangeBuffer<uint32_t>               m_VertexMapBuffer;
		RangeBuffer<db::Vertex>             m_VertexBuffer;
		RangeBuffer<uint32_t>               m_IndexBuffer;
		bool                                m_FirstFrame = true;

		core::SharedRef<IResourceManager> m_ResourceManager;
	};
}
