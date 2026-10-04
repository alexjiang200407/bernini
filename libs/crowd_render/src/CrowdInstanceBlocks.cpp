#include "WriterFrame.h"
#include <algorithm>
#include <bgl/GeomType.h>
#include <bgl/IExternalBuffer.h>
#include <bgl/IGraphics.h>
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/ISceneView.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/MeshInstanceBlockDesc.h>
#include <bgl/types/MeshInstanceWriterDesc.h>
#include <core/err/util.h>
#include <crowd_render/CrowdInstanceBlocks.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/RenderTick.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace crowd_render
{
	namespace
	{
		// A frame reads its tick and the two before it: the one it interpolates from, and the one
		// last frame's pose may need.
		constexpr uint64_t c_TicksRead = 3;

		bool
		IsSkinned(bgl::GeomHandle geom) noexcept
		{
			return geom.geomType == bgl::GeomType::kSkinnedMesh;
		}

		CrowdInstanceBlocksDesc
		Validate(CrowdInstanceBlocksDesc desc)
		{
			if (desc.crowd == nullptr || desc.graphics == nullptr || desc.view == nullptr)
				core::throw_runtime_error(
					"CrowdInstanceBlocks needs a crowd, a renderer and a view");
			if (desc.crowd->GetDesc().renderRingTicks == 0)
				core::throw_runtime_error("CrowdInstanceBlocks needs a crowd with a render ring");
			const auto typeCount = desc.crowd->GetDesc().agentTypes.size();
			if (desc.types.size() != typeCount)
			{
				core::throw_runtime_error(
					"{} agent type meshes for a crowd of {} agent types",
					desc.types.size(),
					typeCount);
			}
			for (size_t type = 0; type < desc.types.size(); ++type)
			{
				const auto& geoms = desc.types[type].geoms;
				if (geoms.empty())
					core::throw_runtime_error("agent type {} names no geom", type);
				if (!std::ranges::all_of(geoms, [&](bgl::GeomHandle geom) {
						return IsSkinned(geom) == IsSkinned(geoms.front());
					}))
				{
					core::throw_runtime_error("agent type {} mixes static and skinned geoms", type);
				}
			}
			return desc;
		}

		bgl::MeshInstanceWriterRef
		CreateWriter(bgl::IGraphics& graphics, bool skinned)
		{
			return graphics.CreateMeshInstanceWriter(
				bgl::MeshInstanceWriterDesc()
					.SetSlangModuleName("crowd_render.CrowdInstanceWriter")
					.SetSlangTypeName(
						skinned ? "SkinnedCrowdInstanceWriter" : "CrowdInstanceWriter")
					.SetGeomType(
						skinned ? bgl::GeomType::kSkinnedMesh : bgl::GeomType::kStaticMesh));
		}
	}

	CrowdInstanceBlocks::CrowdInstanceBlocks(CrowdInstanceBlocksDesc desc) :
		m_Desc(Validate(std::move(desc))),
		m_Ring(m_Desc.graphics->ImportBuffer(m_Desc.crowd->GetRenderRing()))
	{
		auto& view = *m_Desc.view;
		try
		{
			for (uint32_t type = 0; type < m_Desc.types.size(); ++type)
			{
				const AgentTypeMeshDesc& mesh    = m_Desc.types[type];
				const bool               skinned = IsSkinned(mesh.geoms.front());
				auto&                    writer  = skinned ? m_SkinnedWriter : m_StaticWriter;
				if (writer == nullptr)
					writer = CreateWriter(*m_Desc.graphics, skinned);

				for (const bgl::GeomHandle geom : mesh.geoms)
				{
					const auto block = view.CreateMeshInstanceBlock(
						bgl::MeshInstanceBlockDesc()
							.SetGeom(geom)
							.SetPlayback(mesh.playback)
							.SetCapacity(
								mesh.capacity != 0 ? mesh.capacity :
													 m_Desc.crowd->GetDesc().maxAgents));
					m_Blocks.push_back({ .block = block, .type = type });
					view.SetBlockWriter(block, writer);
					WriteWriterParams(
						view.GetBlockParams(block),
						WriterFrame(),
						m_Ring->GetHandle(),
						type,
						mesh.model,
						mesh.phaseSpreadSeconds);
				}
			}
		}
		catch (...)
		{
			for (const TypeBlock& typeBlock : m_Blocks)
				view.DeleteMeshInstanceBlock(typeBlock.block);
			throw;
		}
	}

	CrowdInstanceBlocks::~CrowdInstanceBlocks()
	{
		for (const TypeBlock& typeBlock : m_Blocks)
			m_Desc.view->DeleteMeshInstanceBlock(typeBlock.block);
	}

	void
	CrowdInstanceBlocks::PrepareFrame(float alpha)
	{
		if (!(alpha >= 0.0f && alpha <= 1.0f))
			core::throw_runtime_error("A frame's alpha must be in [0, 1], not {}", alpha);

		auto&          crowd = *m_Desc.crowd;
		const uint64_t tick  = crowd.GetCompletedTick();
		if (tick != 0)
		{
			// Completed already, so the wait passes at once; it is what orders the crowd's writes
			// before the frame's reads on a queue the crowd does not share.
			const std::optional<crowd::RenderTick> written = crowd.GetRenderTick(tick);
			core::ensure(written.has_value(), "the ring holds every tick a frame may draw");
			m_Desc.graphics->WaitBeforeNextFrame(written->written);
		}

		const WriterFrame frame = PlanWriterFrame(crowd, tick, alpha, m_DrawnTick, m_DrawnAlpha);
		for (const TypeBlock& typeBlock : m_Blocks)
		{
			const AgentTypeMeshDesc& mesh = m_Desc.types[typeBlock.type];
			WriteWriterParams(
				m_Desc.view->GetBlockParams(typeBlock.block),
				frame,
				m_Ring->GetHandle(),
				typeBlock.type,
				mesh.model,
				mesh.phaseSpreadSeconds);
		}
		m_DrawnTick  = tick;
		m_DrawnAlpha = alpha;
	}

	void
	CrowdInstanceBlocks::FinishFrame()
	{
		// Ticks only grow, so no later frame reads before the oldest this one read.
		if (m_DrawnTick <= c_TicksRead)
			return;
		const uint64_t through = m_DrawnTick - c_TicksRead;
		if (through > m_Desc.crowd->GetReleasedRenderTick())
			m_Desc.crowd->ReleaseRenderReads(through, m_Desc.graphics->GetLastFrameDone());
	}
}
