#include "ContextBase.h"
#include "metal_cpp.h"
#include "native_device_metal.h"
#include <bgpu/GpuContext.h>
#include <core/err/util.h>
#include <core/log/log.h>
#include <core/platform/util.h>
#include <core/ref/SharedRef.h>
#include <slang.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace bgpu
{
	namespace
	{
		class Context final : public ContextBase
		{
		public:
			Context(const Context&) = delete;
			Context(Context&&)      = delete;
			Context&
			operator=(const Context&) = delete;
			Context&
			operator=(Context&&) = delete;

			explicit Context(const GpuContextDesc& desc) : ContextBase(desc, SLANG_METAL)
			{
				core::logging::init_file_logger("bgpu.log", std::to_underlying(desc.logLevel));

				// The device's name is autoreleased, so the scope that reads it owns a pool.
				NS::SharedPtr<NS::AutoreleasePool> pool =
					NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

				m_Device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
				if (!m_Device)
					core::throw_runtime_error("no Metal device available");

				spdlog::info("Metal device: {}", m_Device->name()->utf8String());

				// Metal's validation is switched on by the environment, not by us, so the option
				// alone cannot say whether it is running. Either variable instruments shaders enough
				// that a binary archive written without them no longer describes what the driver
				// will run.
				m_GpuValidation = desc.enableGPUValidationLayer ||
				                  core::env_var("MTL_SHADER_VALIDATION").has_value() ||
				                  core::env_var("METAL_DEVICE_WRAPPER_TYPE").has_value();
			}

			~Context() noexcept override { spdlog::trace("~GpuContext"); }

			bool
			GpuValidationActive() const noexcept override
			{
				return m_GpuValidation;
			}

			[[nodiscard]] MTL::Device*
			GetDevice() const noexcept
			{
				return m_Device.get();
			}

		private:
			NS::SharedPtr<MTL::Device> m_Device;
			bool                       m_GpuValidation = false;
		};
	}

	MTL::Device*
	GetMtlDevice(const GpuContext& context) noexcept
	{
		const auto* metal = dynamic_cast<const Context*>(&context);
		core::ensure(metal != nullptr, "The GPU context is not a Metal one");
		return metal->GetDevice();
	}

	GpuContextRef
	CreateGpuContext(const GpuContextDesc& desc)
	{
		return core::SharedRef<Context>::Make(desc);
	}
}
