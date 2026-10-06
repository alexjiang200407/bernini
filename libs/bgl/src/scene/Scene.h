#pragma once
#include "scene/BonePaletteBuffer.h"
#include "scene/NamedBuffer.h"
#include "scene/TextureAssetStore.h"
#include "scene/scene_buffer_names.h"
#include "types/SubmeshInstance.h"
#include "types/VertexGen.h"
#include <RangeWithCount.h>
#include <array>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/IScene.h>
#include <bgl/LodLevel.h>
#include <bgl/MaterialType.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/SurfaceType.h>
#include <bgl/idl/BlendNode.h>
#include <bgl/idl/BlendSpaceSample.h>
#include <bgl/idl/BoneSample.h>
#include <bgl/idl/Clip.h>
#include <bgl/idl/Geom.h>
#include <bgl/idl/GrassChunk.h>
#include <bgl/idl/GrassClump.h>
#include <bgl/idl/GrassLook.h>
#include <bgl/idl/LodSubmeshRange.h>
#include <bgl/idl/LoosePbrMaterial.h>
#include <bgl/idl/Meshlet.h>
#include <bgl/idl/MeshletGroup.h>
#include <bgl/idl/PbrMaterial.h>
#include <bgl/idl/Rig.h>
#include <bgl/idl/SkinnedBone.h>
#include <bgl/idl/SkinnedLegChain.h>
#include <bgl/idl/Submesh.h>
#include <bgl/idl/Terrain.h>
#include <bgl/idl/TerrainNodeBounds.h>
#include <bgl/idl/ToonShadingRig.h>
#include <bgl/types/FootPlantDesc.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/GrassHandle.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/LoosePbrMaterialDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RigHandle.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <bgl/types/TextureAssetHandle.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <bgl/types/ToonShadingRigHandle.h>
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/buffer/EntryBuffer.h>
#include <bgpu/buffer/PackedBuffer.h>
#include <bgpu/buffer/RangeBuffer.h>
#include <bgpu/buffer/RawBuffer.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <core/containers/multi_slot_handle.h>
#include <core/containers/slot_handle.h>
#include <core/containers/slot_vector.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace bgpu
{
	class ICommandList;
}

namespace bgl
{
	class FrameGraph;

	/**
	 * One grass field bound to a geom: the look it draws with, and where its chunks and clumps sit in
	 * the scene's grass buffers. `chunks.index` is the field's first chunk there.
	 */
	struct GrassFieldRecord
	{
		GrassHandle             look;
		core::multi_slot_handle chunks;
		core::multi_slot_handle clumps;
		uint32_t                chunkCount = 0;
	};

	/**
	 * One live geom. Every geom has a submesh range; a kSkinnedMesh one additionally *names* a rig it
	 * shares with every other geom skinned to it, and records its clip count for instance-creation
	 * validation.
	 *
	 * Namespace-scope rather than nested in Scene: a nested class's default member initializers
	 * only resolve once the enclosing class is complete, which would leave this
	 * not-default-constructible right where slot_vector's concept checks it.
	 */
	struct GeomRecord
	{
		// Every level's submeshes, level-major; see idl::LodSubmeshRange. `submeshes.submeshCount`
		// is one level's -- a placement's SubmeshInstances -- and TotalCount() the range's.
		idl::LodSubmeshRange submeshes;

		// Local space, enclosing level 0: what a placement's size on screen is measured from.
		glm::vec4 boundingSphere = glm::vec4(0.0f);

		// Each level's threshold, idl::Geom::lodMinPixels; zero past the geom's levels, and a
		// single-level geom's zero entry is "never dropped".
		std::array<float, cMaxMeshLods> lodMinPixels{};

		// The geom's GPU record, which holds the same range for a placement to name rather than
		// copy. Freed with the geom.
		core::slot_handle entry;

		// kSkinnedMesh only: the rig this geom poses from, shared rather than owned -- deleting the
		// geom releases its use of the rig, never the rig's ranges.
		core::slot_handle rig;

		uint32_t clipCount = 0;
		uint32_t boneCount = 0;  // kSkinnedMesh only
		uint32_t nodeCount = 0;  // kSkinnedMesh only: clips plus authored spaces
		uint32_t legCount  = 0;  // kSkinnedMesh only; zero on a rig that authored no legs

		// Every bound grass field, each holding a use of its look (see GrassMeta::useCount) and the
		// chunk and clump ranges it was uploaded into. Released by AttachGrass and DeleteGeom.
		std::vector<GrassFieldRecord> grass;
	};

