#include "scene/SceneView.h"
#include "constants/constants.h"
#include "fg/FrameGraph.h"
#include "scene/Scene.h"
#include "types/BaseInstance.h"
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
			instanceBufferDesc.blockSize = sizeof(BaseInstance) * 256;

			m_InstanceBuffer.Init(std::move(instanceBufferDesc), m_ResourceManager);
		}

		{
			auto staticMeshInstanceBufferDesc      = EntryBufferDesc();
			staticMeshInstanceBufferDesc.maxCount  = m_MaxInstances;
			staticMeshInstanceBufferDesc.debugName = "Static Mesh Instance Buffer";
			staticMeshInstanceBufferDesc.blockSize = sizeof(idl::StaticMeshInstance) * 256;

			m_StaticMeshInstanceBuffer.Init(
				std::move(staticMeshInstanceBufferDesc),
				m_ResourceManager);
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
			if (!m_StaticMeshInstanceBuffer.IsIndexValid(i))
				continue;

			const uint32_t geomIndex = m_StaticMeshInstanceBuffer.AtIndex(i).base.offset;
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
			auto staticMeshInstance      = idl::StaticMeshInstance();
			staticMeshInstance.base      = geom.handle;
			staticMeshInstance.transform = transform;

			auto staticMeshInstanceHandle = m_StaticMeshInstanceBuffer.Add(staticMeshInstance);

			const PsoType psoType = GetPsoFromGeomAndMaterial(geom.geomType, material.materialType);

			auto instance             = BaseInstance();
			instance.meshInstance     = staticMeshInstanceHandle;
			instance.materialInstance = material.handle;
			instance.psoType          = psoType;

			auto instanceHandle    = MeshInstanceHandle();
			instanceHandle.psoType = psoType;
			instanceHandle.handle  = m_InstanceBuffer.Add(std::move(instance));

			m_SceneRaw->IncGeomRef(geom.handle.index);

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
		if (!instance.IsValid() || !m_InstanceBuffer.IsValid(instance.handle))
		{
			throw SceneError(
				"MeshInstanceHandle passed to DeleteMeshInstance is invalid or already removed");
		}

		const auto& baseInstance = m_InstanceBuffer[instance.handle];

		const uint32_t staticMeshInstanceIndex = baseInstance.meshInstance.offset;
		gassert(
			m_StaticMeshInstanceBuffer.IsIndexValid(staticMeshInstanceIndex),
			"Mesh instance references a missing static mesh instance");

		const uint32_t geomIndex =
			m_StaticMeshInstanceBuffer.AtIndex(staticMeshInstanceIndex).base.offset;
		gassert(
			m_SceneRaw->IsGeomIndexValid(geomIndex),
			"Static mesh instance references a missing geom");

		m_SceneRaw->DecGeomRef(geomIndex);

		m_StaticMeshInstanceBuffer.EraseByIndex(staticMeshInstanceIndex);
		m_InstanceBuffer.Erase(instance.handle);
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
			std::vector<BaseInstance> tail(padded - count);
			for (BaseInstance& instance : tail)
			{
				instance.psoType = PsoType::kInvalid;
			}
			cmdList->WriteBuffer(
				m_InstanceBuffer.GetBufferHandle(),
				tail.data(),
				static_cast<size_t>(count) * sizeof(BaseInstance),
				tail.size() * sizeof(BaseInstance));
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
