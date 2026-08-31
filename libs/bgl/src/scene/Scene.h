#pragma once
#include "idl/idl.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/NamedBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/RangeBuffer.h"
#include "scene/RawBuffer.h"
#include "scene/TextureAssetStore.h"
#include "scene/scene_buffer_names.h"
#include "types/SubmeshInstance.h"
#include "types/VertexGen.h"
#include <bgl/IScene.h>
#include <core/containers/slot_vector.h>

namespace bgl
{
	class ICommandList;
	class FrameGraph;

	/**
	 * One live geom. Every geom has a submesh range; a kVatMesh one additionally owns its VatGeom
	 * entry (whose Range fields name the clip and column ranges DeleteGeom frees) and a kSkinnedMesh
	 * one its SkinnedGeom entry (naming the bone, sample and clip ranges). Both record their clip
	 * count for instance-creation validation.
	 *
	 * Namespace-scope rather than nested in Scene: a nested class's default member initializers
	 * only resolve once the enclosing class is complete, which would leave this
	 * not-default-constructible right where slot_vector's concept checks it.
	 */
	struct GeomRecord
	{
		idl::RangeWithCount submeshes;
		core::slot_handle   vatGeom;
		core::slot_handle   skinnedGeom;
		uint32_t            clipCount = 0;
		uint32_t            boneCount = 0;  // kSkinnedMesh only
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

		Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager);
		~Scene() noexcept override { logger::trace("~Scene"); }
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
		[[nodiscard]] RawArenaBinding
		GetMaterialBinding() const noexcept
		{
			return RawArenaBinding{ m_Materials.GetBufferHandle(), m_Materials.GetHandleView() };
		}

		[[nodiscard]] auto&
		GetClipBuffer() noexcept
		{
			return m_Clips;
		}

		[[nodiscard]] auto&
		GetSkinnedGeomBuffer() noexcept
		{
			return m_SkinnedGeoms;
		}

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

		// --- SceneView support -------------------------------------------------
		// Instances live in SceneViews and reference this Scene's geometry by value: a view copies
		// the submesh range below into its per-placement Mesh. The Scene keeps no record of who
		// placed what, so the caller owns the ordering -- see IScene::DeleteGeom.

		[[nodiscard]] bool
		IsGeomAlive(GeomHandle geom) const noexcept override
		{
			return geom.IsValid() && m_Geoms.valid(geom.handle);
		}

		// The submesh range a SceneView copies into a per-placement Mesh at instance-creation time.
		// Only valid while the geom is alive; check IsGeomAlive first.
		[[nodiscard]] const idl::RangeWithCount&
		GetGeomSubmeshes(uint32_t index) const noexcept
		{
			return m_Geoms[index].submeshes;
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

			// Bones the rig carries, which is what sizes an instance's palette. 0 on a VAT geom: its
			// pose is fetched, not composed, so nothing on that path needs a bone count.
			uint32_t boneCount = 0;
		};

		[[nodiscard]] AnimGeomInfo
		GetGeomVatInfo(uint32_t index) const noexcept
		{
			const GeomRecord& geom = m_Geoms[index];
			return { geom.vatGeom, geom.clipCount };
		}