	/**
	 * One live grass look, and the count that decides whether it may be deleted.
	 *
	 * Namespace-scope for the same reason as GeomRecord above.
	 */
	struct GrassMeta
	{
		GrassDesc desc;

		// The look's GrassLook record, which every field bound to it names.
		core::slot_handle entry;

		// Grass fields bound to this look across every live geom. DeleteGrass refuses while it is
		// nonzero: a field left naming a freed slot would draw with whatever look takes it next.
		uint32_t useCount = 0;
	};

	/**
	 * One live terrain: what the scene made of the desc's samples.
	 *
	 * Namespace-scope for the same reason as GeomRecord above.
	 */
	struct TerrainMeta
	{
		MaterialHandle material;

		// The samples as one R16_UNORM texture, the record the stage reads, and the lowest and
		// highest height of every node of every level (scene/terrain_lod.h), level-major. The
		// shape itself lives in the record alone: nothing on the CPU reads a terrain back.
		TextureAssetHandle      heights;
		core::slot_handle       record;
		core::multi_slot_handle nodeBounds;
	};

	/**
	 * The CPU half of a rig: what creating an instance needs without reading back the GPU record,
	 * and the count that decides whether the rig may be deleted.
	 *
	 * Namespace-scope for the same reason as GeomRecord above.
	 */
	struct RigMeta
	{
		uint32_t boneCount = 0;
		uint32_t clipCount = 0;

		// Clip nodes plus authored ones, which is what a playback slot's `node` is checked against.
		// Equal to `clipCount` on a rig with no blend set.
		uint32_t nodeCount = 0;
		// Legs the rig's FootPlantDesc listed, which sizes a hero instance's foot-IK record.
		uint32_t legCount = 0;

		// Frames across every clip, which sizes the bone anim table and is the fill's group count.
		uint32_t frameCount = 0;

		// Geoms added against this rig. DeleteRig refuses while it is nonzero: a geom outliving its
		// rig would pose from freed ranges, which is a read of whatever lands there next rather
		// than a misrender.
		uint32_t useCount = 0;

		// The rig's slice of the scene's bone anim table arena -- its matrices, then its soles -- null
		// until something asks for one.
		// Held here as well as on the GPU record because freeing it needs the allocator's handle.
		core::multi_slot_handle boneAnimTable;

		// Whether that slice holds a filled pose. False while one is allocated but unwritten -- on
		// the first request, and again after a growth, which discards what the arena held.
		bool tableFilled = false;
	};

	/**
	 * The CPU half of a toon shading rig: what SetToonShadingRig checks a placement against, and the count
	 * that decides whether the rig may be deleted.
	 *
	 * Namespace-scope for the same reason as GeomRecord above.
	 */
	struct ToonShadingRigMeta
	{
		std::optional<uint32_t> headBoneIndex;

		// Placements across every view that hold the rig. DeleteToonShadingRig refuses while it is
		// nonzero.
		uint32_t useCount = 0;
	};

	class Scene : public core::RefCounter<IScene>
	{
	public:
		enum class StandardSampler : uint32_t
		{
			kAnisoLinearWrap,
			kLinearClamp,
			kCount
		};

		Scene(
			SceneDesc                               desc,
			core::SharedRef<bgpu::IResourceManager> resourceManager,
			std::span<const SurfaceType>            surfaces);
		~Scene() noexcept override { spdlog::trace("~Scene"); }
		Scene(const Scene&) noexcept = delete;
		Scene(Scene&&) noexcept      = delete;

		Scene&
		operator=(const Scene&) noexcept = delete;

		Scene&
		operator=(Scene&&) noexcept = delete;

		const SceneDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		[[nodiscard]] auto&
		GetSubmeshBuffer() noexcept
		{
			return m_SubmeshBuffer;
		}

		[[nodiscard]] auto&
		GetMeshletBuffer() noexcept
		{
			return m_MeshletBuffer;
		}

		[[nodiscard]] auto&
		GetMeshletGroupBuffer() noexcept
		{
			return m_MeshletGroupBuffer;
		}

		[[nodiscard]] auto&
		GetVertexMapBuffer() noexcept
		{
			return m_VertexMapBuffer;
		}

		[[nodiscard]] auto&
		GetVertexDataBuffer() noexcept
		{
			return m_VertexDataBuffer;
		}

		[[nodiscard]] auto&
		GetIndexBuffer() noexcept
		{
			return m_IndexBuffer;
		}

		[[nodiscard]] auto&
		GetMaterialArena() noexcept
		{
			return m_Materials;
		}

