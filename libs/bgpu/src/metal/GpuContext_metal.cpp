#include "ContextBase.h"
#include "metal_cpp.h"
#include "native_device_metal.h"
#include <bgpu/GpuContext.h>
#include <bgpu/SystemRequirements.h>
#include <core/err/util.h>
#include <core/log/log.h>
#include <core/platform/util.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <slang.h>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/sysctl.h>
#include <utility>
#include <vector>

namespace bgpu
{
	namespace
	{
		[[nodiscard]] std::string
		ProcessorName()
		{
			size_t size = 0;
			if (sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0) != 0 ||
			    size == 0)
				return {};
			auto name = std::string(size, '\0');
			if (sysctlbyname("machdep.cpu.brand_string", name.data(), &size, nullptr, 0) != 0)
				return {};
			name.resize(name.find('\0'));
			return name;
		}

		// hw.optional.arm64 rather than the build's architecture: an x86_64 build under Rosetta runs on
		// Apple silicon, and its GPU is the one that counts.
		[[nodiscard]] bool
		IsAppleSilicon() noexcept
		{
			int    arm64 = 0;
			size_t size  = sizeof(arm64);
			return sysctlbyname("hw.optional.arm64", &arm64, &size, nullptr, 0) == 0 && arm64 == 1;
		}

		[[nodiscard]] AppleSystemFacts
		ReadSystemFacts(MTL::Device* device)
		{
			auto facts         = AppleSystemFacts();
			facts.appleSilicon = IsAppleSilicon();
			facts.processor    = ProcessorName();

			const NS::OperatingSystemVersion os =
				NS::ProcessInfo::processInfo()->operatingSystemVersion();
			facts.osMajor = static_cast<uint32_t>(os.majorVersion);
			facts.osMinor = static_cast<uint32_t>(os.minorVersion);
			facts.osPatch = static_cast<uint32_t>(os.patchVersion);

			if (device == nullptr)
				return facts;

			facts.gpuName     = device->name()->utf8String();
			facts.metal3      = device->supportsFamily(MTL::GPUFamilyMetal3);
			facts.meshShaders = device->supportsFamily(MTL::GPUFamilyApple7);
			facts.argumentBuffersTier2 =
				device->argumentBuffersSupport() == MTL::ArgumentBuffersTier2;
			return facts;
		}

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

				// Below the bar the first mesh pipeline is where it would fail, with nothing a player
				// could act on.
				std::vector<UnmetRequirement> unmet =
					CheckSystemRequirements(ReadSystemFacts(m_Device.get()));
				if (!unmet.empty())
				{
					auto error = UnsupportedSystem(std::move(unmet));
					spdlog::critical("{}", error.what());
					throw error;
				}

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
