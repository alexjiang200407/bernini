#include "drawable/Drawable.h"
#include "material/Material.h"
#include "mesh/Mesh.h"

namespace gfx
{
	void
	Drawable::AttachVertexLayout(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept
	{
		assert(m_mesh != nullptr);
		m_mesh->AttachVertexLayout(pipelineDesc);
	}

	void
	Drawable::AttachVertexShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept
	{
		assert(m_mesh != nullptr);
		m_mesh->AttachVertexShader(pipelineDesc);
	}

	void
	Drawable::AttachPixelShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept
	{
		assert(m_material != nullptr);
		pipelineDesc.setPixelShader(m_material->GetPixelShader());
	}

	void
	Drawable::Draw(DrawParams params)
	{
		auto& state   = params.gfxState;
		auto  cmdList = params.commandList;

		m_mesh->AttachMesh(state);

		cmdList->setGraphicsState(state);

		cmdList->drawIndexed(
			nvrhi::DrawArguments{}
				.setVertexCount(m_mesh->GetIndexCount())
				.setStartIndexLocation(0)
				.setInstanceCount(1));
	}

	bool
	Drawable::CanDraw() const noexcept
	{
		return m_mesh != nullptr && m_material != nullptr;
	}

	void
	Drawable::SetMesh(std::shared_ptr<const Mesh> mesh) noexcept
	{
		m_mesh = mesh;
	}

	void
	Drawable::SetMaterial(std::shared_ptr<const Material> material) noexcept
	{
		m_material = material;
	}

}
