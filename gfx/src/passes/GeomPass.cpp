#include "passes/GeomPass.h"
#include "BindingSlots.h"
#include "camera/Camera.h"
#include "math/ShaderMatrix.h"

namespace gfx
{
	struct CameraCBuf
	{
		ShaderMatrix viewMatrix;
		ShaderMatrix projMatrix;
	};

	void
	GeomPass::Setup(nvrhi::DeviceHandle device)
	{
		if (!m_cameraCBuf.Get())
		{
			m_cameraCBuf = device->createBuffer(
				nvrhi::utils::CreateVolatileConstantBufferDesc(
					sizeof(CameraCBuf),
					"CameraConstantBuffer",
					16));
		}

		if (!m_transformCBuf.Get())
		{
			m_transformCBuf = device->createBuffer(
				nvrhi::utils::CreateVolatileConstantBufferDesc(
					sizeof(ShaderMatrix),
					"TransformConstantBuffer",
					16));
		}
	}

	nvrhi::BindingSetItem
	GeomPass::GetCameraBindingSetItem() const noexcept
	{
		return nvrhi::BindingSetItem::ConstantBuffer(BindingSlots::CameraVCB, m_cameraCBuf);
	}

	nvrhi::BindingSetItem
	GeomPass::GetTransformBindingSetItem() const noexcept
	{
		return nvrhi::BindingSetItem::ConstantBuffer(
			BindingSlots::ObjectTransformVCB,
			m_transformCBuf);
	}

	nvrhi::BindingLayoutItem
	GeomPass::GetCameraBindingLayoutItem() const noexcept
	{
		return nvrhi::BindingLayoutItem::VolatileConstantBuffer(BindingSlots::CameraVCB);
	}

	nvrhi::BindingLayoutItem
	GeomPass::GetTransformBindingLayoutItem() const noexcept
	{
		return nvrhi::BindingLayoutItem::VolatileConstantBuffer(BindingSlots::ObjectTransformVCB);
	}

	void
	GeomPass::UpdateCameraBuffer(nvrhi::CommandListHandle cmdList, Camera& camera) const noexcept
	{
		if (camera.ShouldUpdate())
		{
			camera.OnUpdated();

			auto cameraCbufData = CameraCBuf{ .viewMatrix = camera.GetViewMatrix(),
				                              .projMatrix = camera.GetProjMatrix() };

			cmdList->writeBuffer(m_cameraCBuf, &cameraCbufData, sizeof(cameraCbufData));
		}
	}

	void
	GeomPass::UpdateTransformBuffer(nvrhi::CommandListHandle cmdList, ShaderMatrix transform)
		const noexcept
	{
		cmdList->writeBuffer(m_transformCBuf, &transform, sizeof(transform));
	}

}
