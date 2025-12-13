#include "passes/GeomPass.h"
#include "BindingSlots.h"
#include "camera/Camera.h"
#include "math/ShaderMatrix.h"

namespace gfx
{
	void
	GeomPass::Setup(nvrhi::DeviceHandle device)
	{
		if (!m_cameraCBuf.Initialized())
		{
			auto cameraBufDesc = DynamicBufferDesc{};
			cameraBufDesc.AddElement("viewMatrix", ElementType::kFloat4x4)
				.AddElement("projMatrix", ElementType::kFloat4x4)
				.SetName("CameraConstantBuffer");
			m_cameraCBuf = std::move(DynamicConstantBuffer{ device, cameraBufDesc });
		}

		if (!m_transformCBuf.Initialized())
		{
			auto transformBufDesc = DynamicBufferDesc{};
			transformBufDesc.AddElement("transformMatrix", ElementType::kFloat4x4)
				.SetName("ModelTransformBuffer")
				.SetUpdateFrequency(DynamicBufferDesc::UpdateFrequency::kPerDraw);

			m_transformCBuf = std::move(DynamicConstantBuffer{ device, transformBufDesc });
		}
	}

	void
	GeomPass::Shutdown()
	{
		m_transformCBuf.Release();
		m_cameraCBuf.Release();
	}

	nvrhi::BindingSetItem
	GeomPass::GetCameraBindingSetItem() const noexcept
	{
		return m_cameraCBuf.GetBindingSetItem(BindingSlots::CameraVCB);
	}

	nvrhi::BindingSetItem
	GeomPass::GetTransformBindingSetItem() const noexcept
	{
		return m_transformCBuf.GetBindingSetItem(BindingSlots::ObjectTransformVCB);
	}

	nvrhi::BindingLayoutItem
	GeomPass::GetCameraBindingLayoutItem() const noexcept
	{
		return m_cameraCBuf.GetBindingLayoutItem(BindingSlots::CameraVCB);
	}

	nvrhi::BindingLayoutItem
	GeomPass::GetTransformBindingLayoutItem() const noexcept
	{
		return m_transformCBuf.GetBindingLayoutItem(BindingSlots::ObjectTransformVCB);
	}

	void
	GeomPass::UpdateCameraBuffer(nvrhi::CommandListHandle cmdList, Camera& camera) const noexcept
	{
		if (camera.ShouldUpdate())
		{
			camera.OnUpdated();

			m_cameraCBuf["viewMatrix"] = camera.GetViewMatrix();
			m_cameraCBuf["projMatrix"] = camera.GetProjMatrix();

			m_cameraCBuf.Update(cmdList);
		}
	}

	void
	GeomPass::UpdateTransformBuffer(nvrhi::CommandListHandle cmdList, ShaderMatrix transform)
		const noexcept
	{
		m_transformCBuf["transformMatrix"] = transform;
		m_transformCBuf.Update(cmdList);
	}

}
