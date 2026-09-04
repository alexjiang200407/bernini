#include "scene/SceneView.h"
#include "fg/FrameGraph.h"
#include "scene/Scene.h"
#include "scene/scene_buffer_names.h"
#include "types/SubmeshInstance.h"
#include "util/util.h"
#include <bgl_common/idl/Constants.h>
#include <core/math.h>

namespace bgl
{
	namespace
	{
		// The counting sort dispatches whole groups, so the instance buffer's tail past the live count
		// must read as skippable: a default SubmeshInstance names no mesh and carries pso kInvalid,
		// which both the histogram and the compaction skip.
		constexpr std::array<SubmeshInstance, idl::cHistogramGroupSize> c_InstanceTailPadding{};

		// Each SceneView gets a process-unique namespace so views sharing one Scene
		// don't collide in the frame graph.
		std::atomic<uint32_t> g_NextViewId{ 0 };

		static_assert(
			c_MaxLegsPerRig == idl::cMaxLegsPerRig,
			"FootIKDesc has a slot per leg the IDL lets a rig carry");

		idl::Ramp
		ToRecord(const WeightRamp& ramp) noexcept
		{
			return { ramp.from, ramp.to, ramp.start, ramp.end };
		}

		WeightRamp
		FromRecord(const idl::Ramp& ramp) noexcept
		{
			return { ramp.from, ramp.to, ramp.start, ramp.end };
		}

		idl::FootIKLeg
		ToRecord(const FootIKLegDesc& leg) noexcept
		{
			return { ToRecord(leg.position), ToRecord(leg.rotation) };
		}

		FootIKLegDesc
		FromRecord(const idl::FootIKLeg& leg) noexcept
		{
			return { FromRecord(leg.position), FromRecord(leg.rotation) };
		}

		void
		ValidateWeightRamp(const WeightRamp& ramp, uint32_t leg, std::string_view which)
		{
			const bool finite = std::isfinite(ramp.from) && std::isfinite(ramp.to) &&
			                    std::isfinite(ramp.start) && std::isfinite(ramp.end);
			if (!finite)
			{
				throw SceneError(
					std::format(
						"SetFootIK: leg {}'s {} ramp holds a non-finite field",
						leg,
						which));
			}
			if (ramp.from < 0.0f || ramp.from > 1.0f || ramp.to < 0.0f || ramp.to > 1.0f)
			{
				throw SceneError(
					std::format(
						"SetFootIK: leg {}'s {} weight must stay within [0, 1]",
						leg,
						which));
			}
			if (ramp.end < ramp.start)
			{
				throw SceneError(
					std::format("SetFootIK: leg {}'s {} ramp ends before it starts", leg, which));
			}
		}

		void
		ValidateFootIKLeg(const FootIKLegDesc& desc, uint32_t leg)
		{
			ValidateWeightRamp(desc.position, leg, "position");
			ValidateWeightRamp(desc.rotation, leg, "rotation");
		}
	}

