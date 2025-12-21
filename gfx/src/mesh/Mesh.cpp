#include "mesh/Mesh.h"
#include <core/file/file.h>

namespace gfx
{
	Mesh::Mesh(
		std::shared_ptr<SharedData> sharedData,
		nvrhi::DeviceHandle         device,
		std::string_view            vertexShaderPath) : m_sharedMeshData{ sharedData }
	{
		auto vertexShaderData = core::file::readFileBytes(vertexShaderPath);
		m_vertexShader        = device->createShader(
            nvrhi::ShaderDesc{}
                .setShaderType(nvrhi::ShaderType::Vertex)
                .setDebugName(vertexShaderPath.data()),
            vertexShaderData.data(),
            vertexShaderData.size());
		m_vertexLayout = m_sharedMeshData->vertexBuf.GenerateVertexLayout(device, m_vertexShader);
	}

	void
	Mesh::AttachVertexLayout(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept
	{
		pipelineDesc.setInputLayout(m_vertexLayout);
	}

	uint32_t
	Mesh::GetIndexCount() const noexcept
	{
		return m_sharedMeshData->indexCount;
	}

	void
	Mesh::AttachVertexShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept
	{
		pipelineDesc.setVertexShader(m_vertexShader);
	}

	void
	Mesh::AttachMesh(nvrhi::GraphicsState& state) const noexcept
	{
		nvrhi::VertexBufferBinding vbufBinding;
		vbufBinding.buffer = m_sharedMeshData->vertexBuf;
		vbufBinding.slot   = 0;
		vbufBinding.offset = 0;

		nvrhi::IndexBufferBinding ibufBinding;
		ibufBinding.buffer = m_sharedMeshData->indexBuffer;
		ibufBinding.format = nvrhi::Format::R16_UINT;
		ibufBinding.offset = 0;

		state.addVertexBuffer(vbufBinding);
		state.setIndexBuffer(ibufBinding);
	}
}
