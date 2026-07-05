#pragma once
#include "constants/constants.h"
#include "resource/Buffer.h"
#include "resource/Dsv.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include <core/containers/static_vector.h>
#include <core/str/str.h>

namespace bgl
{
	class IResourceManager;
	class ICommandList;
	class ICommandQueue;
	class FrameGraph;

	struct BufferArg
	{
		std::string   name;
		BarrierSync   sync;
		BarrierAccess access;
	};

	struct TextureArg
	{
		std::string   name;
		BarrierSync   sync;
		BarrierAccess access;
		BarrierLayout layout;
	};

	class PassContext
	{
	private:
		struct BufferEntry
		{
			BufferHandle handle;
			BufferArg    arg;
		};

		struct TextureEntry
		{
			TextureHandle handle;
			TextureArg    arg;
		};

	public:
		/**
		 * Resolves a buffer declared by this pass to its physical handle. Throws
		 * std::runtime_error if the name was not declared by the pass or resolves
		 * to no imported resource (e.g. a transient).
		 */
		BufferHandle
		GetBuffer(std::string_view sv) const;

		/**
		 * Resolves a texture declared by this pass. See GetBuffer for the throwing
		 * contract.
		 */
		TextureHandle
		GetTexture(std::string_view sv) const;

		// The command list / queue of the queue this pass was assigned to (its
		// PassDesc::queue), supplied by the graph at execute time.
		[[nodiscard]] ICommandList*
		GetCommandList() const noexcept
		{
			return m_CommandList;
		}

		[[nodiscard]] ICommandQueue*
		GetCommandQueue() const noexcept
		{
			return m_CommandQueue;
		}

	private:
		core::str::unordered_str_map<BufferEntry>  m_Buffers;
		core::str::unordered_str_map<TextureEntry> m_Textures;
		core::SharedRef<IResourceManager>          m_ResourceManager;
		ICommandList*                              m_CommandList  = nullptr;
		ICommandQueue*                             m_CommandQueue = nullptr;

		friend class FrameGraph;
	};

	struct PassDesc
	{
		std::string name = "Unnamed Pass";

		// Render targets, transitioned to render-target state by the graph. The
		// graph resolves each view to its texture (via the ResourceManager) to
		// barrier it and to reject a texture reached both here and as an import.
		// Can be empty.
		core::static_vector<RtvHandle, c_MaxRenderTargets> colorAttachments;

		// Depth target, transitioned to depth-write. Empty when null.
		DsvHandle depthAttachment;

		std::vector<BufferArg>  buffers;
		std::vector<TextureArg> textures;

		// Pins the pass so it survives culling even if its outputs are unused.
		bool sideEffect = false;

		// Name of the queue this pass records on (registered via RegisterQueue).
		std::string queue = "main";

		std::function<void(const PassContext&)> exec = nullptr;

		PassDesc&
		SetQueue(std::string queueName)
		{
			queue = std::move(queueName);
			return *this;
		}

		PassDesc&
		AddColorAttachment(RtvHandle view)
		{
			colorAttachments.push_back(view);
			return *this;
		}

		PassDesc&
		SetDepthAttachment(DsvHandle view) noexcept
		{
			depthAttachment = view;
			return *this;
		}

		PassDesc&
		AddBufferArg(BufferArg buffer)
		{
			buffers.push_back(std::move(buffer));
			return *this;
		}

		PassDesc&
		AddBufferArg(std::string_view name_, BarrierSync sync_, BarrierAccess access_)
		{
			buffers.push_back(BufferArg(std::string(name_), sync_, access_));
			return *this;
		}

		PassDesc&
		AddTextureArg(TextureArg tex)
		{
			textures.push_back(std::move(tex));
			return *this;
		}

		PassDesc&
		AddTextureArg(
			std::string_view name_,
			BarrierSync      sync_,
			BarrierAccess    access_,
			BarrierLayout    layout_)
		{
			textures.push_back(TextureArg(std::string(name_), sync_, access_, layout_));
			return *this;
		}

		PassDesc&
		SetName(std::string passName) noexcept
		{
			gassert(!passName.empty(), "PassDesc name cannot be empty");
			gassert(
				passName != "$",
				"PassDesc name cannot be '$', which is reserved for the root pass");

			name = std::move(passName);
			return *this;
		}

		PassDesc&
		SetSideEffect(bool value = true) noexcept
		{
			sideEffect = value;
			return *this;
		}

		PassDesc&
		SetExec(std::function<void(const PassContext&)> execFunc) noexcept
		{
			exec = std::move(execFunc);
			return *this;
		}

		template <typename Func>
		PassDesc&
		SetExec(Func&& execFunc) noexcept
		{
			exec = std::function(execFunc);
			return *this;
		}
	};
}