	SceneView::SceneView(
		const SceneRef&                   scene,
		uint32_t                          initialInstances,
		core::SharedRef<IResourceManager> resourceManager) :
		m_Scene(scene), m_ResourceManager(std::move(resourceManager)),
		m_InitialInstances(initialInstances)
	{
		m_SceneRaw = m_Scene->As<Scene>();
		gassert(m_SceneRaw != nullptr, "SceneView requires a valid Scene");

		m_NamePrefix = std::format("v{}:", g_NextViewId.fetch_add(1));

		try
		{
			InitBuffers();
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	void
	SceneView::InitBuffers()
	{
		const uint32_t paddedInstances =
			core::round_up(m_InitialInstances, idl::cHistogramGroupSize);

		{
			auto instanceBufferDesc              = PackedBufferDesc();
			instanceBufferDesc.initialCount      = paddedInstances;
			instanceBufferDesc.capacityAlignment = idl::cHistogramGroupSize;
			instanceBufferDesc.debugName         = "Instance Buffer";
			instanceBufferDesc.blockSize         = sizeof(SubmeshInstance) * 256;

			m_InstanceBuffer.Init(std::move(instanceBufferDesc), m_ResourceManager);
		}

		{
			auto meshBufferDesc         = EntryBufferDesc();
			meshBufferDesc.initialCount = m_InitialInstances;
			meshBufferDesc.debugName    = "Mesh Buffer";
			meshBufferDesc.blockSize    = sizeof(idl::MeshInstance) * 256;

			m_MeshBuffer.Init(std::move(meshBufferDesc), m_ResourceManager);
		}

		{
			auto playbackDesc = RawBufferDesc();

			// One record of each kind: most views hold no animated placement at all, and the arena
			// grows on the first that does.
			playbackDesc.initialBytes =
				2 * idl::cRawPayloadOffset +
				static_cast<uint32_t>(sizeof(idl::SkinnedState) + sizeof(idl::SkinnedTableState));

			// The null record must cover the largest payload as well as its header, so a null
			// reference reads zeros for a whole record rather than the first live one.
			playbackDesc.nullRecordBytes =
				idl::cRawPayloadOffset +
				static_cast<uint32_t>(
					std::max(sizeof(idl::SkinnedState), sizeof(idl::SkinnedTableState)));

			playbackDesc.debugName = "Playback Arena";

			m_Playback.Init(std::move(playbackDesc), m_ResourceManager);
		}

		m_Palettes.Init(m_ResourceManager);

		{
			auto footIKDesc         = RangeBufferDesc();
			footIKDesc.initialCount = 1;
			footIKDesc.debugName    = "Foot IK Buffer";

			m_FootIK.Init(std::move(footIKDesc), m_ResourceManager);
		}

		{
			auto desc         = UploadBufferDesc();
			desc.initialCount = 1;
			desc.debugName    = "Posed Instances";

			m_PosedInstances.Init(std::move(desc), m_ResourceManager);
		}

		EnsureCullStateCount(1);
		m_TransparentSort.Init(paddedInstances, m_ResourceManager);

		{
			auto desc      = UploadBufferDesc();
			desc.debugName = "Selected Instances";

			m_CurrentSelectedInstances.Init(std::move(desc), m_ResourceManager);
		}
	}

	void
	SceneView::EnsureCullStateCount(uint32_t count)
	{
		const uint32_t padded =
			core::round_up(m_InstanceBuffer.Capacity(), idl::cHistogramGroupSize);

		while (m_CullStates.size() < count)
		{
			// Init part-way through leaves buffers behind that no destructor reclaims, and the
			// entry must not join the vector either: a later call would count it as already made.
			auto cullState = CullState();
			try
			{
				cullState.Init(padded, m_ResourceManager);
			}
			catch (...)
			{
				cullState.Release();
				throw;
			}

			m_CullStates.push_back(std::move(cullState));
		}
	}

	void
	SceneView::SyncInstanceScratch()
	{
		const uint32_t padded =
			core::round_up(m_InstanceBuffer.Capacity(), idl::cHistogramGroupSize);

		for (CullState& cullState : m_CullStates)
		{
			cullState.Resize(padded);
		}

		m_TransparentSort.Resize(padded);
	}

	SceneView::~SceneView() noexcept
	{
		// Nothing to release back to the Scene: instances reference geometry by value and keep
		// nothing alive. The GPU buffers are another matter -- none of these types has a
		// destructor, so a view that is not released here holds its resource-manager slots until
		// the manager itself goes, and a session that opens and closes views exhausts the pools.
		//
		// Deferred: frames recorded against this view may still be in flight.
		m_InstanceBuffer.Release();
		m_MeshBuffer.Release();
		m_Playback.Release();
		m_Palettes.Release();
		m_FootIK.Release();
		m_PosedInstances.Release();

		for (CullState& cullState : m_CullStates)
		{
			cullState.Release();
		}

		m_TransparentSort.Release();
		m_CurrentSelectedInstances.Release();

		logger::trace("~SceneView");
	}

	ViewMatrices
	SceneView::AdvanceCamera(uint64_t frameCounter, const ViewMatrices& current) noexcept
	{
		if (!m_CameraFrame.has_value())
		{
			m_PrevCamera = current;
		}
		else if (*m_CameraFrame != frameCounter)
		{
			m_PrevCamera = m_Camera;
		}

		m_CameraFrame = frameCounter;
		m_Camera      = current;

		return m_PrevCamera;
	}

	bool
	SceneView::AdvanceTemporalEpoch() noexcept
	{
		const uint64_t epoch = m_TemporalEpoch + m_SceneRaw->GetTemporalEpoch();
		const bool     moved = epoch != m_DrawnTemporalEpoch;

		m_DrawnTemporalEpoch = epoch;
		return moved;
	}

	MeshInstanceHandle
	SceneView::CreateStaticMeshInstance(GeomHandle geom, glm::mat4 transform)
	{
		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError(
				"GeomHandle passed to CreateStaticMeshInstance must be of type kStaticMesh");
		}

		if (!m_SceneRaw->IsGeomAlive(geom))
		{
			throw SceneError(
				"GeomHandle passed to CreateStaticMeshInstance has expired or is invalid");
		}

		// No playback record: a static placement's MeshInstance.playback stays null.
		return WritePlacement(geom, transform, 0);
	}

	MeshInstanceHandle
	SceneView::CreateSkinnedMeshInstance(
		GeomHandle                 geom,
		glm::mat4                  transform,
		const SkinnedInstanceDesc& desc)
	{
		if (geom.geomType != GeomType::kSkinnedMesh)
		{
			throw SceneError(
				"GeomHandle passed to CreateSkinnedMeshInstance must be of type kSkinnedMesh");
		}

		if (!m_SceneRaw->IsGeomAlive(geom))
		{
			throw SceneError(
				"GeomHandle passed to CreateSkinnedMeshInstance has expired or is invalid");
		}

		const Scene::AnimGeomInfo rig = m_SceneRaw->GetGeomSkinnedInfo(geom.handle.index);
		if (desc.clip >= rig.clipCount)
		{
			throw SceneError(
				"SkinnedInstanceDesc::clip passed to CreateSkinnedMeshInstance is out of "
				"range for the geom's clip table");
		}

		// The pose source is which record this placement gets, and nothing else records it: a hero
		// instance owns a palette the pose pass writes, a crowd one owns no storage at all.
		auto palette = core::multi_slot_handle();
		auto footIK  = core::multi_slot_handle();
		auto record  = idl::RawEntry();

		// One rollback for everything the spawn allocates: each of the three arenas can throw at
		// its ceiling, and whichever does, what the earlier ones handed out goes back.
		try
		{
			if (desc.source == PoseSource::kPerInstance)
			{
				// Two palettes, back to back: the pose at `time` and the pose at `prevTime`, which
				// is what lets the mesh shader write a motion vector without a history buffer.
				palette = m_Palettes.Allocate(idl::cFloat4sPerBone * rig.boneCount * 2);

				// Weight one on every leg, so an instance nobody writes plants as the baked
				// weights say.
				if (rig.legCount > 0)
				{
					auto defaults = core::static_vector<idl::FootIKLeg, c_MaxLegsPerRig>();
					for (uint32_t leg = 0; leg < rig.legCount; ++leg)
					{
						defaults.push_back(ToRecord(FootIKLegDesc()));
					}
					footIK = m_FootIK.Add(std::span(defaults.data(), defaults.size()));
				}

				auto state           = idl::SkinnedState();
				state.playback.rig   = rig.record;
				state.playback.clip  = desc.clip;
				state.playback.phase = desc.phase;
				state.playback.rate  = desc.rate;
				state.palette        = palette;

				record = m_Playback.AddRecord(
					idl::PlaybackType::kSkinned,
					std::as_bytes(std::span(&state, 1)));
			}
			else
			{
				// Asked for here rather than at AddRig, so a rig no crowd instance is spawned on
				// never pays for a table. RigFramesPass fills it before anything reads it this
				// frame.
				m_SceneRaw->RequestBoneAnimTable(RigHandle{ rig.record });

				auto state           = idl::SkinnedTableState();
				state.playback.rig   = rig.record;
				state.playback.clip  = desc.clip;
				state.playback.phase = desc.phase;
				state.playback.rate  = desc.rate;

				record = m_Playback.AddRecord(
					idl::PlaybackType::kSkinnedTable,
					std::as_bytes(std::span(&state, 1)));
			}

			const MeshInstanceHandle instance = WritePlacement(geom, transform, record.byteOffset);

			auto& meta   = m_MeshBuffer.MetaAt(instance.handle.index);
			meta.palette = palette;
			meta.footIK  = footIK;
			m_PosedDirty = true;
			return instance;
		}
		catch (...)
		{
			if (!record.Null())
			{
				m_Playback.Erase(record.byteOffset);
			}
			if (palette)
			{
				m_Palettes.Free(palette);
			}
			if (footIK)
			{
				m_FootIK.Erase(footIK);
			}
			throw;
		}
	}

	const MeshMeta&
	SceneView::FootIKMetaFor(MeshInstanceHandle instance, std::string_view what) const
	{
		if (!instance.IsValid() || !m_MeshBuffer.IsValid(instance.handle))
		{
			throw SceneError(
				std::format("{}: the MeshInstanceHandle is invalid or already removed", what));
		}

		const MeshMeta& meta = m_MeshBuffer.MetaAt(instance.handle.index);
		if (meta.geomType != GeomType::kSkinnedMesh || !meta.palette)
		{
			throw SceneError(
				std::format(
					"{}: the placement is not a skinned instance on the per-instance source",
					what));
		}
		if (!meta.footIK)
		{
			throw SceneError(std::format("{}: the instance's rig authored no legs", what));
		}
		return meta;
	}

	void
	SceneView::SetFootIK(MeshInstanceHandle instance, const FootIKDesc& desc)
	{
		const MeshMeta& meta = FootIKMetaFor(instance, "SetFootIK");

		// Judged whole before anything is written, so a refused desc leaves the record as it was.
		for (uint32_t leg = 0; leg < meta.footIK.count; ++leg)
		{
			ValidateFootIKLeg(desc.leg[leg], leg);
		}
		for (uint32_t leg = 0; leg < meta.footIK.count; ++leg)
		{
			m_FootIK.Set(meta.footIK, leg, ToRecord(desc.leg[leg]));
		}
	}

	FootIKDesc
	SceneView::GetFootIK(MeshInstanceHandle instance) const
	{
		const MeshMeta& meta = FootIKMetaFor(instance, "GetFootIK");

		auto desc = FootIKDesc();
		for (uint32_t leg = 0; leg < meta.footIK.count; ++leg)
		{
			desc.leg[leg] = FromRecord(m_FootIK.Get(meta.footIK, leg));
		}
		return desc;
	}

	MeshInstanceHandle
	SceneView::WritePlacement(GeomHandle geom, glm::mat4 transform, uint32_t animState)
	{
		try
		{
			// Copied by value, and never revisited: from here the instance no longer refers to the
			// geom, only to the range its submeshes occupied. Deleting the geom out from under it
			// leaves it drawing whatever lands in that range next.
			const idl::RangeWithCount submeshes = m_SceneRaw->GetGeomSubmeshes(geom.handle.index);

			auto mesh      = idl::MeshInstance();
			mesh.submeshes = submeshes;
			WriteInstanceTransform(mesh, transform);

			// One field for either tier: the record's own header says which, so nothing here
			// decides it. Zero stays zero, which is the null a static placement wants.
			mesh.playback = idl::RawEntry{ animState };

			auto meshHandle = m_MeshBuffer.Add(mesh);

			auto& meta     = m_MeshBuffer.MetaAt(meshHandle.index);
			meta.geomType  = geom.geomType;
			meta.animState = animState;

			const uint32_t submeshCount = submeshes.count;
			meta.submeshInstances.reserve(submeshCount);
			meta.overrides.assign(submeshCount, MaterialHandle{});
			meta.selected.assign(submeshCount, 0);

			for (uint32_t s = 0; s < submeshCount; ++s)
			{
				auto instance         = SubmeshInstance();
				instance.meshInstance = meshHandle;
				instance.submeshIndex = s;

				ResolveShading(
					instance,
					submeshes.range.offsetStart,
					MaterialHandle{},
					geom.geomType);

				// A drawable with no pipeline is not a drawable: HistogramInstances asserts on a pso
				// past the bucket count, and the sort would skip it regardless. A null slot is still
				// pushed, because overrides, selection marks and the epoch re-resolve all address a
				// submesh by its index in this vector.
				if (instance.pso < idl::c_PsoCount)
				{
					meta.submeshInstances.emplace_back(m_InstanceBuffer.Add(std::move(instance)));
				}
				else
				{
					meta.submeshInstances.emplace_back();
				}
			}

			SyncInstanceScratch();

			// m_SceneEpoch is deliberately not advanced: these instances are current, but their
			// siblings may not be, and marking the view clean would strand them on a stale material.

			// An instance that was not there last frame has no history to reproject from, and an
			// animated one arrives posed on a clip the previous frame never drew.
			++m_TemporalEpoch;

			auto instanceHandle   = MeshInstanceHandle();
			instanceHandle.handle = meshHandle;

			return instanceHandle;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	void
	SceneView::DeleteMeshInstance(MeshInstanceHandle instance)
	{
		if (!instance.IsValid() || !m_MeshBuffer.IsValid(instance.handle))
		{
			throw SceneError(
				"MeshInstanceHandle passed to DeleteMeshInstance is invalid or already removed");
		}

		const uint32_t meshIndex = instance.handle.index;
		auto&          meta      = m_MeshBuffer.MetaAt(meshIndex);

		// Erase every submesh-instance this mesh contributed to the sort buffer.
		for (const core::slot_handle submeshInstance : meta.submeshInstances)
		{
			if (m_InstanceBuffer.IsValid(submeshInstance))
			{
				m_InstanceBuffer.Erase(submeshInstance);
			}
		}

		if (meta.animState != 0)
		{
			m_Playback.Erase(meta.animState);

			// A palette and a place in the pose list belong to the per-instance source alone.
			if (meta.geomType == GeomType::kSkinnedMesh)
			{
				// Null on a crowd instance, whose record owns nothing of its own.
				if (meta.palette)
				{
					m_Palettes.Free(meta.palette);
				}
				if (meta.footIK)
				{
					m_FootIK.Erase(meta.footIK);
				}
				m_PosedDirty = true;
			}
		}

		m_MeshBuffer.EraseByIndex(meshIndex);
		++m_TemporalEpoch;

		// The erases above can move any dense index, selected or not -- but with no mark
		// anywhere, there is no list to stale.
		if (m_SelectionDirty || m_CurrentSelectedInstances.Size() != 0)
		{
			m_SelectionDirty = true;
		}
	}

	void
	SceneView::SetSubmeshSelected(MeshInstanceHandle instance, uint32_t submeshIndex, bool selected)
	{
		MeshMeta& meta = MetaFor(instance, submeshIndex, "SetSubmeshSelected");

		const uint8_t value = selected ? 1 : 0;
		if (meta.selected[submeshIndex] == value)
		{
			return;
		}

		meta.selected[submeshIndex] = value;
		m_SelectionDirty            = true;
	}

	void
	SceneView::ClearSelection() noexcept
	{
		for (uint32_t meshIndex = 0; meshIndex < m_MeshBuffer.Capacity(); ++meshIndex)
		{
			if (!m_MeshBuffer.IsIndexValid(meshIndex))
			{
				continue;
			}

			for (uint8_t& selected : m_MeshBuffer.MetaAt(meshIndex).selected)
			{
				if (selected != 0)
				{
					selected         = 0;
					m_SelectionDirty = true;
				}
			}
		}
	}

	bool
	SceneView::IsSubmeshSelected(MeshInstanceHandle instance, uint32_t submeshIndex) const
	{
		const MeshMeta& meta = MetaFor(instance, submeshIndex, "IsSubmeshSelected");

		return meta.selected[submeshIndex] != 0;
	}

	std::span<const uint32_t>
	SceneView::GetSelectedInstances()
	{
		if (m_SelectionDirty)
		{
			RebuildSelectedList();
		}

		return m_CurrentSelectedInstances.Values();
	}

	void
	SceneView::RebuildPosedList()
	{
		auto list = std::vector<idl::PosedInstance>();

		for (uint32_t meshIndex = 0; meshIndex < m_MeshBuffer.Capacity(); ++meshIndex)
		{
			if (!m_MeshBuffer.IsIndexValid(meshIndex))
			{
				continue;
			}

			// Owning a palette is the predicate, not the record's kind: this list is what the pose
			// pass writes into, so it is exactly the instances that have somewhere for it to write.
			// What goes in it is the placement and not its playback record: the pose pass reads the
			// instance's transform from the one and reaches the other through it.
			const MeshMeta& meta = m_MeshBuffer.MetaAt(meshIndex);
			if (meta.geomType == GeomType::kSkinnedMesh && meta.animState != 0 && meta.palette)
			{
				auto& entry  = list.emplace_back();
				entry.mesh   = meshIndex;
				entry.footIK = meta.footIK;
			}
		}

		m_PosedInstances.Assign(list);
		m_PosedDirty = false;
	}

	void
	SceneView::RebuildSelectedList()
	{
		auto list = std::vector<uint32_t>();

		for (uint32_t meshIndex = 0; meshIndex < m_MeshBuffer.Capacity(); ++meshIndex)
		{
			if (!m_MeshBuffer.IsIndexValid(meshIndex))
			{
				continue;
			}

			const MeshMeta& meta = m_MeshBuffer.MetaAt(meshIndex);

			for (uint32_t s = 0; s < meta.selected.size(); ++s)
			{
				if (meta.selected[s] == 0)
				{
					continue;
				}

				const core::slot_handle handle = meta.submeshInstances[s];
				if (m_InstanceBuffer.IsValid(handle))
				{
					list.push_back(m_InstanceBuffer.GetDenseIndex(handle));
				}
			}
		}

		m_CurrentSelectedInstances.Assign(list);
		m_SelectionDirty = false;
	}

	MeshMeta&
	SceneView::MetaFor(MeshInstanceHandle instance, uint32_t submeshIndex, const char* what)
	{
		if (!instance.IsValid() || !m_MeshBuffer.IsValid(instance.handle))
		{
			throw SceneError(
				std::format("MeshInstanceHandle passed to {} is invalid or already removed", what));
		}

		MeshMeta& meta = m_MeshBuffer.MetaAt(instance.handle.index);

		if (submeshIndex >= meta.submeshInstances.size())
		{
			throw SceneError(std::format("submeshIndex passed to {} is out of range", what));
		}

		return meta;
	}

	void
	SceneView::SetSubmeshMaterialOverride(
		MeshInstanceHandle instance,
		uint32_t           submeshIndex,
		MaterialHandle     material)
	{
		if (!material.IsValid())
		{
			throw SceneError("Invalid MaterialHandle passed to SetSubmeshMaterialOverride");
		}

		MeshMeta& meta = MetaFor(instance, submeshIndex, "SetSubmeshMaterialOverride");

		if (!AcceptsMaterial(meta.geomType, material))
		{
			throw SceneError(
				"SetSubmeshMaterialOverride: an animated instance takes a kPBR material -- neither "
				"animated pipeline has an unlit or loose variant");
		}

		meta.overrides[submeshIndex] = material;
		++m_TemporalEpoch;

		RefreshSubmeshInstance(instance.handle.index, submeshIndex);
	}

	void
	SceneView::ClearSubmeshMaterialOverride(MeshInstanceHandle instance, uint32_t submeshIndex)
	{
		MeshMeta& meta = MetaFor(instance, submeshIndex, "ClearSubmeshMaterialOverride");

		meta.overrides[submeshIndex] = MaterialHandle{};
		++m_TemporalEpoch;

		RefreshSubmeshInstance(instance.handle.index, submeshIndex);
	}

	void
	SceneView::SetEnvironmentMap(const EnvironmentMapDesc& desc)
	{
		// Resolve an asset handle to the view the scene created for it, optionally requiring a cube map.
		const auto resolve = [this](TextureAssetHandle asset, const char* name, bool requireCube) {
			const auto texHandle = TextureHandle::From(asset);
			if (!m_ResourceManager->ValidTextureHandle(texHandle))
			{
				throw SceneError(
					std::format("SetEnvironmentMap: invalid {} texture asset handle", name));
			}
			if (requireCube && !m_ResourceManager->IsTextureCube(texHandle))
			{
				throw SceneError(std::format("SetEnvironmentMap: {} map must be a cube map", name));
			}
			return m_SceneRaw->GetTextureSrv(asset.textureSlot);
		};

		m_EnvironmentMap.irradiance = resolve(desc.irradiance, "irradiance", true);
		m_EnvironmentMap.prefilter  = resolve(desc.prefilter, "prefilter", true);
		++m_TemporalEpoch;
	}

	void
	SceneView::SetExposure(float exposure)
	{
		// reject NaN and negative exposure values, which would propagate through the
		// tone map and blank the frame or drive log2 of a negative into AgX.
		if (!std::isfinite(exposure) || exposure < 0.0f)
		{
			throw SceneError(
				std::format(
					"SetExposure: exposure must be finite and non-negative, got {}",
					exposure));
		}

		m_Exposure = exposure;
	}

	void
	SceneView::SetSkyBox(SkyboxDesc desc)
	{
		auto cubeTex = TextureHandle::From(desc.skyboxCubeTex);
		if (!m_ResourceManager->ValidTextureHandle(cubeTex))
		{
			throw SceneError("SetSkyBox: invalid skybox texture asset handle");
		}
		if (!m_ResourceManager->IsTextureCube(cubeTex))
		{
			throw SceneError("SetSkyBox: skybox texture must be a cube map");
		}

		m_Skybox = std::make_optional(std::move(desc));
		++m_TemporalEpoch;
	}

	void
	SceneView::ResolveShading(
		SubmeshInstance& instance,
		uint32_t         submeshRoot,
		MaterialHandle   materialOverride,
		GeomType         geomType) const
	{
		const MaterialHandle material =
			materialOverride.IsValid() ?
				materialOverride :
				m_SceneRaw->GetSubmeshDefaultMaterial(submeshRoot, instance.submeshIndex);

		// An invalid handle leaves the entry alone; the kNull PSO's pixel shader never reads it.
		if (material.IsValid())
		{
			instance.material = idl::RawEntry{ material.byteOffset };
		}

		instance.pso = SubmeshPso(geomType, material);
	}

	void
	SceneView::RefreshSubmeshInstance(uint32_t meshIndex, uint32_t submeshIndex)
	{
		const idl::MeshInstance& mesh = m_MeshBuffer.AtIndex(meshIndex);
		const MeshMeta&          meta = m_MeshBuffer.MetaAt(meshIndex);

		const core::slot_handle handle = meta.submeshInstances[submeshIndex];
		if (!m_InstanceBuffer.IsValid(handle))
		{
			return;
		}

		SubmeshInstance instance = m_InstanceBuffer[handle];

		const idl::RawEntry material = instance.material;
		const uint32_t      pso      = instance.pso;

		ResolveShading(
			instance,
			mesh.submeshes.range.offsetStart,
			meta.overrides[submeshIndex],
			meta.geomType);

		// Set marks the element's block dirty, so writing back an unchanged instance would re-upload
		// a whole block to change nothing.
		if (instance.material.byteOffset != material.byteOffset || instance.pso != pso)
		{
			m_InstanceBuffer.Set(handle, instance);
		}
	}

	void
	SceneView::ReresolveInstances()
	{
		// Slots are not compacted, so the live meshes are the allocated indices.
		for (uint32_t meshIndex = 0; meshIndex < m_MeshBuffer.Capacity(); ++meshIndex)
		{
			if (!m_MeshBuffer.IsIndexValid(meshIndex))
			{
				continue;
			}

			const MeshMeta& meta = m_MeshBuffer.MetaAt(meshIndex);

			for (uint32_t s = 0; s < meta.submeshInstances.size(); ++s)
			{
				// An override outranks the default, so a default change is not its business.
				if (meta.overrides[s].IsValid())
				{
					continue;
				}

				RefreshSubmeshInstance(meshIndex, s);
			}
		}
	}

	void
	SceneView::Update(ICommandList* cmdList)
	{
		// Must run before the flush below, so what it rewrites is uploaded in the same Update.
		if (const uint64_t epoch = m_SceneRaw->MaterialEpoch(); epoch != m_SceneEpoch)
		{
			ReresolveInstances();
			m_SceneEpoch = epoch;
		}

		for (CullState& cullState : m_CullStates)
		{
			cullState.Update(cmdList);
		}

		m_TransparentSort.Update(cmdList);

		if (m_SelectionDirty)
		{
			RebuildSelectedList();
		}
		m_CurrentSelectedInstances.Update(cmdList);

		if (m_PosedDirty)
		{
			RebuildPosedList();
		}
		m_PosedInstances.Update(cmdList);
		m_Palettes.Update(cmdList);

		ForEachNamedBuffer(*this, c_Buffers, [cmdList](std::string_view, auto& buffer) {
			buffer.Update(cmdList);
		});

		const uint32_t count  = m_InstanceBuffer.Size();
		const uint32_t padded = core::round_up(count, idl::cHistogramGroupSize);
		if (padded > count)
		{
			cmdList->WriteBuffer(
				m_InstanceBuffer.GetBufferHandle(),
				c_InstanceTailPadding.data(),
				static_cast<size_t>(count) * sizeof(SubmeshInstance),
				static_cast<size_t>(padded - count) * sizeof(SubmeshInstance));
		}
	}

	void
	SceneView::AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx)
	{
		std::vector<std::string> updateBuffers;
		ImportResources(fg, updateBuffers);

		PassDesc desc;
		desc.SetName("SceneView Update {}", drawIdx);

		for (const std::string& buffer : updateBuffers)
		{
			desc.AddBufferArg(
				BufferArg{ buffer, BarrierSyncFlag::kCopy, BarrierAccessFlag::kCopyDest });
		}

		desc.SetExec([this](const PassContext& ctx) { Update(ctx.GetCommandList()); });

		fg.AddPass(std::move(desc));
	}

	void
	SceneView::ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames)
	{
		fg.SetResourceNamespace(m_NamePrefix);

		resourceNames.reserve(resourceNames.size() + std::tuple_size_v<decltype(c_Buffers)>);

		ForEachNamedBuffer(*this, c_Buffers, [&](std::string_view name, const auto& buffer) {
			fg.ImportBuffer(name, buffer.GetBufferHandle());
			resourceNames.emplace_back(name);
		});

		m_TransparentSort.ImportResources(fg, resourceNames);

		{
			// Rebuilt before the handle is read: a stale list can grow the buffer, and growth mints
			// a new handle -- importing the old one would hand the mask pass a retired resource
			// that this frame's upload never reaches.
			if (m_SelectionDirty)
			{
				RebuildSelectedList();
			}

			auto name = std::string(c_SelectedInstancesName);
			fg.ImportBuffer(name, m_CurrentSelectedInstances.GetBufferHandle());
			resourceNames.push_back(std::move(name));
		}

		{
			// Same order as the selection list above, and for the same reason: rebuilding can grow the
			// buffer, and a growth mints a new handle.
			if (m_PosedDirty)
			{
				RebuildPosedList();
			}

			auto posed = std::string(c_PosedInstancesName);
			fg.ImportBuffer(posed, m_PosedInstances.GetBufferHandle());
			resourceNames.push_back(std::move(posed));

			auto palettes = std::string(c_BonePaletteName);
			fg.ImportBuffer(palettes, m_Palettes.GetBufferHandle());
			resourceNames.push_back(std::move(palettes));
		}

		// Each frustum's outputs get their own scope inside the view's, so N of them can carry the
		// same names without aliasing. The view's own imports stay outside, shared by all of them.
		for (uint32_t cullIdx = 0; cullIdx < m_CullStates.size(); ++cullIdx)
		{
			m_CullStates[cullIdx].ImportResources(fg, GetCullNamespace(cullIdx), resourceNames);
		}

		fg.SetResourceNamespace(m_NamePrefix);
	}
}
