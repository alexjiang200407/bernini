#pragma once
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "debug/BufferPoisoner.h"
#include "fg/PassDesc.h"
#include <bgl_common/PassScheduler.h>
#include <core/str/str.h>

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

		/**
		 * Imports an external resource under `name`. The optional `initial` is its
		 * access state on entry to the graph; when omitted the FrameGraph reuses the
		 * state that *resource* was left in by the previous frame/draw (defaulting to an
		 * undefined state the first time it is seen). See m_LastState.
		 */
		FrameGraph&
		ImportBuffer(
			std::string_view           name,
			BufferHandle               handle,
			std::optional<AccessState> initial = {});

		/**
		 * Like ImportBuffer but ignores the current resource namespace, registering the
		 * resource under `name` verbatim. Use for pass-owned resources that are shared
		 * across namespaces (e.g. scene-independent scratch buffers) so every scope sees
		 * one tracked resource rather than a per-namespace copy. Passes in a namespace
		 * still reach it via ResolveName's fall back to the bare name.
		 */
		FrameGraph&
		ImportGlobalBuffer(
			std::string_view           name,
			BufferHandle               handle,
			std::optional<AccessState> initial = {});

		FrameGraph&
		ImportTexture(
			std::string_view           name,
			TextureHandle              handle,
			std::optional<AccessState> initial = {});

		FrameGraph&
		AddPass(PassDesc desc);

		/**
		 * Sets the prefix subsequent imports are keyed under and subsequent passes resolve against.
		 *
		 * Scopes nest by ':'-delimited segment: a pass recorded under "v0:c1:" resolves a name at
		 * "v0:c1:", then "v0:", then bare, taking the innermost that was imported. A scope that is
		 * to be nested inside must therefore end in ':'.
		 */
		void
		SetResourceNamespace(std::string resourceNamespace);

		void
		RegisterQueue(std::string name, CommandQueueRef queue, CommandListRef list);

		/**
		 * Installs the poisoner that fills the buffer args passes declare with
		 * AddPoisonedBufferArg. Without one -- which is every Release build -- those declarations
		 * are inert. The poisoner must outlive every Execute that follows.
		 */
		void
		SetBufferPoisoner(BufferPoisoner* poisoner) noexcept
		{
			m_Poisoner = poisoner;
		}

		void
		Compile(IResourceManager* resourceManager);

		void
		Execute();

		// Clears the per-frame state (passes, imports, queues) so the graph can be
		// rebuilt for the next frame, while preserving the tracked resource states.
		void
		Reset();

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
			PassBarriers           barriers;
		};

		struct ImportedRes
		{
			std::variant<BufferHandle, TextureHandle> handle;
			AccessState                               initial;
			AccessState                               current;
		};

		// Identity of the resource itself. Buffers and textures index separate pools, so the kind
		// is part of it; the generation keeps a reused slot from inheriting the dead one's state.
		struct ResourceKey
		{
			uint32_t     index      = core::slot_handle::invalid_index;
			uint32_t     generation = 0;
			ResourceKind kind       = ResourceKind::kBuffer;

			[[nodiscard]] bool
			operator==(const ResourceKey& other) const noexcept = default;
		};

		struct ResourceKeyHash
		{
			[[nodiscard]] size_t
			operator()(const ResourceKey& key) const noexcept
			{
				const uint64_t packed = (uint64_t(key.index) << 32) | key.generation;
				return std::hash<uint64_t>{}(packed) ^
				       (key.kind == ResourceKind::kTexture ? 0x9E3779B97F4A7C15ull : 0ull);
			}
		};

		[[nodiscard]] static ResourceKey
		KeyOf(const std::variant<BufferHandle, TextureHandle>& handle) noexcept;

		void
		DeriveBarriers(IResourceManager* resourceManager);

		// Fills every poison-declared buffer of `pass`, bracketed by the transitions from and back
		// to the state the pass declared for it.
		void
		PoisonPassBuffers(const PassNode& pass, ICommandList* cmd);

		FrameGraph&
		ImportBufferKey(std::string key, BufferHandle handle, std::optional<AccessState> initial);

		[[nodiscard]] AccessState
		ResolveInitialState(ResourceKey key, std::optional<AccessState> initial) const;

		void
		ClearFrame();

		[[nodiscard]] std::string
		ResolveName(std::string_view ns, std::string_view name) const;

		[[nodiscard]] bool
		WritesImported(const PassNode& pass) const;

		// m_Passes.size() when there is no such pass.
		[[nodiscard]] size_t
		FindPassIndex(std::string_view name) const;

		struct QueueBinding
		{
			CommandQueueRef queue;
			CommandListRef  list;
		};

		std::vector<PassNode>                      m_Passes;
		core::str::unordered_str_map<ImportedRes>  m_Imported;
		core::str::unordered_str_map<QueueBinding> m_Queues;
		// The edges, the culling and the order, over passes reduced to a resolved name and a
		// write flag. It is rebuilt from m_Passes on every Compile, so its index is m_Passes'.
		PassScheduler m_Scheduler;
		std::string   m_CurrentNamespace;
		bool          m_Compiled = false;
		// Keyed on the resource, not on the name it was imported under: one FrameGraph serves
		// every render target and the attachment names are shared constants, so a name says
		// nothing about which resource was last left in that state.
		std::unordered_map<ResourceKey, AccessState, ResourceKeyHash> m_LastState;
		BufferPoisoner*                                               m_Poisoner = nullptr;
	};
}
