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

		static constexpr std::array<BufferInfo, 3> c_InstanceBufferInfo = { {
			{ "scene.instanceBuffer" },
			{ "scene.meshInstanceBuffer" },
			{ "scene.compactedInstances" },
		} };

		// Each SceneView gets a process-unique namespace so views sharing one Scene
		// don't collide in the frame graph.
		std::atomic<uint32_t> g_NextViewId{ 0 };
	}

	SceneView::SceneView(
		const SceneHandle&                scene,
		uint32_t                          maxInstances,
		core::SharedRef<IResourceManager> resourceManager) :
		m_Scene(scene), m_ResourceManager(std::move(resourceManager)), m_MaxInstances(maxInstances)
	{
		m_SceneRaw = m_Scene->As<Scene>();
		gassert(m_SceneRaw != nullptr, "SceneView requires a valid Scene");

		m_NamePrefix = std::format("v{}:", g_NextViewId.fetch_add(1));

		{
			auto instanceBufferDesc = PackedBufferDesc();
			// Round up so the kInvalid tail padding (see Update) always fits: the
			// counting sort dispatches whole groups, so it may read up to a group past
			// the last instance.
			instanceBufferDesc.maxCount  = core::round_up(m_MaxInstances, idl::cHistogramGroupSize);
			instanceBufferDesc.debugName = "Instance Buffer";
			instanceBufferDesc.blockSize = sizeof(SubmeshInstance) * 256;

			m_InstanceBuffer.Init(std::move(instanceBufferDesc), m_ResourceManager);
		}

		{
			auto meshBufferDesc      = EntryBufferDesc();
			meshBufferDesc.maxCount  = m_MaxInstances;
			meshBufferDesc.debugName = "Mesh Buffer";
			meshBufferDesc.blockSize = sizeof(idl::Mesh) * 256;

			m_MeshBuffer.Init(std::move(meshBufferDesc), m_ResourceManager);
		}

		{
			auto compactedInstancesDesc = ComputeBufferDesc();
			compactedInstancesDesc.SetElement<uint32_t>();
			compactedInstancesDesc.maxCount  = m_MaxInstances;
			compactedInstancesDesc.debugName = "Compacted Instances";

			m_CompactedInstances.Init(std::move(compactedInstancesDesc), m_ResourceManager);
		}
	}

	SceneView::~SceneView() noexcept
	{
		// Nothing to release back to the Scene: instances reference geometry by value and keep
		// nothing alive. Dropping this view's buffers drops its instances with them.
		logger::trace("~SceneView");
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

			for (uint32_t s = 0; s < submeshCount; ++s)
			{
				auto instance         = SubmeshInstance();
				instance.meshInstance = meshHandle;
				instance.submeshIndex = s;

				ResolveShading(instance, submeshes.range.offsetStart, MaterialHandle{});

				meta.submeshInstances.push_back(m_InstanceBuffer.Add(std::move(instance)));
			}

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
		// Resolve an asset handle to a live RHI texture, optionally requiring a cube map.
		const auto resolve =
			[this](TextureAssetHandle asset, const char* name, bool requireCube) -> TextureHandle {
			auto texHandle = TextureHandle::From(asset);
			if (!m_ResourceManager->ValidTextureHandle(texHandle))
			{
				throw SceneError(
					std::format("SetEnvironmentMap: invalid {} texture asset handle", name));
			}
			if (requireCube && !m_ResourceManager->IsTextureCube(texHandle))
			{
				throw SceneError(std::format("SetEnvironmentMap: {} map must be a cube map", name));
			}
			return texHandle;
		};

		// irradiance and prefilter are cubemaps; the BRDF LUT is a 2D texture.
		m_EnvironmentMap.irradiance = resolve(desc.irradiance, "irradiance", true);
		m_EnvironmentMap.prefilter  = resolve(desc.prefilter, "prefilter", true);
		m_EnvironmentMap.brdfLut    = resolve(desc.brdfLut, "brdfLut", false);
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

		auto buffers = GetInstanceBuffers();
		std::apply(
			[cmdList](auto&... buffer) {
				(..., [&]() {
					if constexpr (is_compute_buffer_v<decltype(buffer)>)
					{
						buffer.Clear(cmdList);
					}
					else
					{
						buffer.Update(cmdList);
					}
				}());
			},
			buffers);

		const uint32_t count  = m_InstanceBuffer.Size();
		const uint32_t padded = core::round_up(count, idl::cHistogramGroupSize);
		if (padded > count)
		{
			std::vector<SubmeshInstance> tail(padded - count);
			for (SubmeshInstance& instance : tail)
			{
				instance.meshInstance.offset = 0xFFFFFFFFu;  // Null(): skipped by the sort
			}
			cmdList->WriteBuffer(
				m_InstanceBuffer.GetBufferHandle(),
				tail.data(),
				static_cast<size_t>(count) * sizeof(SubmeshInstance),
				tail.size() * sizeof(SubmeshInstance));
		}
	}

	void
	SceneView::AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx)
	{
		std::vector<std::string> updateBuffers;
		ImportResources(fg, updateBuffers);

		PassDesc desc;
		desc.SetName(std::format("SceneView Update {}", drawIdx));

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
	}
}
