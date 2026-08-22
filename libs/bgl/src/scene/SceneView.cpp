#include "scene/SceneView.h"
#include "fg/FrameGraph.h"
#include "idl/Constants.h"
#include "scene/Scene.h"
#include "scene/scene_buffer_names.h"
#include "types/SubmeshInstance.h"
#include "util/util.h"
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
			meshBufferDesc.blockSize    = sizeof(idl::Mesh) * 256;

			m_MeshBuffer.Init(std::move(meshBufferDesc), m_ResourceManager);
		}

		{
			// One entry, not m_InitialInstances: most views place no VAT instances at all, and the
			// arena grows on the first that does.
			auto vatStateBufferDesc         = EntryBufferDesc();
			vatStateBufferDesc.initialCount = 1;
			vatStateBufferDesc.debugName    = "Vat State Buffer";

			m_VatStates.Init(std::move(vatStateBufferDesc), m_ResourceManager);
		}

		{
			// One entry, for the same reason as the VAT states above.
			auto skinnedStateBufferDesc         = EntryBufferDesc();
			skinnedStateBufferDesc.initialCount = 1;
			skinnedStateBufferDesc.debugName    = "Skinned State Buffer";

			m_SkinnedStates.Init(std::move(skinnedStateBufferDesc), m_ResourceManager);
		}

		m_Palettes.Init(m_ResourceManager);

		{
			auto desc         = UploadBufferDesc();
			desc.initialCount = 1;
			desc.debugName    = "Posed Instances";

			m_PosedInstances.Init(std::move(desc), m_ResourceManager);
		}

		{
			auto desc         = UploadBufferDesc();
			desc.initialCount = 1;
			desc.debugName    = "Posed Meshes";

			m_PosedMeshes.Init(std::move(desc), m_ResourceManager);
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
		m_VatStates.Release();
		m_SkinnedStates.Release();
		m_Palettes.Release();
		m_PosedInstances.Release();
		m_PosedMeshes.Release();

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

		return WritePlacement(geom, transform, core::slot_handle{});
	}

	MeshInstanceHandle
	SceneView::CreateVatMeshInstance(
		GeomHandle             geom,
		glm::mat4              transform,
		const VatInstanceDesc& desc)
	{
		if (geom.geomType != GeomType::kVatMesh)
		{
			throw SceneError("GeomHandle passed to CreateVatMeshInstance must be of type kVatMesh");
		}

		if (!m_SceneRaw->IsGeomAlive(geom))
		{
			throw SceneError(
				"GeomHandle passed to CreateVatMeshInstance has expired or is invalid");
		}

		const Scene::AnimGeomInfo vat = m_SceneRaw->GetGeomVatInfo(geom.handle.index);
		if (desc.clip >= vat.clipCount)
		{
			throw SceneError(
				"VatInstanceDesc::clip passed to CreateVatMeshInstance is out of "
				"range for the geom's clip table");
		}

		auto state  = idl::VatState();
		state.geom  = vat.record;
		state.clip  = desc.clip;
		state.phase = desc.phase;
		state.rate  = desc.rate;

		const core::slot_handle stateHandle = m_VatStates.Add(state);
		try
		{
			return WritePlacement(geom, transform, stateHandle);
		}
		catch (...)
		{
			m_VatStates.Erase(stateHandle);
			throw;
		}
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

		// Two palettes, back to back: the pose at `time` and the pose at `prevTime`, which is what
		// lets the mesh shader write a motion vector without a history buffer.
		const uint32_t float4s = idl::cFloat4sPerBone * rig.boneCount * 2;

		const core::multi_slot_handle palette = m_Palettes.Allocate(float4s);

		auto state    = idl::SkinnedState();
		state.geom    = rig.record;
		state.clip    = desc.clip;
		state.phase   = desc.phase;
		state.rate    = desc.rate;
		state.palette = palette;

		const core::slot_handle stateHandle = m_SkinnedStates.Add(state);
		try
		{
			const MeshInstanceHandle instance = WritePlacement(geom, transform, stateHandle);

			m_MeshBuffer.MetaAt(instance.handle.index).palette   = palette;
			m_MeshBuffer.MetaAt(instance.handle.index).boneCount = rig.boneCount;
			m_PosedDirty                                         = true;
			return instance;
		}
		catch (...)
		{
			m_SkinnedStates.Erase(stateHandle);
			m_Palettes.Free(palette);
			throw;
		}
	}

	MeshInstanceHandle
	SceneView::WritePlacement(GeomHandle geom, glm::mat4 transform, core::slot_handle animState)
	{
		try
		{
			// Copied by value, and never revisited: from here the instance no longer refers to the
			// geom, only to the range its submeshes occupied. Deleting the geom out from under it
			// leaves it drawing whatever lands in that range next.
			const idl::RangeWithCount submeshes = m_SceneRaw->GetGeomSubmeshes(geom.handle.index);

			auto mesh      = idl::Mesh();
			mesh.transform = transform;
			mesh.submeshes = submeshes;

			// Only a real handle: assigning a null one would write its index over the null
			// sentinel the Entry defaults to. Which field it lands in follows the geom's type --
			// the two are never both set.
			if (animState)
			{
				if (geom.geomType == GeomType::kSkinnedMesh)
				{
					mesh.skinnedState = animState;
				}
				else
				{
					mesh.vatState = animState;
				}
			}

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
				if (instance.pso < c_PsoCount)
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

		if (meta.animState)
		{
			if (meta.geomType == GeomType::kSkinnedMesh)
			{
				m_SkinnedStates.Erase(meta.animState);
				m_Palettes.Free(meta.palette);
				m_PosedDirty = true;
			}
			else
			{
				m_VatStates.Erase(meta.animState);
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
		auto list     = std::vector<uint32_t>();
		auto meshes   = std::vector<uint32_t>();
		auto maxBones = uint32_t(0);

		for (uint32_t meshIndex = 0; meshIndex < m_MeshBuffer.Capacity(); ++meshIndex)
		{
			if (!m_MeshBuffer.IsIndexValid(meshIndex))
			{
				continue;
			}

			const MeshMeta& meta = m_MeshBuffer.MetaAt(meshIndex);
			if (meta.geomType == GeomType::kSkinnedMesh && meta.animState)
			{
				list.push_back(meta.animState.index);
				meshes.push_back(meshIndex);
				maxBones = std::max(maxBones, meta.boneCount);
			}
		}

		m_PosedInstances.Assign(list);
		m_PosedMeshes.Assign(meshes);
		m_MaxPosedBoneCount = maxBones;
		m_PosedDirty        = false;
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
			instance.material = material.handle;
		}

		instance.pso = SubmeshPso(geomType, material);
	}

	void
	SceneView::RefreshSubmeshInstance(uint32_t meshIndex, uint32_t submeshIndex)
	{
		const idl::Mesh& mesh = m_MeshBuffer.AtIndex(meshIndex);
		const MeshMeta&  meta = m_MeshBuffer.MetaAt(meshIndex);

		const core::slot_handle handle = meta.submeshInstances[submeshIndex];
		if (!m_InstanceBuffer.IsValid(handle))
		{
			return;
		}

		SubmeshInstance instance = m_InstanceBuffer[handle];

		const idl::Entry material = instance.material;
		const uint32_t   pso      = instance.pso;

		ResolveShading(
			instance,
			mesh.submeshes.range.offsetStart,
			meta.overrides[submeshIndex],
			meta.geomType);

		// Set marks the element's block dirty, so writing back an unchanged instance would re-upload
		// a whole block to change nothing.
		if (instance.material.offset != material.offset || instance.pso != pso)
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
		m_PosedMeshes.Update(cmdList);
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

			auto meshes = std::string(c_PosedMeshesName);
			fg.ImportBuffer(meshes, m_PosedMeshes.GetBufferHandle());
			resourceNames.push_back(std::move(meshes));

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