		[[nodiscard]] AnimGeomInfo
		GetGeomSkinnedInfo(uint32_t index) const noexcept
		{
			const GeomRecord& geom = m_Geoms[index];
			return { geom.skinnedGeom, geom.clipCount, geom.boneCount };
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

		[[nodiscard]] SamplerHandle
		GetSampler(StandardSampler kind) const noexcept
		{
			return m_Samplers[static_cast<size_t>(kind)];
		}

		// The view this scene created for a texture asset, or a null handle if it created none.
		[[nodiscard]] SrvHandle
		GetTextureSrv(core::slot_handle textureSlot) const noexcept
		{
			return m_Textures.GetSrv(textureSlot);
		}

		void
		AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx);

		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames);

		void
		Update(ICommandList* cmdList);

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
		AddStaticMeshGeom(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials) override;

		GeomHandle
		AddStaticMeshGeom(PreparedStaticMesh mesh, std::span<const MaterialHandle> materials)
			override;

		GeomHandle
		AddVatMeshGeom(
			std::span<const VatVertex> verts,
			std::span<const uint32_t>  indices,
			const VatGeomDesc&         desc,
			MaterialHandle             material) override;

		GeomHandle
		AddVatMeshGeom(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials,
			const VatGeomDesc&              desc) override;

		GeomHandle
		AddSkinnedMeshGeom(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials,
			const assetlib::Skeleton&       skeleton,
			const assetlib::AnimationSet&   animations,
			const assetlib::Bounds&         posedBounds,
			const FootPlantDesc&            footPlant = {}) override;

		TextureAssetHandle
		AddTextureAsset(assetlib::ImageData img, std::string debugName = "") override;

		void
		DeleteTextureAsset(TextureAssetHandle texture) override;

		MaterialHandle
		CreatePbrMaterial(const PbrMaterialDesc& desc) override;

		MaterialHandle
		CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc) override;

		void
		UpdatePbrMaterial(MaterialHandle material, const PbrMaterialDesc& desc) override;

		void
		UpdateLoosePbrMaterial(MaterialHandle material, const LoosePbrMaterialDesc& desc) override;

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
		 * AddStaticMeshGeom's body, with the one knob VAT needs: `sphereOverride` replaces every
		 * submesh's cooked bounding sphere, because a VAT submesh's bind-pose bounds do not hold
		 * once its clips move it.
		 */
		GeomHandle
		AddPreparedMesh(
			PreparedStaticMesh              mesh,
			std::span<const MaterialHandle> materials,
			const std::optional<glm::vec4>  sphereOverride);

		/**
		 * Refuses a VatGeomDesc whose textures are not live scene assets, whose clip table is
		 * empty, or that carries a zero-frame clip.
		 */
		void
		ValidateVatDesc(const VatGeomDesc& desc) const;

		/**
		 * Refuses a rig the pose pass could not walk or address: no bones, a `parent` that is not
		 * lower than its own bone's index, a clip set whose
		 * bone count disagrees with the skeleton's, an empty or zero-frame clip table, or a clip
		 * whose frames run past the end of the sample pool. The clip set's `skeletonSignature` is
		 * not among these -- computing one needs assetlib; see IScene::AddSkinnedMeshGeom.
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
			const FootPlantDesc&          footPlant);

		/**
		 * AttachVatRecords' counterpart: allocates the bone, sample, clip, leg and plant-weight
		 * ranges plus the SkinnedGeom record onto `base` and flips it to kSkinnedMesh. On any
		 * failure the geometry half is taken back down (DeleteGeom) so a failed skinned add leaks
		 * nothing.
		 */
		GeomHandle
		AttachSkinnedRecords(
			GeomHandle                    base,
			const assetlib::Skeleton&     skeleton,
			const assetlib::AnimationSet& animations,
			const FootPlantDesc&          footPlant);

		/**
		 * The tail AddVatMeshGeom and AddVatMeshGeom share: allocates the clip and column ranges plus the
		 * VatGeom record onto `base` and flips it to kVatMesh. On any failure the geometry half is
		 * taken back down (DeleteGeom) so a failed VAT add leaks nothing.
		 */
		GeomHandle
		AttachVatRecords(
			GeomHandle                base,
			const VatGeomDesc&        desc,
			std::span<const uint32_t> columnBases);

		/**
		 * Sizes every GPU-mirrored buffer to its SceneDesc starting point.
		 *
		 * @throws std::runtime_error if the device cannot allocate one; the constructor converts it
		 *         to SceneError, so a caller only ever sees the documented type.
		 */
		void
		InitBuffers();

		// Claims a geom slot, growing the table when it is full. Unlike the GPU arenas this is a
		// pure CPU side table, so it cannot fail on device memory.
		[[nodiscard]] core::slot_handle
		AllocateGeomSlot(const GeomRecord& record);

		// The desc -> GPU-struct conversion, shared by Create* and Update*, so a material built by
		// either route is byte-identical (including the default-texture fallbacks for absent maps).
		[[nodiscard]] idl::PbrMaterial
		BuildPbrMaterial(const PbrMaterialDesc& desc) const;

		[[nodiscard]] idl::LoosePbrMaterial
		BuildLoosePbrMaterial(const LoosePbrMaterialDesc& desc) const;

		SceneDesc   m_Desc;
		std::string m_NamePrefix;

		// One entry per live geom: where its submeshes sit in m_SubmeshBuffer, plus the kVatMesh extras.
		// The slot generation is what makes a GeomHandle expire when its geom is deleted (see
		// IsGeomAlive).
		core::slot_vector<GeomRecord> m_Geoms;

		// Moves whenever a submesh's default material does. SceneViews poll it; see MaterialEpoch.
		uint64_t m_MaterialEpoch = 0;

		// Moves on a change no motion vector describes. SceneViews poll it; see GetTemporalEpoch.
		uint64_t m_TemporalEpoch = 0;

		GroundPlaneDesc m_Ground;

		// One default material per submesh of a range, keyed at its root. It rides on the RangeBuffer
		// as Meta, not a parallel array, so it is allocated and freed with the geometry it belongs to.
		using SubmeshDefaults = std::vector<MaterialHandle>;

		RangeBuffer<idl::Submesh, SubmeshDefaults> m_SubmeshBuffer;
		RangeBuffer<idl::Meshlet>                  m_MeshletBuffer;
		RangeBuffer<uint32_t>                      m_VertexMapBuffer;
		RawBuffer<>                                m_VertexDataBuffer;
		RangeBuffer<uint32_t>                      m_IndexBuffer;

		// Every material of every kind, each behind a header naming its MaterialType. One arena
		// rather than a buffer per kind: a new shading model is a payload and a tag, not a buffer,
		// a binding and a uniform key.
		RawBuffer<MaterialType> m_Materials;

		// One clip table for every animated tier: a Clip means the same thing to both, so a second
		// buffer of the same element type would only be two things to grow.
		RangeBuffer<idl::Clip> m_Clips;

		EntryBuffer<idl::VatGeom> m_VatGeoms;
		RangeBuffer<uint32_t>     m_VatColumns;

		EntryBuffer<idl::SkinnedGeom> m_SkinnedGeoms;
		RangeBuffer<idl::SkinnedBone> m_SkinnedBones;
		RangeBuffer<idl::BoneSample>  m_BoneSamples;

		// Both empty on every scene that holds no rig with an avatar; see AddSkinnedMeshGeom. The
		// weights are packed four bytes to a uint rather than typed: no backend agrees on a
		// structured buffer of bytes.
		RangeBuffer<idl::SkinnedLegChain> m_SkinnedLegs;
		RangeBuffer<uint32_t>             m_PlantWeights;

		std::array<SamplerHandle, static_cast<size_t>(StandardSampler::kCount)> m_Samplers;

		core::SharedRef<IResourceManager> m_ResourceManager;

		// Scene-owned so one scene's textures never ride another context's timeline -- an upload
		// must be ordered against the frames that sample it, which is why Update flushes it.
		// Constructed from m_ResourceManager, so it must stay declared after it.
		TextureAssetStore m_Textures;

		// Every buffer the scene imports into the frame graph, each with the name it is imported
		// under. Declared after the members it names.
		static constexpr auto c_Buffers = std::tuple{
			NamedBuffer{ c_SubmeshBufferName, &Scene::m_SubmeshBuffer },
			NamedBuffer{ c_MeshletBufferName, &Scene::m_MeshletBuffer },
			NamedBuffer{ c_VertexMapBufferName, &Scene::m_VertexMapBuffer },
			NamedBuffer{ c_VertexDataBufferName, &Scene::m_VertexDataBuffer },
			NamedBuffer{ c_IndexBufferName, &Scene::m_IndexBuffer },
			NamedBuffer{ c_MaterialArenaBufferName, &Scene::m_Materials },
			NamedBuffer{ c_VatGeomBufferName, &Scene::m_VatGeoms },
			NamedBuffer{ c_ClipBufferName, &Scene::m_Clips },
			NamedBuffer{ c_VatColumnBufferName, &Scene::m_VatColumns },
			NamedBuffer{ c_SkinnedGeomBufferName, &Scene::m_SkinnedGeoms },
			NamedBuffer{ c_SkinnedBoneBufferName, &Scene::m_SkinnedBones },
			NamedBuffer{ c_BoneSampleBufferName, &Scene::m_BoneSamples },
			NamedBuffer{ c_SkinnedLegBufferName, &Scene::m_SkinnedLegs },
			NamedBuffer{ c_PlantWeightBufferName, &Scene::m_PlantWeights },
		};

		static_assert(HasDistinctNames(c_Buffers), "two scene buffers would import under one name");
	};
}
