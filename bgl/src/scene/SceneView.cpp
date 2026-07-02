#include "scene/SceneView.h"
#include "constants/constants.h"
#include "fg/FrameGraph.h"
#include "scene/Scene.h"
#include "types/SubmeshInstance.h"
#include "util/util.h"
#include <bgl/PsoType.h>
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
			instanceBufferDesc.maxCount  = core::round_up(m_MaxInstances, c_HistogramGroupSize);
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
		// Every live instance contributed a refcount to its geom on the shared Scene.
		// Release them here so Scene::DeleteGeom works once this view is gone. The
		// Scene outlives the view (m_Scene keeps it alive), so the slots stay valid.
		for (uint32_t i = 0; i < m_MaxInstances; ++i)
		{
			if (!m_MeshBuffer.IsIndexValid(i))
				continue;

			const uint32_t geomIndex = m_MeshBuffer.MetaAt(i).geomIndex;
			if (m_SceneRaw != nullptr && m_SceneRaw->IsGeomIndexValid(geomIndex))
				m_SceneRaw->DecGeomRef(geomIndex);
		}

		logger::trace("~SceneView");
	}

	MeshInstanceHandle
	SceneView::CreateStaticMeshInstance(
		GeomHandle     geom,
		MaterialHandle material,
		glm::mat4      transform)
	{
		if (!material.IsValid())
		{
			throw SceneError("Invalid MaterialHandle passed to CreateStaticMeshInstance");
		}

		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError(
				"GeomHandle passed to CreateStaticMeshInstance must be of type kStaticMesh");
		}

		if (!m_SceneRaw->IsGeomSlotValid(geom.handle))
		{
			throw SceneError(
				"GeomHandle passed to CreateStaticMeshInstance has expired or is invalid");
		}

		try
		{
			// The per-placement Mesh copies the (tiny) submeshes descriptor from the
			// shared geometry asset; the heavy data stays in the Scene's buffers.
			const GeomAsset& asset = m_SceneRaw->GetGeomAsset(geom.handle.index);

			auto mesh              = idl::Mesh();
			mesh.transform         = transform;
			mesh.submeshes         = asset.submeshes;
			mesh.totalMeshletCount = asset.totalMeshletCount;

			auto meshHandle = m_MeshBuffer.Add(mesh);

			auto& meta     = m_MeshBuffer.MetaAt(meshHandle.index);
			meta.geomIndex = geom.handle.index;
			meta.geomType  = geom.geomType;

			const PsoType psoType = GetPsoFromGeomAndMaterial(geom.geomType, material.materialType);

			const uint32_t submeshCount = asset.submeshes.count;
			meta.submeshInstances.reserve(submeshCount);
			for (uint32_t s = 0; s < submeshCount; ++s)
			{
				auto instance         = SubmeshInstance();
				instance.meshInstance = meshHandle;
				instance.submeshIndex = s;
				instance.psoType      = psoType;

				meta.submeshInstances.push_back(m_InstanceBuffer.Add(std::move(instance)));
			}

			m_SceneRaw->IncGeomRef(geom.handle.index);

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

		const uint32_t geomIndex = meta.geomIndex;
		gassert(
			m_SceneRaw->IsGeomIndexValid(geomIndex),
			"Mesh record references a missing geom asset");

		m_SceneRaw->DecGeomRef(geomIndex);

		m_MeshBuffer.EraseByIndex(meshIndex);
	}

	void
	SceneView::SetSubmeshMaterial(
		MeshInstanceHandle instance,
		uint32_t           submeshIndex,
		MaterialHandle     material)
	{
		if (!instance.IsValid() || !m_MeshBuffer.IsValid(instance.handle))
		{
			throw SceneError(
				"MeshInstanceHandle passed to SetSubmeshMaterial is invalid or already removed");
		}

		if (!material.IsValid())
		{
			throw SceneError("Invalid MaterialHandle passed to SetSubmeshMaterial");
		}

		auto& meta = m_MeshBuffer.MetaAt(instance.handle.index);

		if (submeshIndex >= meta.submeshInstances.size())
		{
			throw SceneError("submeshIndex passed to SetSubmeshMaterial is out of range");
		}

		const core::slot_handle submeshInstance = meta.submeshInstances[submeshIndex];
		gassert(
			m_InstanceBuffer.IsValid(submeshInstance),
			"Submesh-instance handle referenced by a live mesh record is stale");

		// Recompute this submesh's PSO; Set marks the instance buffer dirty so Update
		// re-uploads it and the next frame's counting sort re-buckets it.
		const PsoType psoType = GetPsoFromGeomAndMaterial(meta.geomType, material.materialType);

		auto updated    = m_InstanceBuffer[submeshInstance];
		updated.psoType = psoType;
		m_InstanceBuffer.Set(submeshInstance, updated);
	}

	void
	SceneView::Update(ICommandList* cmdList)
	{
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
		const uint32_t padded = core::round_up(count, c_HistogramGroupSize);
		if (padded > count)
		{
			std::vector<SubmeshInstance> tail(padded - count);
			for (SubmeshInstance& instance : tail)
			{
				instance.psoType = PsoType::kInvalid;
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