		// The material arena and the typed view of the same allocation, as one binding. A payload
		// keeps a texture handle's bytes inline and the view is what makes a texture of them; the
		// arena owns both and re-issues the view inside its own growth, so they cannot disagree.
		[[nodiscard]] bgpu::RawArenaBinding
		GetMaterialBinding() const noexcept
		{
			return bgpu::RawArenaBinding{ m_Materials.GetBufferHandle(),
				                          m_Materials.GetHandleView() };
		}

		[[nodiscard]] auto&
		GetClipBuffer() noexcept
		{
			return m_Clips;
		}

		[[nodiscard]] auto&
		GetRigBuffer() noexcept
		{
			return m_Rigs;
		}

		[[nodiscard]] const BonePaletteBuffer&
		GetBoneAnimTables() const noexcept
		{
			return m_BoneAnimTables;
		}

		/** One rig whose bone anim table is allocated but not yet written. See PendingRigFills. */
		struct RigFill
		{
			uint32_t rigIndex;
			uint32_t frameCount;
		};

		/**
		 * Sweeps the rigs RigFramesPass must fill this frame: those asked for since the last fill,
		 * plus every filled one when a growth discarded the arena. Empty on almost every frame.
		 *
		 * Swept rather than maintained because a scene holds a handful of rigs, and a list kept
		 * incrementally would have to be right about every path that allocates, grows or deletes one.
		 */
		[[nodiscard]] std::span<const RigFill>
		PendingRigFills();

		/**
		 * Marks every rig the last PendingRigFills named as holding a pose.
		 *
		 * @pre the dispatches it named have been recorded.
		 */
		void
		MarkRigFillsRecorded() noexcept;

		/**
		 * Gives `rig` a bone anim table, and queues it to be filled. Idempotent: a rig that already
		 * holds a filled table is left alone.
		 *
		 * @throws SceneError if the handle is null or deleted, or the arena cannot grow.
		 */
		void
		RequestBoneAnimTable(RigHandle rig);

		[[nodiscard]] auto&
		GetSkinnedBoneBuffer() noexcept
		{
			return m_SkinnedBones;
		}

		[[nodiscard]] auto&
		GetBoneSampleBuffer() noexcept
		{
			return m_BoneSamples;
		}

		[[nodiscard]] auto&
		GetSkinnedLegBuffer() noexcept
		{
			return m_SkinnedLegs;
		}

		[[nodiscard]] auto&
		GetPlantWeightBuffer() noexcept
		{
			return m_PlantWeights;
		}

		[[nodiscard]] auto&
		GetBlendNodeBuffer() noexcept
		{
			return m_BlendNodes;
		}

		[[nodiscard]] auto&
		GetBlendSampleBuffer() noexcept
		{
			return m_BlendSamples;
		}

		// --- SceneView support -------------------------------------------------
		// Instances live in SceneViews and name this Scene's geometry by an entry with no
		// generation. The Scene keeps no record of who placed what, so the caller owns the
		// ordering -- see IScene::DeleteGeom.

		[[nodiscard]] bool
		IsGeomAlive(GeomHandle geom) const noexcept override
		{
			return geom.IsValid() && m_Geoms.valid(geom.handle);
		}

		// The geom's submesh range. A SceneView reads it once at instance-creation time, for the
		// submesh count and the root its shading resolve indexes by; the range itself reaches the
		// GPU on the Geom record, not on the placement.
		// Only valid while the geom is alive; check IsGeomAlive first.
		[[nodiscard]] const idl::LodSubmeshRange&
		GetGeomSubmeshes(uint32_t index) const noexcept
		{
			return m_Geoms[index].submeshes;
		}

		// The geom's GPU record, for a placement to name. Only valid while the geom is alive; check
		// IsGeomAlive first.
		[[nodiscard]] core::slot_handle
		GetGeomEntry(uint32_t index) const noexcept
		{
			return m_Geoms[index].entry;
		}

		[[nodiscard]] const bgpu::EntryBuffer<idl::Geom>&
		GetGeomBuffer() const noexcept
		{
			return m_GeomBuffer;
		}

		/**
		 * The animated half of a geom record: the per-rig entry a playback state points at, and the
		 * clip count instance creation validates against. The handle is invalid on a geom of another
		 * type, which is what makes it the type check's evidence rather than a second flag.
		 * Only valid while the geom is alive; check IsGeomAlive first.
		 */
		struct AnimGeomInfo
		{
			core::slot_handle record;
			uint32_t          clipCount = 0;

			// Bones the rig carries, which is what sizes an instance's palette.
			uint32_t boneCount = 0;

