#pragma once
#include "math/ShaderMatrix.h"

namespace gfx
{
	class Mesh;
	class Material;

	struct DrawParams
	{
		nvrhi::GraphicsState&    gfxState;
		nvrhi::DeviceHandle      device;
		nvrhi::CommandListHandle commandList;
	};

	class Drawable
	{
	public:
		Drawable(ShaderMatrix pos) : m_modelTransform{ pos } {};

		void
		AttachVertexLayout(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept;

		void
		AttachVertexShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept;

		void
		AttachPixelShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept;

		void
		Draw(DrawParams params);

		bool
		CanDraw() const noexcept;

		void
		SetMesh(std::shared_ptr<const Mesh> mesh) noexcept;

		void
		SetMaterial(std::shared_ptr<const Material> material) noexcept;

		ShaderMatrix
		GetTransform() const noexcept
		{
			return m_modelTransform;
		}

	private:
		ShaderMatrix                    m_modelTransform{};
		std::shared_ptr<const Mesh>     m_mesh{};
		std::shared_ptr<const Material> m_material{};
	};
}
