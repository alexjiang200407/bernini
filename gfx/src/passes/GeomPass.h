#pragma once

namespace gfx
{
	class Camera;
	class ShaderMatrix;

	class GeomPass
	{
	public:
		struct DrawCallData
		{
			nvrhi::GraphicsState& gfxState;
			Camera*               camera;
			ShaderMatrix*         modelTransform;
		};

	protected:
		static void
		Setup(nvrhi::DeviceHandle device);

		nvrhi::BindingSetItem
		GetCameraBindingSetItem() const noexcept;

		nvrhi::BindingSetItem
		GetTransformBindingSetItem() const noexcept;

		nvrhi::BindingLayoutItem
		GetCameraBindingLayoutItem() const noexcept;

		nvrhi::BindingLayoutItem
		GetTransformBindingLayoutItem() const noexcept;

		nvrhi::BindingSetDesc&
		AttachBindingSetItems(nvrhi::BindingSetDesc& desc) const noexcept
		{
			return desc.addItem(GetCameraBindingSetItem()).addItem(GetTransformBindingSetItem());
		}

		nvrhi::BindingLayoutDesc&
		AttachVertexBindingLayoutItems(nvrhi::BindingLayoutDesc& desc) const noexcept
		{
			return desc.setVisibility(nvrhi::ShaderType::Vertex)
			    .addItem(GetCameraBindingLayoutItem())
			    .addItem(GetTransformBindingLayoutItem());
		}

		void
		UpdateCameraBuffer(nvrhi::CommandListHandle cmdList, Camera& camera) const noexcept;

		void
		UpdateTransformBuffer(nvrhi::CommandListHandle cmdList, ShaderMatrix transform)
			const noexcept;

	protected:
		static inline nvrhi::BufferHandle m_cameraCBuf;
		static inline nvrhi::BufferHandle m_transformCBuf;
	};

}
