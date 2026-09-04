#pragma once
#include "constants/constants.h"
#include "resource/Buffer.h"
#include "resource/Dsv.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include <bgl_common/gassert.h>
#include <core/containers/static_vector.h>
#include <core/str/str.h>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

		// Fill the buffer with the poison word before the pass runs. See
		// PassDesc::AddPoisonedBufferArg; ignored unless the graph has a poisoner installed.
		bool poison = false;
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
		AddBufferArg(
			std::string_view bufferName,
			BarrierSync      bufferSync,
			BarrierAccess    bufferAccess)
		{
			buffers.push_back(BufferArg(std::string(bufferName), bufferSync, bufferAccess));
			return *this;
		}

		/**
		 * Declares a UAV output this pass rewrites from nothing, rather than one it accumulates
		 * into. In a debug build the graph fills it with the poison word before the pass records,
		 * so an element the pass leaves unwritten reads back as garbage instead of as whatever the
		 * last frame put there -- which is usually plausible enough to look correct.
		 *
		 * Only valid for an unordered-access arg, which is why the access is not a parameter.
		 */
		PassDesc&
		AddPoisonedBufferArg(std::string_view bufferName, BarrierSync bufferSync)
		{
			buffers.push_back(BufferArg(
				std::string(bufferName),
				bufferSync,
				BarrierAccessFlag::kUnorderedAccess,
				true));
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
			std::string_view textureName,
			BarrierSync      textureSync,
			BarrierAccess    textureAccess,
			BarrierLayout    textureLayout)
		{
			textures.push_back(
				TextureArg(std::string(textureName), textureSync, textureAccess, textureLayout));
			return *this;
		}

		/** Formats the name, so a pass keyed on its draw and frustum need not spell out std::format. */
		template <typename... Args>
		PassDesc&
		SetName(std::format_string<Args...> fmt, Args&&... args)
		{
			return SetName(std::format(fmt, std::forward<Args>(args)...));
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
