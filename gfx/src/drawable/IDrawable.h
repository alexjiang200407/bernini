#pragma once
#include "math/ShaderMatrix.h"

namespace gfx
{
	struct DrawParams
	{
		nvrhi::GraphicsState&    gfxState;
		nvrhi::DeviceHandle      device;
		nvrhi::CommandListHandle commandList;
	};

	class IDrawable
	{
	public:
		IDrawable(ShaderMatrix pos);
		virtual ~IDrawable() = default;

		virtual void
		AttachVertexLayout(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept = 0;

		virtual void
		AttachVertexShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept = 0;

		virtual void
		AttachPixelShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept = 0;

		virtual nvrhi::BindingSetDesc&
		AttachBindingSetItems(nvrhi::BindingSetDesc& desc) const noexcept
		{
			return desc;
		}

		virtual nvrhi::BindingSetDesc&
		AttachBindingLayoutItems(nvrhi::BindingSetDesc& desc) const noexcept
		{
			return desc;
		}

		virtual void
		Draw(DrawParams params) = 0;

		ShaderMatrix
		GetTransform() const noexcept
		{
			return m_modelTransform;
		}

	private:
		ShaderMatrix m_modelTransform{};
	};
}
