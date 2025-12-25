#pragma once
#include "buffer/DynamicConstantBuffer.h"

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

	public:
		static void
		Setup(nvrhi::DeviceHandle device);

		static void
		Shutdown();

	protected:
		nvrhi::BindingSetItem
		GetCameraBindingSetItem() const noexcept;

		nvrhi::BindingSetItem
		GetTransformBindingSetItem() const noexcept;

		nvrhi::BindingLayoutItem
		GetCameraBindingLayoutItem() const noexcept;

		nvrhi::BindingLayoutItem
		GetTransformBindingLayoutItem() const noexcept;

		nvrhi::BindingSetDesc&
		AttachPerFrameBindingSetItems(nvrhi::BindingSetDesc& desc) const noexcept
		{
			return desc.addItem(GetCameraBindingSetItem());
		}

		nvrhi::BindingSetDesc&
		AttachPerObjBindingSetItems(nvrhi::BindingSetDesc& desc) const noexcept
		{
			return desc.addItem(GetTransformBindingSetItem());
		}

		nvrhi::BindingLayoutDesc&
		AttachPerFrameBindingLayoutItems(nvrhi::BindingLayoutDesc& desc) const noexcept
		{
			return desc.setVisibility(nvrhi::ShaderType::Vertex)
			    .addItem(GetCameraBindingLayoutItem());
		}

		nvrhi::BindingLayoutDesc&
		AttachPerObjBindingLayoutItems(nvrhi::BindingLayoutDesc& desc) const noexcept
		{
			return desc.setVisibility(nvrhi::ShaderType::Vertex)
			    .addItem(GetTransformBindingLayoutItem());
		}

		void
		UpdateCameraBuffer(nvrhi::CommandListHandle cmdList, Camera& camera) const noexcept;

		void
		UpdateTransformBuffer(nvrhi::CommandListHandle cmdList, ShaderMatrix transform)
			const noexcept;

	protected:
		static inline DynamicConstantBuffer m_cameraCBuf;
		static inline DynamicConstantBuffer m_transformCBuf;
	};

}
