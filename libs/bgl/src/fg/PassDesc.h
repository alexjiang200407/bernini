#pragma once
#include <bgpu/constants/constants.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <core/containers/static_vector.h>
#include <core/err/util.h>
#include <core/str/str.h>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	class IResourceManager;
	class ICommandList;
	class ICommandQueue;
}

namespace bgl
{
	class FrameGraph;

	struct BufferArg
	{
		std::string         name;
		bgpu::BarrierSync   sync;
		bgpu::BarrierAccess access;

		// Fill the buffer with the poison word before the pass runs. See
		// PassDesc::AddPoisonedBufferArg; ignored unless the graph has a poisoner installed.
		bool poison = false;
	};

	struct TextureArg
	{
		std::string         name;
		bgpu::BarrierSync   sync;
		bgpu::BarrierAccess access;
		bgpu::BarrierLayout layout;
	};

	class PassContext
	{
	private:
		struct BufferEntry
		{
			bgpu::BufferHandle handle;
			BufferArg          arg;
		};

		struct TextureEntry
		{
			bgpu::TextureHandle handle;
			TextureArg          arg;
		};

	public:
		/**
		 * Resolves a buffer declared by this pass to its physical handle. Throws
		 * std::runtime_error if the name was not declared by the pass or resolves
		 * to no imported resource (e.g. a transient).
		 */
		bgpu::BufferHandle
		GetBuffer(std::string_view sv) const;

		/**
		 * Resolves a texture declared by this pass. See GetBuffer for the throwing
		 * contract.
		 */
		bgpu::TextureHandle
		GetTexture(std::string_view sv) const;

		// The command list / queue of the queue this pass was assigned to (its
		// PassDesc::queue), supplied by the graph at execute time.
		[[nodiscard]] bgpu::ICommandList*
		GetCommandList() const noexcept
		{
			return m_CommandList;
		}

		[[nodiscard]] bgpu::ICommandQueue*
		GetCommandQueue() const noexcept
		{
			return m_CommandQueue;
		}

	private:
		core::str::unordered_str_map<BufferEntry>  m_Buffers;
		core::str::unordered_str_map<TextureEntry> m_Textures;
		bgpu::ICommandList*                        m_CommandList  = nullptr;
		bgpu::ICommandQueue*                       m_CommandQueue = nullptr;

		friend class FrameGraph;
	};

	struct PassDesc
	{
		std::string name = "Unnamed Pass";

		// Render targets, transitioned to render-target state by the graph. The
		// graph resolves each view to its texture (via the ResourceManager) to
		// barrier it and to reject a texture reached both here and as an import.
		// Can be empty.
		core::static_vector<bgpu::RtvHandle, bgpu::c_MaxRenderTargets> colorAttachments;

		// Depth target, transitioned to depth-write. Empty when null.
		bgpu::DsvHandle depthAttachment;

		std::vector<BufferArg>  buffers;
		std::vector<TextureArg> textures;

		// Pins the pass so it survives culling even if its outputs are unused.
		bool sideEffect = false;

		// Name of the queue this pass records on (registered via RegisterQueue).
		std::string queue = "main";

		std::function<void(const PassContext&)> exec = nullptr;

