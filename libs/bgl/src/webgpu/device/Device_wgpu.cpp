#include "device/Device_wgpu.h"

#include "cmd/CommandAllocator_wgpu.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue_wgpu.h"
#include "pipeline/ComputePipeline_wgpu.h"
#include "resource/ResourceManager_wgpu.h"
#include "resource/Shader_wgpu.h"
#include "slang/SlangErrorChecker.h"

#include <bgl/IGraphics.h>
#include <uniforms/Uniforms.h>

namespace bgl
{
	namespace
	{
		void
		WaitFor(const wgpu::Instance& instance, wgpu::Future future)
		{
			const auto status = instance.WaitAny(future, UINT64_MAX);
			if (status != wgpu::WaitStatus::Success)
				throw GraphicsError(
					"wgpu: waiting on a future failed with status " +
					std::to_string(static_cast<int>(status)));
		}

		wgpu::Adapter
		RequestAdapter(const wgpu::Instance& instance, const WgpuDeviceDesc& desc)
		{
			auto opts            = wgpu::RequestAdapterOptions{};
			opts.powerPreference = desc.powerPreference;

			wgpu::Adapter adapter;
			std::string   message;

			WaitFor(
				instance,
				instance.RequestAdapter(
					&opts,
					wgpu::CallbackMode::WaitAnyOnly,
					[&](wgpu::RequestAdapterStatus status,
			            wgpu::Adapter              got,
			            wgpu::StringView           msg) {
						if (status == wgpu::RequestAdapterStatus::Success)
							adapter = std::move(got);
						else
							message = std::string(std::string_view(msg));
					}));

			if (adapter == nullptr)
				throw GraphicsError("wgpu: no adapter available: " + message);

			return adapter;
		}

		WgpuAdapterInfo
		ReadAdapterInfo(const wgpu::Adapter& adapter)
		{
			auto raw = wgpu::AdapterInfo{};
			if (!adapter.GetInfo(&raw))
				throw GraphicsError("wgpu: could not read adapter info");

			auto info         = WgpuAdapterInfo{};
			info.vendor       = std::string(std::string_view(raw.vendor));
			info.architecture = std::string(std::string_view(raw.architecture));
			info.device       = std::string(std::string_view(raw.device));
			info.description  = std::string(std::string_view(raw.description));
			info.backendType  = raw.backendType;
			info.adapterType  = raw.adapterType;

			return info;
		}

		void
		OnUncapturedError(const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message)
		{
			logger::error(
				"[wgpu] uncaptured error ({}): {}",
				static_cast<int>(type),
				std::string_view(message));
		}

		void
		OnDeviceLost(const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
		{
			// Destroying the device reports a loss through this same callback; that one is expected.
			if (reason == wgpu::DeviceLostReason::Destroyed)
				return;

			logger::error(
				"[wgpu] device lost ({}): {}",
				static_cast<int>(reason),
				std::string_view(message));
		}

		// Only src is staged for this backend (bgl_copy_shader_src); there is no shaders/tests copy
		// as on D3D12. Add both together if a webgpu test ever loads a module from tests/.
		const char* const c_ShaderSearchPaths[] = { "./shaders/src" };

		Slang::ComPtr<slang::ISession>
		CreateWgslSession(slang::IGlobalSession* globalSession)
		{
			auto targetDesc   = slang::TargetDesc{};
			targetDesc.format = SLANG_WGSL;

			// BGL_WGSL selects the plainly-bound (non-.Handle) buffer primitives, in lockstep with
			// the offline -DBGL_WGSL in cmake/compile_shader.cmake; BERNINI_GPU_DEBUG enables the
			// dbg_raise bodies, matching the offline compile.
			auto macros = std::vector<slang::PreprocessorMacroDesc>{ { "BGL_WGSL", "1" } };
#if defined(BERNINI_GPU_DEBUG)
			macros.push_back({ "BERNINI_GPU_DEBUG", "1" });
#endif

			auto sessionDesc            = slang::SessionDesc{};
			sessionDesc.targetCount     = 1;
			sessionDesc.targets         = &targetDesc;
			sessionDesc.searchPaths     = c_ShaderSearchPaths;
			sessionDesc.searchPathCount = std::size(c_ShaderSearchPaths);
			// Match the column-major matrices the CPU side uploads, as the D3D12 session does.
			sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
			sessionDesc.preprocessorMacros      = macros.data();
			sessionDesc.preprocessorMacroCount  = static_cast<SlangInt>(macros.size());

			auto session = Slang::ComPtr<slang::ISession>();
			globalSession->createSession(sessionDesc, session.writeRef());
			if (session == nullptr)
				throw GraphicsError("wgsl: failed to create the Slang session");

			return session;
		}

		wgpu::Device
		RequestDevice(const wgpu::Instance& instance, const wgpu::Adapter& adapter)
		{
			// Request the adapter's full limits: the default maxComputeWorkgroupSizeX is 256, but
			// TransparentSort dispatches 512 threads (one per bitonic compare-exchange pair).
			auto limits = wgpu::Limits{};
			adapter.GetLimits(&limits);

			auto deviceDesc           = wgpu::DeviceDescriptor{};
			deviceDesc.requiredLimits = &limits;
			deviceDesc.SetUncapturedErrorCallback(OnUncapturedError);
			deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous, OnDeviceLost);

			wgpu::Device device;
			std::string  message;

			WaitFor(
				instance,
				adapter.RequestDevice(
					&deviceDesc,
					wgpu::CallbackMode::WaitAnyOnly,
					[&](wgpu::RequestDeviceStatus status, wgpu::Device got, wgpu::StringView msg) {
						if (status == wgpu::RequestDeviceStatus::Success)
							device = std::move(got);
						else
							message = std::string(std::string_view(msg));
					}));

			if (device == nullptr)
				throw GraphicsError("wgpu: could not create device: " + message);

			return device;
		}
	}

