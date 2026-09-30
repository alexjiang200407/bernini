#pragma once
#include <bgpu/GpuContext.h>
#include <bgpu/api.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/types/NativeObject.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/file/file.h>
#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace bgpu
{
	class IResourceManager;
	class IShader;
	class IMeshletPipeline;
	class ICommandList;
	class IComputePipeline;
	class ICommandAllocator;
	class ICommandQueue;
	class ITimestampHeap;
	struct ShaderDesc;
	struct MeshletPipelineDesc;
	struct ComputePipelineDesc;
	struct CommandListDesc;
	struct ResourceManagerDesc;

	class IDevice : public core::Ref
	{
	public:
		IDevice()                        = default;
		IDevice(const IDevice&) noexcept = delete;
		IDevice(IDevice&&) noexcept      = delete;

		IDevice&
		operator=(const IDevice&) noexcept = delete;

		IDevice&
		operator=(IDevice&&) noexcept = delete;

		[[nodiscard]]
		virtual core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept = 0;

		[[nodiscard]] BGPU_API core::SharedRef<IShader>
		CreateShader(std::string slangModuleName, std::string entryPointName = "main")
			const noexcept;

		/**
		 * A module compiled from text under a name, shadowing a file of that name on the search
		 * path for every compile after this one, in every owner of the GPU context. A name maps
		 * to one text: the same text again changes nothing, a new one replaces the old and drops the
		 * sessions. The shader cache's keys follow it through the context's source salt.
		 *
		 * @pre no compile is in flight, and no slang:: object is held by any owner, when the text is
		 *      new or changed.
		 */
		virtual void
		AddSourceModule(const bgpu::SlangSourceModule& sourceModule) noexcept = 0;

		/**
		 * Drops every thread's Slang session -- a few hundred resident megabytes apiece once a
		 * cold-cache compile has stood one up. Call after each pipeline batch is built; the next
		 * compile recreates what it needs. The sessions are the GPU context's, so the drop reaches
		 * every owner of that context.
		 *
		 * @pre no compile is in flight, and no slang:: object is held, by any owner of the context.
		 */
		virtual void
		ReleaseSlangSession() noexcept = 0;

		/** The GPU context this device draws on and compiles through. */
		[[nodiscard]] virtual bgpu::GpuContext&
		GetGpuContext() const noexcept = 0;

		/**
		 * This object's native counterpart as `type`, or null when this backend has none of that type.
		 * Borrowed; see `NativeObject`.
		 */
		[[nodiscard]] virtual NativeObject
		GetNativeObject(NativeObjectType type) const noexcept
		{
			(void)type;
			return {};
		}

		[[nodiscard]]
		virtual core::SharedRef<IComputePipeline>
		CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept = 0;

		[[nodiscard]]
		virtual core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept = 0;

		[[nodiscard]] BGPU_API ComputeKernel
		CreateComputeKernel(const ComputePipelineDesc& desc) const noexcept;

		[[nodiscard]] BGPU_API MeshletKernel
		CreateMeshletKernel(const MeshletPipelineDesc& desc) const noexcept;

		virtual core::SharedRef<ICommandList>
		CreateCommandList(
			const CommandListDesc&             desc,
			core::SharedRef<ICommandAllocator> commandAllocator,
			core::SharedRef<IResourceManager>  resourceManager) const noexcept = 0;

		[[nodiscard]]
		BGPU_API core::SharedRef<ICommandQueue>
				 CreateGraphicsCommandQueue() const noexcept;

		[[nodiscard]]
		virtual core::SharedRef<ICommandAllocator>
		CreateCommandAllocator(QueueType type = QueueType::kGraphics) const noexcept = 0;

		[[nodiscard]]
		virtual core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const noexcept = 0;

		/**
		 * `capacity` timestamp slots for ICommandList's timed spans. Null when the device cannot
		 * sample a timestamp at a pass boundary, which the caller treats as timing being unavailable
		 * rather than as an error.
		 */
		[[nodiscard]]
		virtual core::SharedRef<ITimestampHeap>
		CreateTimestampHeap(uint32_t capacity) const noexcept = 0;

		[[nodiscard]]
		virtual core::SharedRef<IResourceManager>
		CreateResourceManager(const ResourceManagerDesc& desc) const noexcept = 0;

		[[nodiscard]]
		virtual Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept = 0;

		[[nodiscard]]
		virtual Uniforms
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept = 0;
	};

	using DeviceRef = core::SharedRef<IDevice>;

	/**
	 * A device on `context`'s GPU for one owner. Everything made through it -- queues, resource
	 * managers, pipelines -- is that owner's, so two owners on one context stay isolated; the
	 * context is shared, the device is not.
	 *
	 * @pre the build has a backend (RENDERER_BACKEND is DX12 or METAL).
	 */
	[[nodiscard]] BGPU_API DeviceRef
	CreateDevice(bgpu::GpuContextRef context);
}
