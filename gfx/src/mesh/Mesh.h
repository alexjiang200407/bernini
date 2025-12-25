#pragma once
#include "buffer/DynamicVertexBuffer.h"
#include "shader_reflect/ShaderInput.h"

namespace gfx
{
	class Mesh final
	{
	private:
		struct SharedData
		{
			uint32_t            indexCount{};
			DynamicVertexBuffer vertexBuf{};
			nvrhi::BufferHandle indexBuffer{};
		};

		static std::shared_ptr<SharedData>
		CreateSharedData()
		{
			return std::make_shared<SharedData>();
		}

	private:
		Mesh(
			std::shared_ptr<SharedData> sharedData,
			nvrhi::DeviceHandle         device,
			std::string_view            shaderPath);

	public:
		void
		AttachVertexLayout(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept;

		uint32_t
		GetIndexCount() const noexcept;

		void
		AttachVertexShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept;

		void
		AttachMesh(nvrhi::GraphicsState& state) const noexcept;

	private:
		nvrhi::InputLayoutHandle          m_vertexLayout;
		nvrhi::ShaderHandle               m_vertexShader;
		std::shared_ptr<const SharedData> m_sharedMeshData;

		friend class MeshFactory;
	};
}