	Device::Device(const WgpuDeviceDesc& desc)
	{
		// Blocking on a future is opt-in: without TimedWaitAny, WaitAny rejects any non-zero
		// timeout. A browser has no such feature and must poll instead.
		constexpr auto features =
			std::array<wgpu::InstanceFeatureName, 1>{ { wgpu::InstanceFeatureName::TimedWaitAny } };

		auto instanceDesc                 = wgpu::InstanceDescriptor{};
		instanceDesc.requiredFeatureCount = features.size();
		instanceDesc.requiredFeatures     = features.data();

		m_Instance = wgpu::CreateInstance(&instanceDesc);
		if (m_Instance == nullptr)
			throw GraphicsError("wgpu: could not create instance");

		m_Adapter     = RequestAdapter(m_Instance, desc);
		m_AdapterInfo = ReadAdapterInfo(m_Adapter);
		m_Device      = RequestDevice(m_Instance, m_Adapter);
		m_Queue       = m_Device.GetQueue();

		logger::info(
			"[wgpu] adapter '{}' ({}), backend {}",
			m_AdapterInfo.device,
			m_AdapterInfo.description,
			static_cast<int>(m_AdapterInfo.backendType));

		slang::createGlobalSession(m_SlangGlobalSession.writeRef());
		if (m_SlangGlobalSession == nullptr)
			throw GraphicsError("wgsl: failed to create the Slang global session");

		m_SlangSession = CreateWgslSession(m_SlangGlobalSession.get());
	}

	core::SharedRef<ICommandQueue>
	Device::CreateCommandQueue(QueueType) const noexcept
	{
		// WebGPU exposes one queue per device, so every type resolves to the same one; the
		// distinction only survives as the CommandListDesc::type a list records against.
		return core::SharedRef<CommandQueue>::Make(m_Instance, m_Queue);
	}

	core::SharedRef<ICommandAllocator>
	Device::CreateCommandAllocator(QueueType) const noexcept
	{
		return core::SharedRef<CommandAllocator>::Make();
	}

	// The allocator is unused: a WebGPU encoder owns its own memory, so CommandAllocator is an
	// empty shell kept only for the RHI's shape.
	core::SharedRef<ICommandList>
	Device::CreateCommandList(
		const CommandListDesc& desc,
		core::SharedRef<ICommandAllocator> /*commandAllocator*/,
		core::SharedRef<IResourceManager> resourceManager) const noexcept
	{
		return core::SharedRef<CommandList>::Make(m_Device, desc, std::move(resourceManager));
	}

	core::SharedRef<IResourceManager>
	Device::CreateResourceManager(const ResourceManagerDesc& desc) const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device, m_Instance, desc);
	}

	// Unreachable until pipelines exist: there is no way to obtain the argument. The body is
	// backend-agnostic (it reads reflection off the pipeline) and lands with them.
	Uniforms
	Device::CreateUniforms(IMeshletPipeline const*, const std::string&) const noexcept
	{
		gfatal("CreateUniforms: WebGPU has no mesh shaders");
	}

	Uniforms
	Device::CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		gassert(pipeline != nullptr, "CreateUniforms: null pipeline");
		return Uniforms(pipeline, cbufferName);
	}

	core::SharedRef<IShader>
	Device::CreateShader(ShaderDesc desc) const noexcept
	{
		return core::SharedRef<Shader>::Make(std::move(desc), m_SlangSession.get());
	}

	core::SharedRef<IComputePipeline>
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(m_Device, m_SlangSession.get(), desc);
	}

	core::SharedRef<IMeshletPipeline>
	Device::CreateMeshletPipeline(const MeshletPipelineDesc&) const noexcept
	{
		gfatal("CreateMeshletPipeline: WebGPU has no mesh shaders");
	}

	RenderTargetRef
	Device::CreateRenderTarget(
		const RenderTargetDesc&,
		core::SharedRef<ICommandQueue>,
		core::SharedRef<IResourceManager>,
		bool) const
	{
		throw GraphicsError("CreateRenderTarget: not implemented on the WebGPU backend yet");
	}
}