		template <typename Self>
		Self&&
		SetQueue(this Self&& self, std::string queueName)
		{
			self.queue = std::move(queueName);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddColorAttachment(this Self&& self, bgpu::RtvHandle view)
		{
			self.colorAttachments.push_back(view);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDepthAttachment(this Self&& self, bgpu::DsvHandle view) noexcept
		{
			self.depthAttachment = view;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddBufferArg(this Self&& self, BufferArg buffer)
		{
			self.buffers.push_back(std::move(buffer));
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddBufferArg(
			this Self&&         self,
			std::string_view    bufferName,
			bgpu::BarrierSync   bufferSync,
			bgpu::BarrierAccess bufferAccess)
		{
			self.buffers.push_back(BufferArg(std::string(bufferName), bufferSync, bufferAccess));
			return std::forward<Self>(self);
		}

		/**
		 * Declares a UAV output this pass rewrites from nothing, rather than one it accumulates
		 * into. In a debug build the graph fills it with the poison word before the pass records,
		 * so an element the pass leaves unwritten reads back as garbage instead of as whatever the
		 * last frame put there -- which is usually plausible enough to look correct.
		 *
		 * Only valid for an unordered-access arg, which is why the access is not a parameter.
		 */
		template <typename Self>
		Self&&
		AddPoisonedBufferArg(
			this Self&&       self,
			std::string_view  bufferName,
			bgpu::BarrierSync bufferSync)
		{
			self.buffers.push_back(BufferArg(
				std::string(bufferName),
				bufferSync,
				bgpu::BarrierAccessFlag::kUnorderedAccess,
				true));
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddTextureArg(this Self&& self, TextureArg tex)
		{
			self.textures.push_back(std::move(tex));
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddTextureArg(
			this Self&&         self,
			std::string_view    textureName,
			bgpu::BarrierSync   textureSync,
			bgpu::BarrierAccess textureAccess,
			bgpu::BarrierLayout textureLayout)
		{
			self.textures.push_back(
				TextureArg(std::string(textureName), textureSync, textureAccess, textureLayout));
			return std::forward<Self>(self);
		}

		// The accesses the passes make, each owning its barrier flags so a pass names only the
		// resource. `stages` is the shader stages that touch it; the geometry stages of a mesh-shader
		// pipeline sync as kVertexShader. AddBufferArg and AddTextureArg are for any other access.

		/** A texture this pass draws into as a colour target, by its graph name. */
		template <typename Self>
		Self&&
		AddRenderTarget(this Self&& self, std::string_view textureName)
		{
			return std::forward<Self>(self).AddTextureArg(
				textureName,
				bgpu::BarrierSyncFlag::kRenderTarget,
				bgpu::BarrierAccessFlag::kRenderTarget,
				bgpu::BarrierLayout::kRenderTarget);
		}

		/** A depth-stencil texture this pass depth-tests against and writes. */
		template <typename Self>
		Self&&
		AddDepthWrite(this Self&& self, std::string_view textureName)
		{
			return std::forward<Self>(self).AddTextureArg(
				textureName,
				bgpu::BarrierSyncFlag::kDepthStencil,
				bgpu::BarrierAccessFlag::kDepthWrite,
				bgpu::BarrierLayout::kDepthWrite);
		}

		/** A texture `stages` sample or load. */
		template <typename Self>
		Self&&
		AddTextureRead(this Self&& self, std::string_view textureName, bgpu::BarrierSync stages)
		{
			return std::forward<Self>(self).AddTextureArg(
				textureName,
				stages,
				bgpu::BarrierAccessFlag::kShaderResource,
				bgpu::BarrierLayout::kShaderResource);
		}

		/** A buffer `stages` only read. */
		template <typename Self>
		Self&&
		AddBufferRead(this Self&& self, std::string_view bufferName, bgpu::BarrierSync stages)
		{
			return std::forward<Self>(self).AddBufferArg(
				bufferName,
				stages,
				bgpu::BarrierAccessFlag::kShaderResource);
		}

		/** A buffer `stages` bind for unordered access -- read, written, or both. */
		template <typename Self>
		Self&&
		AddBufferReadWrite(this Self&& self, std::string_view bufferName, bgpu::BarrierSync stages)
		{
			return std::forward<Self>(self).AddBufferArg(
				bufferName,
				stages,
				bgpu::BarrierAccessFlag::kUnorderedAccess);
		}

		/** A buffer an indirect dispatch or draw reads its arguments or count from. */
		template <typename Self>
		Self&&
		AddIndirectArgs(this Self&& self, std::string_view bufferName)
		{
			return std::forward<Self>(self).AddBufferArg(
				bufferName,
				bgpu::BarrierSyncFlag::kIndirectArgument,
				bgpu::BarrierAccessFlag::kIndirectArgument);
		}

		/** A buffer this pass copies out of. */
		template <typename Self>
		Self&&
		AddCopySource(this Self&& self, std::string_view bufferName)
		{
			return std::forward<Self>(self).AddBufferArg(
				bufferName,
				bgpu::BarrierSyncFlag::kCopy,
				bgpu::BarrierAccessFlag::kCopySource);
		}

		/** A buffer this pass copies into. */
		template <typename Self>
		Self&&
		AddCopyDest(this Self&& self, std::string_view bufferName)
		{
			return std::forward<Self>(self).AddBufferArg(
				bufferName,
				bgpu::BarrierSyncFlag::kCopy,
				bgpu::BarrierAccessFlag::kCopyDest);
		}

		/** Formats the name, so a pass keyed on its draw and frustum need not spell out std::format. */
		template <typename Self, typename... Args>
			requires(sizeof...(Args) > 0)
		Self&&
		SetName(this Self&& self, std::format_string<Args...> fmt, Args&&... args)
		{
			return std::forward<Self>(self).SetName(std::format(fmt, std::forward<Args>(args)...));
		}

		template <typename Self>
		Self&&
		SetName(this Self&& self, std::string passName) noexcept
		{
			core::ensure(!passName.empty(), "PassDesc name cannot be empty");
			core::ensure(
				passName != "$",
				"PassDesc name cannot be '$', which is reserved for the root pass");

			self.name = std::move(passName);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSideEffect(this Self&& self, bool value = true) noexcept
		{
			self.sideEffect = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetExec(this Self&& self, std::function<void(const PassContext&)> execFunc) noexcept
		{
			self.exec = std::move(execFunc);
			return std::forward<Self>(self);
		}

		template <typename Func, typename Self>
		Self&&
		SetExec(this Self&& self, Func&& execFunc) noexcept
		{
			self.exec = std::function(execFunc);
			return std::forward<Self>(self);
		}
	};
}
