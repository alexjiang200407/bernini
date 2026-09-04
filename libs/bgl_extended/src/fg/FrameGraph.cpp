#include "fg/FrameGraph.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "fg/PassDesc.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include <bgl_common/gassert.h>
#include <core/containers/slot_handle.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace bgl
{
	namespace
	{
		// The scope one level out: "v0:c1:" -> "v0:" -> "". A scope has to end in ':' for another
		// to nest inside it.
		std::string_view
		ParentScope(std::string_view scope) noexcept
		{
			if (scope.empty())
			{
				return {};
			}

			scope.remove_suffix(1);

			const size_t cut = scope.find_last_of(':');
			return cut == std::string_view::npos ? std::string_view() : scope.substr(0, cut + 1);
		}
	}

	BufferHandle
	PassContext::GetBuffer(std::string_view sv) const
	{
		const auto it = m_Buffers.find(sv);
		if (it == m_Buffers.end())
		{
			core::throw_runtime_error(
				"PassContext::GetBuffer: buffer '{}' was not declared by this pass",
				sv);
		}
		if (it->second.handle.IsNull())
		{
			core::throw_runtime_error(
				"PassContext::GetBuffer: buffer '{}' has no imported resource (transient?)",
				sv);
		}
		return it->second.handle;
	}

	TextureHandle
	PassContext::GetTexture(std::string_view sv) const
	{
		const auto it = m_Textures.find(sv);
		if (it == m_Textures.end())
		{
			core::throw_runtime_error(
				"PassContext::GetTexture: texture '{}' was not declared by this pass",
				sv);
		}
		if (it->second.handle.IsNull())
		{
			core::throw_runtime_error(
				"PassContext::GetTexture: texture '{}' has no imported resource (transient?)",
				sv);
		}
		return it->second.handle;
	}

	namespace
	{
		bool
		IsWrite(BarrierAccess access) noexcept
		{
			return access.any(
				BarrierAccessFlag::kUnorderedAccess,
				BarrierAccessFlag::kRenderTarget,
				BarrierAccessFlag::kDepthWrite,
				BarrierAccessFlag::kCopyDest,
				BarrierAccessFlag::kAccelStructWrite);
		}

		bool
		StateEqual(const AccessState& a, const AccessState& b) noexcept
		{
			return a.sync.underlying() == b.sync.underlying() &&
			       a.access.underlying() == b.access.underlying() && a.layout == b.layout;
		}

		bool
		NeedsBarrier(const AccessState& before, const AccessState& after) noexcept
		{
			if (!StateEqual(before, after))
			{
				return true;
			}

			return after.access.any(BarrierAccessFlag::kUnorderedAccess);
		}

		AccessState
		Merge(const AccessState& a, const AccessState& b) noexcept
		{
			AccessState out;
			out.sync   = a.sync | b.sync;
			out.access = a.access | b.access;
			out.layout = (b.layout != BarrierLayout::kUndefined) ? b.layout : a.layout;
			return out;
		}

		TextureBarrierDesc
		MakeTextureBarrierDesc(const AccessState& before, const AccessState& after) noexcept
		{
			TextureBarrierDesc desc;
			desc.syncBefore   = before.sync;
			desc.accessBefore = before.access;
			desc.syncAfter    = after.sync;
			desc.accessAfter  = after.access;
			desc.layoutBefore = before.layout;
			desc.layoutAfter  = after.layout;
			return desc;
		}
	}

	FrameGraph::ResourceKey
	FrameGraph::KeyOf(const std::variant<BufferHandle, TextureHandle>& handle) noexcept
	{
		const core::slot_handle slot = std::visit([](const auto& h) { return h.slot; }, handle);

		return ResourceKey{ slot.index,
			                slot.generation,
			                std::holds_alternative<TextureHandle>(handle) ? ResourceKind::kTexture :
			                                                                ResourceKind::kBuffer };
	}

	AccessState
	FrameGraph::ResolveInitialState(const ResourceKey key, std::optional<AccessState> initial) const
	{
		if (initial.has_value())
		{
			return *initial;
		}
		if (const auto it = m_LastState.find(key); it != m_LastState.end())
		{
			return it->second;
		}
		return AccessState{};
	}

	FrameGraph&
	FrameGraph::ImportBuffer(
		std::string_view           name,
		BufferHandle               handle,
		std::optional<AccessState> initial)
	{
		return ImportBufferKey(std::string(m_CurrentNamespace).append(name), handle, initial);
	}

	FrameGraph&
	FrameGraph::ImportGlobalBuffer(
		std::string_view           name,
		BufferHandle               handle,
		std::optional<AccessState> initial)
	{
		return ImportBufferKey(std::string(name), handle, initial);
	}

	FrameGraph&
	FrameGraph::ImportBufferKey(
		std::string                key,
		BufferHandle               handle,
		std::optional<AccessState> initial)
	{
		ImportedRes res;
		res.handle  = handle;
		res.initial = ResolveInitialState(KeyOf(res.handle), initial);
		res.current = res.initial;
		m_Imported.insert_or_assign(std::move(key), res);
		return *this;
	}

	FrameGraph&
	FrameGraph::ImportTexture(
		std::string_view           name,
		TextureHandle              handle,
		std::optional<AccessState> initial)
	{
		const std::string key = std::string(m_CurrentNamespace).append(name);

		ImportedRes res;
		res.handle  = handle;
		res.initial = ResolveInitialState(KeyOf(res.handle), initial);
		res.current = res.initial;
		m_Imported.insert_or_assign(key, res);
		return *this;
	}

	void
	FrameGraph::SetResourceNamespace(std::string resourceNamespace)
	{
		m_CurrentNamespace = std::move(resourceNamespace);
	}

	std::string
	FrameGraph::ResolveName(std::string_view ns, std::string_view name) const
	{
		std::string scoped = std::string(ns) + std::string(name);
		if (m_Imported.contains(scoped))
		{
			return scoped;
		}

		for (std::string_view outer = ParentScope(ns); !outer.empty(); outer = ParentScope(outer))
		{
			scoped.assign(outer).append(name);
			if (m_Imported.contains(scoped))
			{
				return scoped;
			}
		}

		if (m_Imported.contains(name))
		{
			return std::string(name);
		}

		scoped.assign(ns).append(name);
		return scoped;
	}

	FrameGraph&
	FrameGraph::AddPass(PassDesc desc)
	{
		if (FindPassIndex(desc.name) != m_Passes.size())
		{
			core::throw_runtime_error(
				"FrameGraph::AddPass: a pass named '{}' already exists",
				desc.name);
		}

		PassNode node;
		node.desc = std::move(desc);
		node.ns   = m_CurrentNamespace;

		for (const BufferArg& b : node.desc.buffers)
		{
			node.accesses.push_back(
				{ b.name,
			      ResourceKind::kBuffer,
			      AccessState{ b.sync, b.access, BarrierLayout::kUndefined },
			      IsWrite(b.access) });
		}
		for (const TextureArg& t : node.desc.textures)
		{
			node.accesses.push_back(
				{ t.name,
			      ResourceKind::kTexture,
			      AccessState{ t.sync, t.access, t.layout },
			      IsWrite(t.access) });
		}

		m_Passes.push_back(std::move(node));
		return *this;
	}

	bool
	FrameGraph::WritesImported(const PassNode& pass) const
	{
		for (const ResAccess& a : pass.accesses)
		{
			if (a.isWrite && m_Imported.contains(ResolveName(pass.ns, a.name)))
			{
				return true;
			}
		}
		return false;
	}

	void
	FrameGraph::Compile(IResourceManager* resourceManager)
	{
		gassert(resourceManager != nullptr, "ResourceManager cannot be null");

		for (const PassNode& pass : m_Passes)
		{
			for (const ResAccess& a : pass.accesses)
			{
				const auto it = m_Imported.find(ResolveName(pass.ns, a.name));
				if (it == m_Imported.end())
				{
					continue;  // transient: no declared kind to conflict with
				}
				const bool importedIsBuffer =
					std::holds_alternative<BufferHandle>(it->second.handle);

				const bool accessIsBuffer = a.kind == ResourceKind::kBuffer;
				if (importedIsBuffer != accessIsBuffer)
				{
					core::throw_runtime_error(
						"FrameGraph::Compile: resource '{}' was imported as a {} but pass '{}' "
						"accesses it as a {}",
						a.name,
						importedIsBuffer ? "buffer" : "texture",
						pass.desc.name,
						accessIsBuffer ? "buffer" : "texture");
				}
			}
		}

		for (const PassNode& pass : m_Passes)
		{
			const bool hasAttachments =
				!pass.desc.colorAttachments.empty() || !pass.desc.depthAttachment.IsNull();
			if (hasAttachments && resourceManager == nullptr)
			{
				core::throw_runtime_error(
					"FrameGraph::Compile: pass '{}' has attachments but no ResourceManager was "
					"provided to resolve them",
					pass.desc.name);
			}

			const auto rejectIfImported = [&](TextureHandle tex) {
				for (const auto& [name, res] : m_Imported)
				{
					if (!std::holds_alternative<TextureHandle>(res.handle))
					{
						continue;
					}
					const TextureHandle imported = std::get<TextureHandle>(res.handle);
					if (imported == tex)
					{
						core::throw_runtime_error(
							"FrameGraph::Compile: the texture attached to pass '{}' is also "
							"imported as '{}'; reach a texture either as an attachment or as "
							"an "
							"imported resource, not both",
							pass.desc.name,
							name);
					}
				}
			};

			for (size_t i = 0; i < pass.desc.colorAttachments.size(); ++i)
			{
				rejectIfImported(
					resourceManager->GetRtvTexture(pass.desc.colorAttachments.data()[i]));
			}
			if (!pass.desc.depthAttachment.IsNull())
			{
				rejectIfImported(resourceManager->GetDsvTexture(pass.desc.depthAttachment));
			}
		}

		m_Scheduler.Clear();
		for (size_t p = 0; p < m_Passes.size(); ++p)
		{
			const PassNode& pass = m_Passes[p];

			PassScheduler::Pass scheduled;
			// Culling makes no distinction between these, and none of them can be named a tier
			// down, so every reason to keep the pass regardless of its outputs arrives as one flag.
			scheduled.pinned = pass.desc.sideEffect || !pass.desc.colorAttachments.empty() ||
			                   !pass.desc.depthAttachment.IsNull() || WritesImported(pass);

			// Resolved here rather than at AddPass: an import may be registered after the pass
			// that names it, so the walk is only correct once the frame is fully declared.
			scheduled.accesses.reserve(pass.accesses.size());
			for (const ResAccess& a : pass.accesses)
			{
				scheduled.accesses.emplace_back(ResolveName(pass.ns, a.name), a.isWrite);
			}

			const size_t scheduledIndex = m_Scheduler.AddPass(std::move(scheduled));
			gassert(
				scheduledIndex == p,
				"DeriveBarriers and Execute index m_Passes with the scheduler's index, so the two "
				"must stay in step");
		}

		m_Scheduler.Compile();

		DeriveBarriers(resourceManager);
		m_Compiled = true;
	}

	void
	FrameGraph::DeriveBarriers(IResourceManager* resourceManager)
	{
		for (auto& [name, res] : m_Imported)
		{
			res.current = res.initial;
		}

		std::unordered_map<uint32_t, AccessState> attachmentState;

		const AccessState rtTarget{ BarrierSyncFlag::kRenderTarget,
			                        BarrierAccessFlag::kRenderTarget,
			                        BarrierLayout::kRenderTarget };
		const AccessState dsTarget{ BarrierSyncFlag::kDepthStencil,
			                        BarrierAccessFlag::kDepthWrite,
			                        BarrierLayout::kDepthWrite };

		for (const size_t p : m_Scheduler.Order())
		{
			PassNode& pass = m_Passes[p];
			pass.barriers  = PassBarriers{};

			std::vector<std::pair<std::string, AccessState>> targets;
			for (const ResAccess& a : pass.accesses)
			{
				const std::string resolved = ResolveName(pass.ns, a.name);
				if (!m_Imported.contains(resolved))
				{
					continue;
				}
				bool merged = false;
				for (auto& [name, st] : targets)
				{
					if (name == resolved)
					{
						st     = Merge(st, a.state);
						merged = true;
						break;
					}
				}
				if (!merged)
				{
					targets.emplace_back(resolved, a.state);
				}
			}

			for (const auto& [name, target] : targets)
			{
				ImportedRes& res = m_Imported[name];
				if (!NeedsBarrier(res.current, target))
				{
					continue;
				}

				if (std::holds_alternative<BufferHandle>(res.handle))
				{
					BufferBarrierDesc desc;
					desc.syncBefore   = res.current.sync;
					desc.accessBefore = res.current.access;
					desc.syncAfter    = target.sync;
					desc.accessAfter  = target.access;
					pass.barriers.bufferHandles.push_back(std::get<BufferHandle>(res.handle));
					pass.barriers.bufferDescs.push_back(desc);
				}
				else
				{
					pass.barriers.textureHandles.push_back(std::get<TextureHandle>(res.handle));
					pass.barriers.textureDescs.push_back(
						MakeTextureBarrierDesc(res.current, target));
				}

				res.current = target;
			}

			const auto& colorAttachments = pass.desc.colorAttachments;
			for (size_t i = 0; i < colorAttachments.size(); ++i)
			{
				const TextureHandle tex =
					resourceManager->GetRtvTexture(colorAttachments.data()[i]);
				AccessState& cur = attachmentState[tex.slot.index];
				if (!StateEqual(cur, rtTarget))
				{
					pass.barriers.textureHandles.push_back(tex);
					pass.barriers.textureDescs.push_back(MakeTextureBarrierDesc(cur, rtTarget));
					cur = rtTarget;
				}
			}
			if (!pass.desc.depthAttachment.IsNull())
			{
				const TextureHandle tex = resourceManager->GetDsvTexture(pass.desc.depthAttachment);
				AccessState&        cur = attachmentState[tex.slot.index];
				if (!StateEqual(cur, dsTarget))
				{
					pass.barriers.textureHandles.push_back(tex);
					pass.barriers.textureDescs.push_back(MakeTextureBarrierDesc(cur, dsTarget));
					cur = dsTarget;
				}
			}
		}
	}

	void
	FrameGraph::RegisterQueue(std::string name, CommandQueueRef queue, CommandListRef list)
	{
		m_Queues.insert_or_assign(
			std::move(name),
			QueueBinding{ std::move(queue), std::move(list) });
	}

	void
	FrameGraph::Execute()
	{
		if (!m_Compiled)
		{
			throw std::runtime_error("FrameGraph::Execute called before Compile");
		}

		for (const size_t p : m_Scheduler.Order())
		{
			PassNode& pass = m_Passes[p];

			const auto qit = m_Queues.find(pass.desc.queue);
			if (qit == m_Queues.end())
			{
				core::throw_runtime_error(
					"FrameGraph::Execute: pass '{}' records on unregistered queue '{}'",
					pass.desc.name,
					pass.desc.queue);
			}
			ICommandList*  cmd   = qit->second.list.Get();
			ICommandQueue* queue = qit->second.queue.Get();

			cmd->BeginEvent(pass.desc.name);

			const PassBarriers& b = pass.barriers;
			if (!b.bufferHandles.empty())
			{
				cmd->Barrier(b.bufferHandles, b.bufferDescs);
			}
			if (!b.textureHandles.empty())
			{
				cmd->Barrier(b.textureHandles, b.textureDescs);
			}

			if (m_Poisoner != nullptr)
			{
				PoisonPassBuffers(pass, cmd);
			}

			if (pass.desc.exec)
			{
				PassContext ctx;
				ctx.m_CommandList  = cmd;
				ctx.m_CommandQueue = queue;
				for (const BufferArg& barg : pass.desc.buffers)
				{
					BufferHandle handle{};
					if (const auto it = m_Imported.find(ResolveName(pass.ns, barg.name));
					    it != m_Imported.end() &&
					    std::holds_alternative<BufferHandle>(it->second.handle))
					{
						handle = std::get<BufferHandle>(it->second.handle);
					}
					ctx.m_Buffers.insert_or_assign(
						barg.name,
						PassContext::BufferEntry{ handle, barg });
				}
				for (const TextureArg& targ : pass.desc.textures)
				{
					TextureHandle handle{};
					if (const auto it = m_Imported.find(ResolveName(pass.ns, targ.name));
					    it != m_Imported.end() &&
					    std::holds_alternative<TextureHandle>(it->second.handle))
					{
						handle = std::get<TextureHandle>(it->second.handle);
					}
					ctx.m_Textures.insert_or_assign(
						targ.name,
						PassContext::TextureEntry{ handle, targ });
				}
				pass.desc.exec(ctx);
			}

			cmd->EndEvent();
		}

		// Remember the state each imported resource was left in so the next frame's
		// import resumes from it (DeriveBarriers leaves res.current at the final
		// state). This is what lets callers omit the initial state on re-import,
		// including when the same scene is drawn more than once, or when another
		// render target imports its own attachments under the same names.
		for (const auto& [name, res] : m_Imported)
		{
			m_LastState[KeyOf(res.handle)] = res.current;
		}

		// Executing consumes the frame: drop the passes (releasing their exec
		// lambdas and anything those captured, e.g. scene references), the imports,
		// and the queue bindings, and require a recompile before the next Execute.
		// The tracked resource states (m_LastState) are kept for the next frame.
		ClearFrame();
	}

	void
	FrameGraph::PoisonPassBuffers(const PassNode& pass, ICommandList* cmd)
	{
		for (const BufferArg& arg : pass.desc.buffers)
		{
			if (!arg.poison)
			{
				continue;
			}

			const auto it = m_Imported.find(ResolveName(pass.ns, arg.name));
			if (it == m_Imported.end() || !std::holds_alternative<BufferHandle>(it->second.handle))
			{
				continue;
			}

			// A transient reaches here as a null handle: DeriveBarriers inserts a default entry for
			// every name it tracks, imported or not.
			const BufferHandle handle = std::get<BufferHandle>(it->second.handle);
			if (handle.IsNull())
			{
				continue;
			}

			// The fill is a copy, so the buffer round-trips out of the state DeriveBarriers put it
			// in and back again -- leaving the tracked state the pass declared, and separating the
			// fill from the pass's own writes with a barrier the graph would not otherwise emit.
			BufferBarrierDesc toCopy;
			toCopy.syncBefore   = arg.sync;
			toCopy.accessBefore = arg.access;
			toCopy.syncAfter    = BarrierSyncFlag::kCopy;
			toCopy.accessAfter  = BarrierAccessFlag::kCopyDest;

			BufferBarrierDesc toPass;
			toPass.syncBefore   = BarrierSyncFlag::kCopy;
			toPass.accessBefore = BarrierAccessFlag::kCopyDest;
			toPass.syncAfter    = arg.sync;
			toPass.accessAfter  = arg.access;

			cmd->Barrier(handle, toCopy);
			m_Poisoner->Poison(cmd, handle);
			cmd->Barrier(handle, toPass);
		}
	}

	void
	FrameGraph::ClearFrame()
	{
		m_Passes.clear();
		m_Scheduler.Clear();
		m_Imported.clear();
		m_Queues.clear();
		m_Compiled = false;
	}

	void
	FrameGraph::Reset()
	{
		ClearFrame();
	}

	size_t
	FrameGraph::FindPassIndex(const std::string_view name) const
	{
		for (size_t p = 0; p < m_Passes.size(); ++p)
		{
			if (m_Passes[p].desc.name == name)
			{
				return p;
			}
		}
		return m_Passes.size();
	}

	std::vector<std::string>
	FrameGraph::ExecutionOrder() const
	{
		const std::vector<size_t>& order = m_Scheduler.Order();

		std::vector<std::string> names;
		names.reserve(order.size());
		for (const size_t p : order)
		{
			names.push_back(m_Passes[p].desc.name);
		}
		return names;
	}

	bool
	FrameGraph::WasCulled(std::string_view passName) const
	{
		const size_t p = FindPassIndex(passName);
		return p != m_Passes.size() && m_Scheduler.WasCulled(p);
	}

	const PassBarriers&
	FrameGraph::BarriersFor(std::string_view passName) const
	{
		static const PassBarriers c_Empty;
		const size_t              p = FindPassIndex(passName);
		return p != m_Passes.size() ? m_Passes[p].barriers : c_Empty;
	}
}