			// Nodes the rig's table holds -- its clips, then its authored spaces -- which is what a
			// playback slot's `node` is checked against.
			uint32_t nodeCount = 0;
			// Legs the rig authored, which is what sizes an instance's foot-IK record.
			uint32_t legCount = 0;
		};

		[[nodiscard]] AnimGeomInfo
		GetGeomSkinnedInfo(uint32_t index) const noexcept
		{
			const GeomRecord& geom = m_Geoms[index];
			return { geom.rig, geom.clipCount, geom.boneCount, geom.nodeCount, geom.legCount };
		}

		/**
		 * The default material of submesh `submeshIndex` of the geom whose range starts at
		 * `submeshRoot`. A SceneView resolves a SubmeshInstance from this when it has no override.
		 *
		 * A dead or shorter range yields a null handle (drawn unlit) rather than asserting: an
		 * instance may outlive its geom (see IScene::DeleteGeom), and the epoch re-resolve walks every
		 * instance, so one stale instance must not turn an authoring action into a crash.
		 */
		[[nodiscard]] MaterialHandle
		GetSubmeshDefaultMaterial(uint32_t submeshRoot, uint32_t submeshIndex) const noexcept
		{
			if (!m_SubmeshBuffer.IsIndexValid(submeshRoot))
			{
				return {};
			}

			const SubmeshDefaults& defaults = m_SubmeshBuffer.MetaAt(submeshRoot);
			return submeshIndex < defaults.size() ? defaults[submeshIndex] : MaterialHandle{};
		}

		/** Bumped by every SetSubmeshMaterial; a SceneView polls it in Update and re-resolves. */
		[[nodiscard]] uint64_t
		MaterialEpoch() const noexcept
		{
			return m_MaterialEpoch;
		}

		/**
		 * Bumped by every change to the scene that no motion vector describes: a material's
		 * contents, a submesh's binding, a texture's release. A SceneView polls it; see
		 * SceneView::AdvanceTemporalEpoch.
		 *
		 * Discrete rebinds only. State a caller moves every frame -- a camera, a transform -- is
		 * not in it: reprojection follows that, and an epoch that moved with it would leave a
		 * moving scene permanently unaccumulated.
		 */
		[[nodiscard]] uint64_t
		GetTemporalEpoch() const noexcept
		{
			return m_TemporalEpoch;
		}

		[[nodiscard]] const std::string&
		GetResourceNamespace() const noexcept
		{
			return m_NamePrefix;
		}

		[[nodiscard]] bgpu::SamplerHandle
		GetSampler(StandardSampler kind) const noexcept
		{
			return m_Samplers[static_cast<size_t>(kind)];
		}

		// The view this scene created for a texture asset, or a null handle if it created none.
		[[nodiscard]] bgpu::SrvHandle
		GetTextureSrv(core::slot_handle textureSlot) const noexcept
		{
			return m_Textures.GetSrv(textureSlot);
		}

