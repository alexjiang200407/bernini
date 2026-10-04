#include "scene/ToonShadingRigState.h"
#include "fg/FrameGraph.h"
#include "scene/scene_buffer_names.h"
#include <bgl/ToonShadingRigLimits.h>
#include <bgl/idl/ToonShadingRigBlock.h>
#include <bgl/idl/ToonShadingRigPool.h>
#include <bgl/idl/ToonShadingRigRange.h>
#include <bgpu/cmd/CommandList.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		static_assert(
			idl::cToonShadingRigSlotMask >= cToonShadingRigPoolCapacity,
			"a flags word's slot bits must name every block of the pool, plus one for none");
		static_assert(
			((idl::cToonShadingRigSlotMask + 1u) & idl::cToonShadingRigSlotMask) == 0u &&
				((idl::cToonShadingRigSlotMask << idl::cToonShadingRigSlotShift) >>
		         idl::cToonShadingRigSlotShift) == idl::cToonShadingRigSlotMask,
			"the slot bits are a contiguous run that fits the flags word");
	}

	ToonShadingRigState::ToonShadingRigState(const bgpu::ResourceManagerRef& resourceManager) :
		m_Ranges(
			resourceManager,
			bgpu::UploadBufferDesc().SetInitialCount(1).SetDebugName("Toon Shading Rig Ranges")),
		m_Pool(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<idl::ToonShadingRigPool>()
				.SetInitialCount(1)
				.SetDebugName("Toon Shading Rig Pool")),
		m_Blocks(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<idl::ToonShadingRigBlock>()
				.SetInitialCount(1)
				.SetDebugName("Toon Shading Rig Blocks"))
	{}

	void
	ToonShadingRigState::Assign(std::vector<idl::ToonShadingRigRange> ranges)
	{
		uint32_t placements = 0;
		for (idl::ToonShadingRigRange& range : ranges)
		{
			range.firstThread = placements;
			placements += range.placements.count;
		}

		m_Ranges.Assign(std::span<const idl::ToonShadingRigRange>(ranges));
		m_PlacementCount = placements;

		if (!ranges.empty() && m_Blocks.GetDesc().initialCount < cToonShadingRigPoolCapacity)
		{
			m_Blocks.Resize(cToonShadingRigPoolCapacity);
		}
	}

	void
	ToonShadingRigState::Update(bgpu::ICommandList* cmdList)
	{
		m_Ranges.Update(cmdList);
		m_Pool.Update(cmdList);
		m_Blocks.Update(cmdList);

		const auto pool = idl::ToonShadingRigPool();
		cmdList->WriteBuffer(m_Pool.GetBufferHandle(), &pool, sizeof(pool));
	}

	void
	ToonShadingRigState::ImportResources(FrameGraph& fg, std::vector<std::string>& updateArgs) const
	{
		const auto import = [&](std::string_view name, bgpu::BufferHandle handle) {
			fg.ImportBuffer(name, handle);
			updateArgs.emplace_back(name);
		};

		import(c_ToonShadingRigRangesName, m_Ranges.GetBufferHandle());
		import(c_ToonShadingRigPoolName, m_Pool.GetBufferHandle());
		import(c_ToonShadingRigBlocksName, m_Blocks.GetBufferHandle());
	}
}
