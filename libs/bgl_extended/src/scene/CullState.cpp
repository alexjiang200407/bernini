#include "scene/CullState.h"
#include "fg/FrameGraph.h"
#include "scene/scene_buffer_names.h"
#include <bgl_common/idl/CullView.h>
#include <bgl_common/idl/DispatchArgs.h>
#include <bgl_common/idl/InstanceVisibility.h>
#include <bgl_common/idl/PsoType.h>

namespace bgl
{
	void
	CullState::Init(uint32_t paddedInstances, ResourceManagerRef resourceManager)
	{
		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances;
			desc.debugName    = "Compacted Instances";
			desc.SetElement<uint32_t>();

			m_CompactedInstances.Init(std::move(desc), resourceManager);
		}

		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances;
			desc.debugName    = "Instance Visibility";
			desc.SetElement<idl::InstanceVisibility>();

			m_InstanceVisibility.Init(std::move(desc), resourceManager);
		}

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<uint32_t>()
				.SetInitialCount(idl::c_PsoCount)
				.SetDebugName("Pso Prefix Sum");

			m_PsoPrefixSum.Init(std::move(desc), resourceManager);
		}

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<idl::DispatchArgs>()
				.SetInitialCount(idl::c_PsoCount)
				.SetDebugName("Compacted Dispatch Args");

			m_CompactedDispatchArgs.Init(std::move(desc), resourceManager);
		}

		{
			auto desc         = UploadBufferDesc();
			desc.initialCount = 1;
			desc.debugName    = "Cull View";

			m_CullView.Init(std::move(desc), std::move(resourceManager));
		}
	}

	void
	CullState::Resize(uint32_t paddedInstances)
	{
		if (paddedInstances <= m_CompactedInstances.GetDesc().initialCount)
		{
			return;
		}

		m_CompactedInstances.Resize(paddedInstances);
		m_InstanceVisibility.Resize(paddedInstances);
	}

	void
	CullState::Release(bool deferred) noexcept
	{
		m_CompactedInstances.Release(deferred);
		m_InstanceVisibility.Release(deferred);
		m_PsoPrefixSum.Release(deferred);
		m_CompactedDispatchArgs.Release(deferred);
		m_CullView.Release(deferred);
	}

	void
	CullState::Update(ICommandList* cmdList)
	{
		m_CompactedInstances.Update(cmdList);
		m_InstanceVisibility.Update(cmdList);
	}

	void
	CullState::ImportResources(
		FrameGraph&               fg,
		std::string_view          scope,
		std::vector<std::string>& updateArgs) const
	{
		fg.SetResourceNamespace(std::string(scope));

		const auto importUpdated = [&](std::string_view name, const ComputeBuffer& buffer) {
			fg.ImportBuffer(name, buffer.GetBufferHandle());
			updateArgs.push_back(std::format("{}{}", scope, name));
		};

		importUpdated(c_CompactedInstancesName, m_CompactedInstances);
		importUpdated(c_InstanceVisibilityName, m_InstanceVisibility);

		fg.ImportBuffer(c_PsoPrefixSumName, m_PsoPrefixSum.GetBufferHandle());
		fg.ImportBuffer(c_CompactDispatchArgsName, m_CompactedDispatchArgs.GetBufferHandle());
		fg.ImportBuffer(c_CullViewName, m_CullView.GetBufferHandle());
	}
}