		void
		AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx);

		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames);

		void
		Update(bgpu::ICommandList* cmdList);

		GeomHandle
		AddCubeGeom(MaterialHandle material = {}) override;

		GeomHandle
		AddSphereGeom(
			uint32_t       xSegments,
			uint32_t       ySegments,
			float          radius,
			MaterialHandle material = {}) override;

		GeomHandle
		AddPlaneGeom(
			uint32_t       xSegments,
			uint32_t       ySegments,
			float          width,
			float          height,
			MaterialHandle material = {}) override;

		GeomHandle
		AddStaticMeshGeom(const StaticMeshGeomDesc& desc) override;

		GeomHandle
		AddStaticMeshGeom(PreparedStaticMesh mesh, std::span<const MaterialHandle> materials)
			override;

		GrassHandle
		CreateGrass(const GrassDesc& desc) override;

		void
		UpdateGrass(GrassHandle grass, const GrassDesc& desc) override;

		[[nodiscard]] bool
		IsGrassAlive(GrassHandle grass) const noexcept
		{
			return grass.IsValid() && m_Grass.valid(grass.handle);
		}

		/**
		 * The grass fields a geom draws, for a view to list. Empty for a dead geom -- an instance
		 * may outlive its geom (see IScene::DeleteGeom), and its grass then draws nothing.
		 */
		[[nodiscard]] std::span<const GrassFieldRecord>
		GetGeomGrass(GeomHandle geom) const noexcept
		{
			if (!IsGeomAlive(geom))
			{
				return {};
			}
			return m_Geoms[geom.handle.index].grass;
		}

		/** The GrassLook record `grass` names, and the material it draws through. */
		struct GrassLookRef
		{
			uint32_t       entry = 0;
			MaterialHandle material;
		};

		/** @pre IsGrassAlive(grass). */
		[[nodiscard]] GrassLookRef
		GetGrassLook(GrassHandle grass) const noexcept
		{
			const GrassMeta& meta = m_Grass[grass.handle.index];
			return { meta.entry.index, meta.desc.material };
		}

		/**
		 * Moves whenever a view's grass list could change without any of its own placements
		 * changing: grass attached or released, a look rewritten. A SceneView polls it.
		 */
		[[nodiscard]] uint64_t
		GetGrassEpoch() const noexcept
		{
			return m_GrassEpoch;
		}

		void
		DeleteGrass(GrassHandle grass) override;

		TerrainHandle
		CreateTerrain(const TerrainDesc& desc) override;

		void
		DeleteTerrain(TerrainHandle terrain) override;

		[[nodiscard]] bool
		IsTerrainAlive(const TerrainHandle terrain) const noexcept override
		{
			return terrain.IsValid() && m_Terrains.valid(terrain.handle);
		}

		/** Slots a terrain may occupy: the bound a walk over TerrainAt runs to. */
		[[nodiscard]] uint32_t
		TerrainCapacity() const noexcept
		{
			return m_Terrains.capacity();
		}

		/** The terrain in slot `index`, or null where no live terrain holds it. */
		[[nodiscard]] const TerrainMeta*
		TerrainAt(const uint32_t index) const noexcept
		{
			return m_Terrains.allocated(index) ? &m_Terrains[index] : nullptr;
		}

		/** Moves whenever a terrain is created or deleted. */
		[[nodiscard]] uint64_t
		GetTerrainEpoch() const noexcept
		{
			return m_TerrainEpoch;
		}

		void
		AttachGrass(
			GeomHandle                    geom,
			const assetlib::BGrassFields& fields,
			uint32_t                      meshIndex,
			std::span<const GrassHandle>  looks) override;

		RigHandle
		AddRig(
			const assetlib::Skeleton&     skeleton,
			const assetlib::AnimationSet& animations,
			const FootPlantDesc&          footPlant = {},
			const BlendSetDesc&           blendSet  = {}) override;

		void
		SetRigBlendParameters(RigHandle rig, const BlendSetDesc& blendSet) override;

		void
		DeleteRig(RigHandle rig) override;

		ToonShadingRigHandle
		AddToonShadingRig(const ToonShadingRigDesc& desc) override;

		void
		DeleteToonShadingRig(ToonShadingRigHandle rig) override;

		/**
		 * The live rig `rig` names, or nullptr if the handle is null or already deleted. The
		 * pointer is into the entry buffer's metadata and is invalidated by the next AddToonShadingRig.
		 */
		[[nodiscard]] const ToonShadingRigMeta*
		FindToonShadingRig(ToonShadingRigHandle rig) const noexcept;

		/** A placement takes a use of `rig`. @pre FindToonShadingRig(rig) is not null. */
		void
		AcquireToonShadingRig(ToonShadingRigHandle rig) noexcept;

		/** A placement releases its use of `rig`. A no-op on a null or deleted handle. */
		void
		ReleaseToonShadingRig(ToonShadingRigHandle rig) noexcept;

		GeomHandle
		AddSkinnedMeshGeom(const SkinnedMeshGeomDesc& desc) override;

		TextureAssetHandle
		AddTextureAsset(assetlib::ImageData img, std::string debugName = "") override;

		void
		DeleteTextureAsset(TextureAssetHandle texture) override;

		MaterialHandle
		CreatePbrMaterial(const PbrMaterialDesc& desc) override;

		MaterialHandle
		CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc) override;

		MaterialHandle
		CreateSurfaceMaterial(const SurfaceMaterialDesc& desc) override;

		void
		UpdatePbrMaterial(MaterialHandle material, const PbrMaterialDesc& desc) override;

		void
		UpdateLoosePbrMaterial(MaterialHandle material, const LoosePbrMaterialDesc& desc) override;

		void
		UpdateSurfaceMaterial(MaterialHandle material, const SurfaceMaterialDesc& desc) override;

		void
		DeleteMaterial(MaterialHandle material) override;

		void
		SetSubmeshMaterial(GeomHandle geom, uint32_t submeshIndex, MaterialHandle material)
			override;

		void
		SetGround(const GroundPlaneDesc& ground) override;

		[[nodiscard]] const GroundPlaneDesc&
		GetGround() const noexcept override
		{
			return m_Ground;
		}

		void
		SetFootPlanting(bool enabled) noexcept override
		{
			if (m_FootPlanting != enabled)
			{
				m_FootPlanting = enabled;
				++m_TemporalEpoch;
			}
		}

		[[nodiscard]] bool
		GetFootPlanting() const noexcept override
		{
			return m_FootPlanting;
		}

		void
		DeleteGeom(GeomHandle geom) override;

	private:
		/**
		 * The tail every procedural primitive shares: meshletize `indices`, upload the vertex, vertex-map,
		 * index and meshlet pools, and register the result as one single-submesh geometry asset.
		 *
		 * `verts` is packed verbatim, so it must already be in the 48-byte procedural layout.
		 *
		 * @throws SceneError if the primitive needs more meshlets than one DispatchMesh can launch, or if
		 *         a buffer allocation fails.
		 */
		GeomHandle
		AddProceduralGeom(
			std::span<const VertexGen>     verts,
			std::span<const uint32_t>      indices,
			MaterialHandle                 material,
			const std::optional<glm::vec4> boundingSphere = std::nullopt);

		/**
		 * AddStaticMeshGeom's body, with the one knob an animated geom needs: `sphereOverride`
		 * replaces every submesh's cooked bounding sphere, because a posed submesh's bind-pose
		 * bounds do not hold once its clips move it.
		 */
		GeomHandle
		AddPreparedMesh(
			PreparedStaticMesh              mesh,
			std::span<const MaterialHandle> materials,
			const std::optional<glm::vec4>  sphereOverride);

		/**
		 * Refuses a look no grass pass could draw; see IScene::CreateGrass for the rules. Static
		 * for the same reason ValidateSkinnedRig is, bar the material, which it reads off the
		 * handle alone.
		 */
		static void
		ValidateGrass(const GrassDesc& desc, std::string_view caller);

		/**
		 * Gives back what each of `fields` holds -- its look's use (see GrassMeta::useCount) and its
		 * chunk and clump ranges -- and empties it.
		 */
		void
		ReleaseGrass(std::vector<GrassFieldRecord>& fields) noexcept;

		/** The GrassLook record CreateGrass and UpdateGrass write for `desc`. */
		[[nodiscard]] static idl::GrassLook
		BuildGrassLook(const GrassDesc& desc) noexcept;

		/**
		 * Refuses a rig the pose pass could not walk or address: no bones, a `parent` that is not
		 * lower than its own bone's index, a clip set whose
		 * bone count disagrees with the skeleton's, an empty or zero-frame clip table, or a clip
		 * whose frames run past the end of the sample pool. The clip set's `skeletonSignature` is
		 * not among these -- computing one needs assetlib; see IScene::AddRig.
		 *
		 * `footPlant` is refused for more legs than `idl::cMaxLegsPerRig`, a bone outside the
		 * skeleton, a chain whose links are not directly parented, a sole normal that is not finite
		 * and nonzero, or a `plantWeights` that is not one byte per leg for every frame in the
		 * pool.
		 *
		 * Static-only, because it reads nothing of the scene: the checks are all about the
		 * containers agreeing with each other.
		 */
		static void
		ValidateSkinnedRig(
			const assetlib::Skeleton&     skeleton,
			const assetlib::AnimationSet& animations,
			const FootPlantDesc&          footPlant,
			const BlendSetDesc&           blendSet);

		/**
		 * What a sample run must be whichever door it arrives at: two or more samples, every
		 * parameter finite, and strictly increasing. `space` names it in a refusal.
		 *
		 * Here rather than inline at each door so an upload and a later parameter move refuse the
		 * same run in the same words -- the clip-side checks stay with ValidateSkinnedRig, since
		 * only it has the clip set to check against.
		 */
		static void
		ValidateBlendSpaceRun(size_t space, std::span<const BlendSpaceSampleDesc> samples);

		/**
		 * The live rig `rig` names, or nullptr if the handle is null or already deleted. The
		 * pointer is into the entry buffer's metadata and is invalidated by the next AddRig.
		 */
		[[nodiscard]] RigMeta*
		FindRig(RigHandle rig) noexcept;

		// Claims a geom slot, growing the table when it is full. Unlike the GPU arenas this is a
		// pure CPU side table, so it cannot fail on device memory.
		[[nodiscard]] core::slot_handle
		AllocateGeomSlot(const GeomRecord& record);

		// The bytes a record stores for `texture`, or for `fallback` when the caller named none.
		[[nodiscard]] idl::RawTextureHandle
		ResolveTexture(TextureAssetHandle texture, core::slot_handle fallback) const;

		// The desc -> GPU-struct conversion, shared by Create* and Update*, so a material built by
		// either route is byte-identical (including the default-texture fallbacks for absent maps).
		[[nodiscard]] idl::PbrMaterial
		BuildPbrMaterial(const PbrMaterialDesc& desc) const;

		/// A surface material packed for the arena: the record's bytes, and the kind they are filed
		/// under -- the surface's own, since a game row belongs to one surface.
		struct BuiltSurfaceMaterial
		{
			MaterialType           kind = MaterialType::kInvalid;
			std::vector<std::byte> payload;
		};

		/**
		 * Packs the surface named by `desc`: the engine's fixed part of the record, then the
		 * parameter block with each declared field at the offset reflection read for it.
		 *
		 * @throws SceneError if no surface has that name, if the layer is one no game row draws, or
		 *         if a value or texture names a field the surface does not declare.
		 */
		[[nodiscard]] BuiltSurfaceMaterial
		BuildSurfaceMaterial(const SurfaceMaterialDesc& desc) const;

		[[nodiscard]] idl::LoosePbrMaterial
		BuildLoosePbrMaterial(const LoosePbrMaterialDesc& desc) const;

		SceneDesc m_Desc;

		// The surfaces the graphics registered, by the name a material writes. Copied rather than
		// referenced: a scene outlives no graphics, but it is small and read on every material.
		std::vector<SurfaceType> m_Surfaces;
		std::string              m_NamePrefix;

		// One entry per live geom: where its submeshes sit in m_SubmeshBuffer, plus the animated extras.
		// The slot generation is what makes a GeomHandle expire when its geom is deleted (see
		// IsGeomAlive).
		core::slot_vector<GeomRecord> m_Geoms;

		// Moves whenever a submesh's default material does. SceneViews poll it; see MaterialEpoch.
		uint64_t m_MaterialEpoch = 0;

		// Moves on a change no motion vector describes. SceneViews poll it; see GetTemporalEpoch.
		uint64_t m_TemporalEpoch = 0;

		GroundPlaneDesc m_Ground;
		bool            m_FootPlanting = true;

		core::slot_vector<GrassMeta> m_Grass;
		uint64_t                     m_GrassEpoch = 0;

		core::slot_vector<TerrainMeta> m_Terrains;
		uint64_t                       m_TerrainEpoch = 0;

		// One ToonShadingRig per AddToonShadingRig, and its edits' keys in one range it owns.
		bgpu::EntryBuffer<idl::ToonShadingRig, ToonShadingRigMeta> m_ToonShadingRigs;
		bgpu::RangeBuffer<idl::ToonShadingRigKey>                  m_ToonShadingRigKeys;

		bgpu::EntryBuffer<idl::GrassLook>  m_GrassLooks;
		bgpu::RangeBuffer<idl::GrassChunk> m_GrassChunks;
		bgpu::RangeBuffer<idl::GrassClump> m_GrassClumps;

		// One record per live terrain, and every terrain's node bounds, a range each owns.
		bgpu::EntryBuffer<idl::Terrain>           m_TerrainRecords;
		bgpu::RangeBuffer<idl::TerrainNodeBounds> m_TerrainNodeBounds;

		// One default material per submesh of a range, keyed at its root. It rides on the RangeBuffer
		// as Meta, not a parallel array, so it is allocated and freed with the geometry it belongs to.
		using SubmeshDefaults = std::vector<MaterialHandle>;

		// One record per live geom, named by every placement of it. Its slot is not m_Geoms' -- the
		// two are different allocators -- so GeomRecord carries the handle.
		bgpu::EntryBuffer<idl::Geom> m_GeomBuffer;

		bgpu::RangeBuffer<idl::Submesh, SubmeshDefaults> m_SubmeshBuffer;
		bgpu::RangeBuffer<idl::Meshlet>                  m_MeshletBuffer;
		bgpu::RangeBuffer<idl::MeshletGroup>             m_MeshletGroupBuffer;
		bgpu::RangeBuffer<uint32_t>                      m_VertexMapBuffer;
		bgpu::RawBuffer<>                                m_VertexDataBuffer;
		bgpu::RangeBuffer<uint32_t>                      m_IndexBuffer;

		// Every material of every kind, each behind a header naming its MaterialType. One arena
		// rather than a buffer per kind: a new shading model is a payload and a tag, not a buffer,
		// a binding and a uniform key.
		bgpu::RawBuffer<MaterialType> m_Materials;

		// One clip table for every animated tier: a Clip means the same thing to both, so a second
		// buffer of the same element type would only be two things to grow.
		bgpu::RangeBuffer<idl::Clip> m_Clips;

		bgpu::EntryBuffer<idl::Rig, RigMeta> m_Rigs;
		bgpu::RangeBuffer<idl::SkinnedBone>  m_SkinnedBones;
		bgpu::RangeBuffer<idl::BoneSample>   m_BoneSamples;

		// Every rig's posed frames, written by RigFramesPass and read by the crowd tier's mesh
		// shader. The same storage-plus-offset-allocator the per-view palette uses, and it discards
		// on growth for the same reason -- but a table is written once rather than every frame, so
		// a growth re-queues every rig holding one instead of being free.
		BonePaletteBuffer    m_BoneAnimTables;
		std::vector<RigFill> m_PendingRigFills;

		// Both empty on every scene that holds no rig with an avatar; see AddSkinnedMeshGeom. The
		// weights are packed four bytes to a uint rather than typed: no backend agrees on a
		// structured buffer of bytes.
		bgpu::RangeBuffer<idl::SkinnedLegChain> m_SkinnedLegs;
		bgpu::RangeBuffer<uint32_t>             m_PlantWeights;

		// The node table every rig carries -- one clip node per clip, then its authored spaces --
		// and the samples those spaces address. Only the samples are empty on a scene whose rigs
		// author no blend set; a rig always has nodes.
		bgpu::RangeBuffer<idl::BlendNode>        m_BlendNodes;
		bgpu::RangeBuffer<idl::BlendSpaceSample> m_BlendSamples;

		std::array<bgpu::SamplerHandle, static_cast<size_t>(StandardSampler::kCount)> m_Samplers;

		core::SharedRef<bgpu::IResourceManager> m_ResourceManager;

		// Scene-owned so one scene's textures never ride another context's timeline -- an upload
		// must be ordered against the frames that sample it, which is why Update flushes it.
		// Constructed from m_ResourceManager, so it must stay declared after it.
		TextureAssetStore m_Textures;

		// Every buffer the scene imports into the frame graph, each with the name it is imported
		// under. Declared after the samples it names.
		static constexpr auto c_Buffers = std::tuple{
			NamedBuffer{ c_GeomBufferName, &Scene::m_GeomBuffer },
			NamedBuffer{ c_SubmeshBufferName, &Scene::m_SubmeshBuffer },
			NamedBuffer{ c_MeshletBufferName, &Scene::m_MeshletBuffer },
			NamedBuffer{ c_MeshletGroupBufferName, &Scene::m_MeshletGroupBuffer },
			NamedBuffer{ c_VertexMapBufferName, &Scene::m_VertexMapBuffer },
			NamedBuffer{ c_VertexDataBufferName, &Scene::m_VertexDataBuffer },
			NamedBuffer{ c_IndexBufferName, &Scene::m_IndexBuffer },
			NamedBuffer{ c_MaterialArenaBufferName, &Scene::m_Materials },
			NamedBuffer{ c_ClipBufferName, &Scene::m_Clips },
			NamedBuffer{ c_RigBufferName, &Scene::m_Rigs },
			NamedBuffer{ c_SkinnedBoneBufferName, &Scene::m_SkinnedBones },
			NamedBuffer{ c_BoneSampleBufferName, &Scene::m_BoneSamples },
			NamedBuffer{ c_SkinnedLegBufferName, &Scene::m_SkinnedLegs },
			NamedBuffer{ c_PlantWeightBufferName, &Scene::m_PlantWeights },
			NamedBuffer{ c_BlendNodeBufferName, &Scene::m_BlendNodes },
			NamedBuffer{ c_BlendSampleBufferName, &Scene::m_BlendSamples },
			NamedBuffer{ c_GrassLookBufferName, &Scene::m_GrassLooks },
			NamedBuffer{ c_GrassChunkBufferName, &Scene::m_GrassChunks },
			NamedBuffer{ c_GrassClumpBufferName, &Scene::m_GrassClumps },
			NamedBuffer{ c_ToonShadingRigBufferName, &Scene::m_ToonShadingRigs },
			NamedBuffer{ c_ToonShadingRigKeyBufferName, &Scene::m_ToonShadingRigKeys },
			NamedBuffer{ c_TerrainBufferName, &Scene::m_TerrainRecords },
			NamedBuffer{ c_TerrainNodeBoundsBufferName, &Scene::m_TerrainNodeBounds },
		};

		static_assert(HasDistinctNames(c_Buffers), "two scene buffers would import under one name");
	};
}
