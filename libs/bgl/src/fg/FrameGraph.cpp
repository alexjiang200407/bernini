#include "fg/FrameGraph.h"
#include "resource/ResourceManager.h"

#include <stack>

namespace bgl
{
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

	AccessState
	FrameGraph::ResolveInitialState(const std::string& key, std::optional<AccessState> initial)
		const
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
		std::string                name,
		BufferHandle               handle,
		std::optional<AccessState> initial)
	{
		return ImportBufferKey(m_CurrentNamespace + name, handle, initial);
	}

	FrameGraph&
	FrameGraph::ImportGlobalBuffer(
		std::string                name,
		BufferHandle               handle,
		std::optional<AccessState> initial)
	{
		return ImportBufferKey(std::move(name), handle, initial);
	}

	FrameGraph&
	FrameGraph::ImportBufferKey(
		std::string                key,
		BufferHandle               handle,
		std::optional<AccessState> initial)
	{
		ImportedRes res;
		res.handle  = handle;
		res.initial = ResolveInitialState(key, initial);
		res.current = res.initial;
		m_Imported.insert_or_assign(std::move(key), res);
		return *this;
	}

	FrameGraph&
	FrameGraph::ImportTexture(
		std::string                name,
		TextureHandle              handle,
		std::optional<AccessState> initial)
	{
		const std::string key = m_CurrentNamespace + name;

		ImportedRes res;
		res.handle  = handle;
		res.initial = ResolveInitialState(key, initial);
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
		if (m_Imported.contains(name))
		{
			return std::string(name);
		}
		return scoped;
	}

	FrameGraph&
	FrameGraph::AddPass(PassDesc desc)
	{
		if (FindPass(desc.name) != nullptr)
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

		std::unordered_map<std::string, int32_t> producer;
		for (size_t p = 0; p < m_Passes.size(); ++p)
		{
			PassNode& pass = m_Passes[p];
			pass.deps.clear();
			pass.kept = false;

			for (const ResAccess& a : pass.accesses)
			{
				const std::string resolved = ResolveName(pass.ns, a.name);
				if (const auto it = producer.find(resolved); it != producer.end())
				{
					pass.deps.push_back(it->second);
				}
				if (a.isWrite)
				{
					producer[resolved] = static_cast<int32_t>(p);
				}
			}
		}

		std::stack<size_t> live;
		for (size_t p = 0; p < m_Passes.size(); ++p)
		{
			const PassNode& pass = m_Passes[p];
			const bool      root = pass.desc.sideEffect || !pass.desc.colorAttachments.empty() ||
			                       !pass.desc.depthAttachment.IsNull() || WritesImported(pass);
			if (root)
			{
				m_Passes[p].kept = true;
				live.push(p);
			}
		}

		while (!live.empty())
		{
			const size_t p = live.top();
			live.pop();
			for (const int32_t d : m_Passes[p].deps)
			{
				if (!m_Passes[static_cast<size_t>(d)].kept)
				{
					m_Passes[static_cast<size_t>(d)].kept = true;
					live.push(static_cast<size_t>(d));
				}
			}
		}

		m_Order.clear();
		for (size_t p = 0; p < m_Passes.size(); ++p)
		{
			if (m_Passes[p].kept)
			{
				m_Order.push_back(p);
			}
		}

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

		for (const size_t p : m_Order)
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
				if (StateEqual(res.current, target))
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
	FrameGraph::RegisterQueue(std::string name, CommandQueueHandle queue, CommandListHandle list)
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

		for (const size_t p : m_Order)
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
		// including when the same scene is drawn more than once.
		for (const auto& [name, res] : m_Imported)
		{
			m_LastState[name] = res.current;
		}

		// Executing consumes the frame: drop the passes (releasing their exec
		// lambdas and anything those captured, e.g. scene references), the imports,
		// and the queue bindings, and require a recompile before the next Execute.
		// The tracked resource states (m_LastState) are kept for the next frame.
		ClearFrame();
	}

	void
	FrameGraph::ClearFrame()
	{
		m_Passes.clear();
		m_Order.clear();
		m_Imported.clear();
		m_Queues.clear();
		m_Compiled = false;
	}

	void
	FrameGraph::Reset()
	{
		ClearFrame();
	}

	const FrameGraph::PassNode*
	FrameGraph::FindPass(std::string_view name) const
	{
		for (const PassNode& pass : m_Passes)
		{
			if (pass.desc.name == name)
			{
				return &pass;
			}
		}
		return nullptr;
	}

	std::vector<std::string>
	FrameGraph::ExecutionOrder() const
	{
		std::vector<std::string> names;
		names.reserve(m_Order.size());
		for (const size_t p : m_Order)
		{
			names.push_back(m_Passes[p].desc.name);
		}
		return names;
	}

	bool
	FrameGraph::WasCulled(std::string_view passName) const
	{
		const PassNode* pass = FindPass(passName);
		return pass && !pass->kept;
	}

	const PassBarriers&
	FrameGraph::BarriersFor(std::string_view passName) const
	{
		static const PassBarriers kEmpty;
		const PassNode*           pass = FindPass(passName);
		return pass ? pass->barriers : kEmpty;
	}
}
