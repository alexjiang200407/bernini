#include "passes/SetupFrameDataPass.h"
#include "BindingSlots.h"
#include "camera/Camera.h"
#include "frame_graph/FrameGraphView.h"
#include "math/util.h"
#include "mesh/Mesh.h"
#include "mesh/MeshRegistry.h"
#include "mesh/Vertex.h"
#include "passes/GBufferPass.h"
#include "passes/output/FrameData.h"
#include "shader_util/util.h"
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace gfx
{
	nvrhi::BindingLayoutHandle
	SetupFrameDataPass::Init(nvrhi::DeviceHandle device, MeshRegistry& registry)
	{
		m_mainCommandList = device->createCommandList();

		m_frameConstants = DynamicConstantBuffer(
			device,
			DynamicConstantBufferDesc{}
				.AddElement("viewMatrix", ElementType::kFloat4x4)
				.AddElement("projMatrix", ElementType::kFloat4x4)
				.AddElement("instanceCount", ElementType::kUInt)
				.SetName("FrameConstantBuffer"));

		auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
		bindingLayoutDesc.setRegisterSpace(BindingSpaces::PerFrameSpace)
			.addItem(m_frameConstants.GetBindingLayoutItem(BindingSlots::CB::FrameConstants))
			.setVisibility(nvrhi::ShaderType::All);

		registry.AttachBindingLayoutItems(bindingLayoutDesc);

		m_blPerFrame = device->createBindingLayout(bindingLayoutDesc);

		return m_blPerFrame;
	}

	void
	SetupFrameDataPass::AttachToFrameGraph(
		FrameGraph&           frameGraph,
		FrameGraphBlackboard& blackBoard,
		MeshRegistry&         registry,
		Camera&               camera,
		nvrhi::DeviceHandle   device)
	{
		blackBoard.add<FrameData>() = frameGraph.addCallbackPass<FrameData>(
			"SetupFrameGraphPass",
			[this, &registry, device](FrameGraph::Builder& builder, FrameData& data) {
				using BindingLayoutAView = FrameGraphView<nvrhi::IBindingLayout>;

				data.bsFrameData = builder.create<FGBindingSet>("bsFrameData"sv, {});

				data.instanceCount =
					builder.create<FGCount>("instanceCount"sv, FGCount::Desc::Create(0));

				data.bsFrameData   = builder.write(data.bsFrameData);
				data.instanceCount = builder.write(data.instanceCount);
			},
			[this, device, &registry, &camera, &blackBoard](
				const FrameData&         data,
				FrameGraphPassResources& fgr,
				void*) {
				m_mainCommandList->open();
				auto instanceCount = registry.GetInstancesCount();

				fgr.get<FGCount>(data.instanceCount).SetValue(instanceCount);

				if (camera.ShouldUpdate())
				{
					m_frameConstants["viewMatrix"] = camera.GetViewMatrix();
					m_frameConstants["projMatrix"] = camera.GetProjMatrix();
				}

				if (registry.Update(m_mainCommandList, device))
				{
					auto bindingSetDesc = nvrhi::BindingSetDesc{};
					bindingSetDesc.addItem(
						m_frameConstants.GetBindingSetItem(BindingSlots::CB::FrameConstants));
					registry.AttachBindingSetItems(bindingSetDesc);

					m_bsPerFrame = device->createBindingSet(bindingSetDesc, m_blPerFrame);
				}

				fgr.get<FGBindingSet>(data.bsFrameData).SetValue(m_mainCommandList, m_bsPerFrame);

				m_frameConstants["instanceCount"] = instanceCount;

				m_frameConstants.Update(m_mainCommandList);

				m_mainCommandList->close();
				device->executeCommandList(m_mainCommandList);
			});
	}
}
