#pragma once
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "fg/PassDesc.h"

namespace bgl
{
	enum class ResourceKind : uint8_t
	{
		kBuffer,
		kTexture
	};

	// Buffers ignore layout.
	struct AccessState
	{
		BarrierSync   sync   = BarrierSyncFlag::kNone;
		BarrierAccess access = BarrierAccessFlag::kNone;
		BarrierLayout layout = BarrierLayout::kUndefined;
	};

	struct PassBarriers
	{
		std::vector<BufferHandle>       bufferHandles;
		std::vector<BufferBarrierDesc>  bufferDescs;
		std::vector<TextureHandle>      textureHandles;
		std::vector<TextureBarrierDesc> textureDescs;

		[[nodiscard]] bool
		Empty() const noexcept
		{
			return bufferHandles.empty() && textureHandles.empty();
		}
	};

	/**
	 * Passes are connected based on the order of AddPass and the resource access
	 */
	class FrameGraph
	{
	public:
		FrameGraph()                      = default;
		FrameGraph(const FrameGraph&)     = delete;
		FrameGraph(FrameGraph&&) noexcept = default;

		FrameGraph&
		operator=(const FrameGraph&) = delete;

		FrameGraph&
		operator=(FrameGraph&&) noexcept = default;

		FrameGraph&
		ImportBuffer(std::string name, BufferHandle handle, AccessState initial = {});

		FrameGraph&
		ImportTexture(std::string name, TextureHandle handle, AccessState initial = {});

		FrameGraph&
		AddPass(PassDesc desc);

		void
		SetResourceNamespace(std::string resourceNamespace);

		void
		RegisterQueue(std::string name, CommandQueueHandle queue, CommandListHandle list);

		void
		Compile(IResourceManager* resourceManager);

		void
		Execute();

		[[nodiscard]] std::vector<std::string>
		ExecutionOrder() const;

		[[nodiscard]] bool
		WasCulled(std::string_view passName) const;

		[[nodiscard]] const PassBarriers&
		BarriersFor(std::string_view passName) const;

		[[nodiscard]] size_t
		ImportedResourceCount() const noexcept
		{
			return m_Imported.size();
		}

	private:
		struct ResAccess
		{
			std::string  name;
			ResourceKind kind;
			AccessState  state;
			bool         isWrite;
		};

		struct PassNode
		{
			PassDesc               desc;
			std::string            ns;
			std::vector<ResAccess> accesses;
			std::vector<int32_t>   deps;
			bool                   kept = false;
			PassBarriers           barriers;
		};

		struct ImportedRes
		{
			std::variant<BufferHandle, TextureHandle> handle;
			AccessState                               initial;
			AccessState                               current;
		};

		void
		DeriveBarriers(IResourceManager* resourceManager);

		[[nodiscard]] std::string
		ResolveName(std::string_view ns, std::string_view name) const;

		[[nodiscard]] bool
		WritesImported(const PassNode& pass) const;

		[[nodiscard]] const PassNode*
		FindPass(std::string_view name) const;

		struct QueueBinding
		{
			CommandQueueHandle queue;
			CommandListHandle  list;
		};

		std::vector<PassNode>                         m_Passes;
		std::unordered_map<std::string, ImportedRes>  m_Imported;
		std::unordered_map<std::string, QueueBinding> m_Queues;
		std::vector<size_t>                           m_Order;
		std::string                                   m_CurrentNamespace;
		bool                                          m_Compiled = false;
	};
}
