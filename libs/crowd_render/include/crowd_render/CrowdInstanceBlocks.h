#pragma once
#include <bgl/IExternalBuffer.h>
#include <bgl/IGraphics.h>
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/MeshInstanceBlockHandle.h>
#include <crowdlib/ICrowd.h>
#include <cstdint>
#include <utility>
#include <vector>

namespace crowd_render
{
	/**
	 * How one agent type is drawn: its geoms, placed in the agent's frame by `model`. All static, or
	 * all skinned to one rig -- a character cooked as several meshes is several geoms -- in which
	 * case every agent plays `playback`, each from a phase of its own.
	 */
	struct AgentTypeMeshDesc
	{
		// One block each, all placed alike. To the renderer each is a placement of its own.
		std::vector<bgl::GeomHandle> geoms;

		// The geoms' placement in the agent's frame: +z its facing, y up, the origin on the ground
		// where the agent stands.
		glm::mat4 model = glm::mat4(1.0f);

		// The most agents of this type the crowd holds at once, which is what each of its blocks
		// draws and culls every frame; 0 is the crowd's maxAgents. Agents past it are not drawn.
		uint32_t capacity = 0;

		// What every agent of a skinned type plays, as bgl::MeshInstanceBlockDesc::playback takes
		// it. Not read for static geoms.
		bgl::SkinnedPlaybackDesc playback;

		// Seconds the type's agents are spread over, each ahead of the clock by a share of it that
		// its crowd::RenderAgent::id fixes for its life: a looping clip's cycle puts every agent at
		// a phase of its own. 0 plays them in step.
		float phaseSpreadSeconds = 0.0f;

		template <typename Self>
		Self&&
		AddGeom(this Self&& self, bgl::GeomHandle geom)
		{
			self.geoms.push_back(geom);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPlayback(this Self&& self, const bgl::SkinnedPlaybackDesc& playback) noexcept
		{
			self.playback = playback;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPhaseSpreadSeconds(this Self&& self, float seconds) noexcept
		{
			self.phaseSpreadSeconds = seconds;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetModel(this Self&& self, const glm::mat4& model) noexcept
		{
			self.model = model;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetCapacity(this Self&& self, uint32_t capacity) noexcept
		{
			self.capacity = capacity;
			return std::forward<Self>(self);
		}
	};

	/**
	 * A crowd drawn in one view: the crowd, which must have a render ring, the renderer on the same
	 * GPU context, and what draws each of the crowd's agent types, in CrowdDesc::agentTypes order.
	 */
	struct CrowdInstanceBlocksDesc
	{
		crowd::CrowdRef                crowd;
		bgl::GraphicsRef               graphics;
		bgl::SceneViewRef              view;
		std::vector<AgentTypeMeshDesc> types;

		template <typename Self>
		Self&&
		SetCrowd(this Self&& self, crowd::CrowdRef crowd) noexcept
		{
			self.crowd = std::move(crowd);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetGraphics(this Self&& self, bgl::GraphicsRef graphics) noexcept
		{
			self.graphics = std::move(graphics);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetView(this Self&& self, bgl::SceneViewRef view) noexcept
		{
			self.view = std::move(view);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddType(this Self&& self, AgentTypeMeshDesc type) noexcept
		{
			self.types.push_back(std::move(type));
			return std::forward<Self>(self);
		}
	};

	/**
	 * A crowd's agents drawn GPU to GPU: one instance block per agent type in the view, placed
	 * every frame from the type's run of the crowd's render ring by a writer kernel, with no
	 * per-agent work on the CPU. A frame draws the crowd between its last two completed ticks,
	 * `alpha` of the way, and each agent's motion vector is its pose last frame.
	 *
	 * Every frame is bracketed: PrepareFrame before the renderer's frame begins, FinishFrame after
	 * it ends. FinishFrame hands the ticks no later frame reads back to the crowd, which cannot step
	 * over its ring without them: a view that stops being drawn stops the crowd's stepping once its
	 * ring is full.
	 *
	 * Release it before the renderer and the crowd: it deletes its blocks from the view.
	 */
	class CrowdInstanceBlocks
	{
	public:
		/**
		 * Imports the crowd's ring, compiles the writer once and creates a block per agent type.
		 *
		 * @throws std::runtime_error if a ref is null, the crowd has no render ring, there is not
		 *         one desc per agent type, or a type names no geom -- or, until a block per geom is
		 *         made, more than one; the renderer's errors as CreateMeshInstanceBlock and
		 *         ImportBuffer throw them.
		 */
		explicit CrowdInstanceBlocks(CrowdInstanceBlocksDesc desc);
		~CrowdInstanceBlocks();

		CrowdInstanceBlocks(const CrowdInstanceBlocks&) = delete;
		CrowdInstanceBlocks(CrowdInstanceBlocks&&)      = delete;

		CrowdInstanceBlocks&
		operator=(const CrowdInstanceBlocks&) = delete;

		CrowdInstanceBlocks&
		operator=(CrowdInstanceBlocks&&) = delete;

		/**
		 * Points the next frame at the crowd's latest completed tick, drawn `alpha` of the way from
		 * the tick before it: 0 is the tick before, 1 the latest. A game's fixed-step accumulator
		 * divided by the tick is its alpha. Until a tick completes, every agent is hidden.
		 *
		 * @throws std::runtime_error if `alpha` is outside [0, 1]; the renderer's error if called
		 *         between BeginFrame and EndFrame.
		 */
		void
		PrepareFrame(float alpha);

		/**
		 * Releases to the crowd the ticks the frame just drawn was the last to read, at the point
		 * that frame passes on the renderer's queue.
		 *
		 * @throws the renderer's error if called between BeginFrame and EndFrame.
		 */
		void
		FinishFrame();

	private:
		CrowdInstanceBlocksDesc                   m_Desc;
		bgl::ExternalBufferRef                    m_Ring;
		bgl::MeshInstanceWriterRef                m_Writer;
		std::vector<bgl::MeshInstanceBlockHandle> m_Blocks;

		// The tick the last prepared frame drew, and how far between it and the tick before.
		uint64_t m_DrawnTick  = 0;
		float    m_DrawnAlpha = 0.0f;
	};
}
