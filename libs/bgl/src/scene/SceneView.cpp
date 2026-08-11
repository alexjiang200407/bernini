#include "scene/SceneView.h"
#include "fg/FrameGraph.h"
#include "idl/Constants.h"
#include "scene/Scene.h"
#include "types/SubmeshInstance.h"
#include "util/util.h"
#include <core/math.h>

namespace bgl
{
	namespace
	{
		struct BufferInfo
		{
			std::string_view name;
		};

		// Paired positionally with SceneView::GetInstanceBuffers(); shorten one and you must
		// shorten the other.
		static constexpr std::array<BufferInfo, 2> c_InstanceBufferInfo = { {
			{ "scene.instanceBuffer" },
			{ "scene.meshInstanceBuffer" },
		} };

		constexpr std::string_view c_SelectedInstancesName = "scene.selectedInstances";

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

		EnsureCullStateCount(1);
		m_TransparentSort.Init(paddedInstances, m_ResourceManager);

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<uint32_t>()
				.SetInitialCount(paddedInstances)
				.SetDebugName("Selected Instances");

			m_SelectedInstances.Init(std::move(desc), m_ResourceManager);
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

		if (padded > m_SelectedInstances.GetDesc().initialCount)
		{
			// Resize discards the GPU contents; the CPU list is still current, so re-upload it.
			m_SelectedInstances.Resize(padded);
			m_SelectionUploadPending = true;
		}
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

		for (CullState& cullState : m_CullStates)
		{
			cullState.Release();
		}

		m_TransparentSort.Release();
		m_SelectedInstances.Release();

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

		try
		{
			// Copied by value, and never revisited: from here the instance no longer refers to the
			// geom, only to the range its submeshes occupied. Deleting the geom out from under it
			// leaves it drawing whatever lands in that range next.
			const idl::RangeWithCount submeshes = m_SceneRaw->GetGeomSubmeshes(geom.handle.index);

			auto mesh      = idl::Mesh();
			mesh.transform = transform;
			mesh.submeshes = submeshes;

			auto meshHandle = m_MeshBuffer.Add(mesh);

			auto& meta = m_MeshBuffer.MetaAt(meshHandle.index);

			const uint32_t submeshCount = submeshes.count;
			meta.submeshInstances.reserve(submeshCount);
			meta.overrides.assign(submeshCount, MaterialHandle{});
			meta.selected.assign(submeshCount, 0);

			for (uint32_t s = 0; s < submeshCount; ++s)
			{
				auto instance         = SubmeshInstance();
				instance.meshInstance = meshHandle;
				instance.submeshIndex = s;

				ResolveShading(instance, submeshes.range.offsetStart, MaterialHandle{});

				meta.submeshInstances.push_back(m_InstanceBuffer.Add(std::move(instance)));
			}

			SyncInstanceScratch();

			// m_SceneEpoch is deliberately not advanced: these instances are current, but their
			// siblings may not be, and marking the view clean would strand them on a stale material.

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

		m_MeshBuffer.EraseByIndex(meshIndex);

		// The erases above can move any dense index, selected or not -- but with no mark
		// anywhere, there is no list to stale.
		if (m_SelectionDirty || !m_SelectedList.empty())
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

		return m_SelectedList;
	}

	void
	SceneView::RebuildSelectedList()
	{
		m_SelectedList.clear();

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
					m_SelectedList.push_back(m_InstanceBuffer.DenseIndexOf(handle));
				}
			}
		}

		m_SelectionDirty         = false;
		m_SelectionUploadPending = true;
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

		meta.overrides[submeshIndex] = material;

		RefreshSubmeshInstance(instance.handle.index, submeshIndex);
	}

	void
	SceneView::ClearSubmeshMaterialOverride(MeshInstanceHandle instance, uint32_t submeshIndex)
	{
		MeshMeta& meta = MetaFor(instance, submeshIndex, "ClearSubmeshMaterialOverride");

		meta.overrides[submeshIndex] = MaterialHandle{};

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
	}

	void
	SceneView::ResolveShading(
		SubmeshInstance& instance,
		uint32_t         submeshRoot,
		MaterialHandle   materialOverride) const
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

		instance.pso = SubmeshPso(GeomType::kStaticMesh, material);
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

		ResolveShading(instance, mesh.submeshes.range.offsetStart, meta.overrides[submeshIndex]);

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

		m_SelectedInstances.Update(cmdList);
		if (m_SelectionDirty)
		{
			RebuildSelectedList();
		}
		if (m_SelectionUploadPending)
		{
			if (!m_SelectedList.empty())
			{
				cmdList->WriteBuffer(
					m_SelectedInstances.GetBufferHandle(),
					m_SelectedList.data(),
					m_SelectedList.size() * sizeof(uint32_t));
			}

			m_SelectionUploadPending = false;
		}

		auto buffers = GetInstanceBuffers();
		std::apply([cmdList](auto&... buffer) { (..., buffer.Update(cmdList)); }, buffers);

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

		resourceNames.reserve(resourceNames.size() + c_InstanceBufferInfo.size());

		auto   buffers = GetInstanceBuffers();
		size_t i       = 0;
		std::apply(
			[&](auto&... buffer) {
				(..., [&] {
					std::string name(c_InstanceBufferInfo[i++].name);
					fg.ImportBuffer(name, buffer.GetBufferHandle());
					resourceNames.push_back(std::move(name));
				}());
			},
			buffers);

		m_TransparentSort.ImportResources(fg, resourceNames);

		{
			auto name = std::string(c_SelectedInstancesName);
			fg.ImportBuffer(name, m_SelectedInstances.GetBufferHandle());
			resourceNames.push_back(std::move(name));
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
