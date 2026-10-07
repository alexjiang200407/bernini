#include "ContextBase.h"
#include "native_device_vulkan.h"
#include "volk_vulkan.h"
#include <algorithm>
#include <array>
#include <bgpu/GpuContext.h>
#include <bgpu/SystemRequirements.h>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/log/log.h>
#include <core/platform/util.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <numeric>
#include <slang.h>
#include <span>
#include <spdlog/spdlog.h>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>

namespace bgpu
{
	namespace
	{
		constexpr const char* c_ValidationLayer         = "VK_LAYER_KHRONOS_validation";
		constexpr const char* c_ValidationLayerManifest = "VkLayer_khronos_validation.json";
		constexpr const char* c_LayerPathVariable       = "VK_ADD_LAYER_PATH";

		// One list for the read and the enable, so a device is created with what was checked.
		constexpr auto c_DescriptorIndexingFeatures =
			std::to_array<VkBool32 VkPhysicalDeviceVulkan12Features::*>({
				&VkPhysicalDeviceVulkan12Features::runtimeDescriptorArray,
				&VkPhysicalDeviceVulkan12Features::descriptorBindingPartiallyBound,
				&VkPhysicalDeviceVulkan12Features::descriptorBindingSampledImageUpdateAfterBind,
				&VkPhysicalDeviceVulkan12Features::descriptorBindingStorageImageUpdateAfterBind,
				&VkPhysicalDeviceVulkan12Features::descriptorBindingStorageBufferUpdateAfterBind,
				&VkPhysicalDeviceVulkan12Features::shaderSampledImageArrayNonUniformIndexing,
				&VkPhysicalDeviceVulkan12Features::shaderStorageImageArrayNonUniformIndexing,
				&VkPhysicalDeviceVulkan12Features::shaderStorageBufferArrayNonUniformIndexing,
			});

		// The core features of the graphics bar, read and enabled from the same lists.
		constexpr auto c_GraphicsFeatures = std::to_array<VkBool32 VkPhysicalDeviceFeatures::*>({
			&VkPhysicalDeviceFeatures::independentBlend,
			&VkPhysicalDeviceFeatures::dualSrcBlend,
			&VkPhysicalDeviceFeatures::fillModeNonSolid,
			&VkPhysicalDeviceFeatures::depthClamp,
			&VkPhysicalDeviceFeatures::multiViewport,
			&VkPhysicalDeviceFeatures::samplerAnisotropy,
			&VkPhysicalDeviceFeatures::textureCompressionBC,
		});
		constexpr auto c_GraphicsFeatures12 =
			std::to_array<VkBool32 VkPhysicalDeviceVulkan12Features::*>({
				&VkPhysicalDeviceVulkan12Features::drawIndirectCount,
				&VkPhysicalDeviceVulkan12Features::samplerFilterMinmax,
				&VkPhysicalDeviceVulkan12Features::samplerMirrorClampToEdge,
			});

		/** The feature structs the minimum requirements name, chained as Vulkan takes them. */
		struct FeatureChain
		{
			VkPhysicalDeviceFeatures2                        features2         = {};
			VkPhysicalDeviceVulkan12Features                 vulkan12          = {};
			VkPhysicalDeviceVulkan13Features                 vulkan13          = {};
			VkPhysicalDeviceMeshShaderFeaturesEXT            meshShader        = {};
			VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableDescriptor = {};

			FeatureChain() noexcept
			{
				features2.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
				features2.pNext  = &vulkan12;
				vulkan12.sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
				vulkan12.pNext   = &vulkan13;
				vulkan13.sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
				vulkan13.pNext   = &meshShader;
				meshShader.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
				meshShader.pNext = &mutableDescriptor;
				mutableDescriptor.sType =
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT;
			}

			FeatureChain(const FeatureChain&) = delete;
			FeatureChain(FeatureChain&&)      = delete;
			FeatureChain&
			operator=(const FeatureChain&) = delete;
			FeatureChain&
			operator=(FeatureChain&&) = delete;
		};

		[[noreturn]] void
		ThrowFailed(const char* call, const VkResult result)
		{
			core::throw_runtime_error("{} failed: {}", call, string_VkResult(result));
		}

		[[noreturn]] void
		ThrowUnsupported(const VulkanSystemFacts& facts)
		{
			auto error = UnsupportedSystem(CheckSystemRequirements(facts));
			spdlog::critical("{}", error.what());
			throw error;
		}

		// The build stages the validation layer beside the executable, where the loader does not
		// look. A path the environment already names is left alone: whoever set it chose a layer.
		void
		AddStagedLayerPath()
		{
			if (core::env_var(c_LayerPathVariable).has_value())
				return;

			const std::filesystem::path directory = core::file::get_executable_path().parent_path();
			std::error_code             ec;
			if (!std::filesystem::exists(directory / c_ValidationLayerManifest, ec))
				return;

			if (!core::set_env_var(c_LayerPathVariable, directory.string().c_str()))
				spdlog::warn("could not add {} to {}", directory.string(), c_LayerPathVariable);
		}

		// What the validation layer warns of itself when GPU validation turns its heavier checks on:
		// that they are slow beside the core checks, and that it raised a device limit it needs.
		// Neither is about a call of this process.
		[[nodiscard]] bool
		IsLayerSetupNotice(const char* messageId) noexcept
		{
			if (messageId == nullptr)
				return false;
			const auto id = std::string_view(messageId);
			return id == "VALIDATION-SETTINGS" || id == "WARNING-Setting-Limit-Adjusted";
		}

		[[nodiscard]] bool
		HasExtension(const VkPhysicalDevice physicalDevice, const std::string_view name)
		{
			uint32_t count = 0;
			if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) !=
			    VK_SUCCESS)
				return false;

			auto extensions = std::vector<VkExtensionProperties>(count);
			if (vkEnumerateDeviceExtensionProperties(
					physicalDevice,
					nullptr,
					&count,
					extensions.data()) != VK_SUCCESS)
				return false;

			return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
				return name == extension.extensionName;
			});
		}

		[[nodiscard]] VulkanSystemFacts
		ReadSystemFacts(const VkPhysicalDevice physicalDevice)
		{
			auto facts = VulkanSystemFacts();
			if (physicalDevice == VK_NULL_HANDLE)
				return facts;

			auto properties = VkPhysicalDeviceProperties();
			vkGetPhysicalDeviceProperties(physicalDevice, &properties);
			facts.device     = true;
			facts.gpuName    = properties.deviceName;
			facts.apiVersion = properties.apiVersion;

			if (properties.apiVersion < VK_API_VERSION_1_3)
				return facts;

			// A driver without the mesh extension skips its struct, which stays zero.
			auto supported = FeatureChain();
			vkGetPhysicalDeviceFeatures2(physicalDevice, &supported.features2);

			facts.meshShaders = HasExtension(physicalDevice, VK_EXT_MESH_SHADER_EXTENSION_NAME) &&
			                    supported.meshShader.meshShader == VK_TRUE &&
			                    supported.meshShader.taskShader == VK_TRUE;
			facts.descriptorIndexing = std::ranges::all_of(
				c_DescriptorIndexingFeatures,
				[&supported](const VkBool32 VkPhysicalDeviceVulkan12Features::* feature) {
					return supported.vulkan12.*feature == VK_TRUE;
				});
			facts.scalarBlockLayout = supported.vulkan12.scalarBlockLayout == VK_TRUE;
			facts.synchronization   = supported.vulkan12.timelineSemaphore == VK_TRUE &&
			                          supported.vulkan13.synchronization2 == VK_TRUE;
			facts.mutableDescriptors =
				HasExtension(physicalDevice, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME) &&
				supported.mutableDescriptor.mutableDescriptorType == VK_TRUE;
			facts.graphics =
				supported.vulkan13.dynamicRendering == VK_TRUE &&
				std::ranges::all_of(
					c_GraphicsFeatures,
					[&supported](const VkBool32 VkPhysicalDeviceFeatures::* feature) {
						return supported.features2.features.*feature == VK_TRUE;
					}) &&
				std::ranges::all_of(
					c_GraphicsFeatures12,
					[&supported](const VkBool32 VkPhysicalDeviceVulkan12Features::* feature) {
						return supported.vulkan12.*feature == VK_TRUE;
					});
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

			explicit Context(const GpuContextDesc& desc) :
				ContextBase(desc, SLANG_SPIRV),
				m_GpuValidation(desc.enableDebugLayer && desc.enableGPUValidationLayer)
			{
				core::logging::init_file_logger("bgpu.log", std::to_underlying(desc.logLevel));

				try
				{
					// No loader is no Vulkan driver: the machine is below the bar, not at fault.
					if (volkInitialize() != VK_SUCCESS)
						ThrowUnsupported(VulkanSystemFacts());

					CreateInstance();
					PickPhysicalDevice();
					CreateDevice();
				}
				catch (...)
				{
					DestroyAll();
					throw;
				}
			}

			~Context() noexcept override
			{
				spdlog::trace("~GpuContext");
				DestroyAll();
			}

			bool
			GpuValidationActive() const noexcept override
			{
				return m_GpuValidation;
			}

			[[nodiscard]] VulkanHandles
			GetHandles() const noexcept
			{
				return { m_Instance, m_PhysicalDevice, m_Device };
			}

			[[nodiscard]] std::span<const VkQueueFamilyProperties>
			GetQueueFamilies() const noexcept
			{
				return m_QueueFamilies;
			}

		private:
			void
			CreateInstance()
			{
				const GpuContextDesc& desc = GetDesc();

				auto application        = VkApplicationInfo();
				application.sType       = VK_STRUCTURE_TYPE_APPLICATION_INFO;
				application.pEngineName = "Bernini";
				application.apiVersion  = VK_API_VERSION_1_3;

				auto messenger            = VkDebugUtilsMessengerCreateInfoEXT();
				messenger.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
				messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				messenger.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
				messenger.pfnUserCallback = &Context::LogMessage;
				messenger.pUserData       = this;

				// GPU validation is D3D12's GPU-based validation's two halves: what shaders reach,
				// checked on the GPU, and whether barriers order every access, checked by
				// synchronization validation.
				const VkBool32 enabled  = VK_TRUE;
				auto           settings = std::array<VkLayerSettingEXT, 2>();
				for (VkLayerSettingEXT& setting : settings)
				{
					setting.pLayerName = c_ValidationLayer;
					setting.type       = VK_LAYER_SETTING_TYPE_BOOL32_EXT;
					setting.valueCount = 1;
					setting.pValues    = &enabled;
				}
				settings[0].pSettingName = "gpuav_enable";
				settings[1].pSettingName = "validate_sync";

				auto layerSettings         = VkLayerSettingsCreateInfoEXT();
				layerSettings.sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
				layerSettings.pNext        = &messenger;
				layerSettings.settingCount = static_cast<uint32_t>(settings.size());
				layerSettings.pSettings    = settings.data();

				auto layers     = std::vector<const char*>();
				auto extensions = std::vector<const char*>();

				auto info             = VkInstanceCreateInfo();
				info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
				info.pApplicationInfo = &application;

				if (desc.enableDebugLayer)
				{
					AddStagedLayerPath();
					layers.push_back(c_ValidationLayer);
					extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

					// Chained too, so the messages of creating and destroying the instance itself
					// are routed: the messenger object below exists only between the two.
					info.pNext = &messenger;
					if (m_GpuValidation)
					{
						extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
						info.pNext = &layerSettings;
					}
				}

				info.enabledLayerCount       = static_cast<uint32_t>(layers.size());
				info.ppEnabledLayerNames     = layers.data();
				info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
				info.ppEnabledExtensionNames = extensions.data();

				const VkResult created = vkCreateInstance(&info, nullptr, &m_Instance);
				// Only "incompatible" says the machine is below the bar; anything else is a fault.
				if (created == VK_ERROR_INCOMPATIBLE_DRIVER)
					ThrowUnsupported(VulkanSystemFacts());
				if (created == VK_ERROR_LAYER_NOT_PRESENT)
				{
					core::throw_runtime_error(
						"the Vulkan validation layer ({}) is not installed: the debug layer needs "
						"the one the build stages beside the executable, or a Vulkan SDK",
						c_ValidationLayer);
				}
				if (created != VK_SUCCESS)
					ThrowFailed("vkCreateInstance", created);

				volkLoadInstance(m_Instance);

				if (desc.enableDebugLayer)
				{
					const VkResult routed = vkCreateDebugUtilsMessengerEXT(
						m_Instance,
						&messenger,
						nullptr,
						&m_Messenger);
					if (routed != VK_SUCCESS)
						ThrowFailed("vkCreateDebugUtilsMessengerEXT", routed);
				}
			}

			// The first device the loader enumerates is the one the system prefers, as DXGI's first
			// adapter is: the engine does not choose between GPUs.
			void
			PickPhysicalDevice()
			{
				uint32_t       count = 1;
				const VkResult enumerated =
					vkEnumeratePhysicalDevices(m_Instance, &count, &m_PhysicalDevice);
				// VK_INCOMPLETE: there are more than the one asked for.
				if (enumerated != VK_SUCCESS && enumerated != VK_INCOMPLETE)
					ThrowFailed("vkEnumeratePhysicalDevices", enumerated);
				if (count == 0)
					m_PhysicalDevice = VK_NULL_HANDLE;

				// Below the bar the first mesh pipeline is where it would fail, with nothing a player
				// could act on.
				const VulkanSystemFacts facts = ReadSystemFacts(m_PhysicalDevice);
				if (!CheckSystemRequirements(facts).empty())
					ThrowUnsupported(facts);

				spdlog::info("Vulkan device: {}", facts.gpuName);
			}

			void
			CreateDevice()
			{
				uint32_t familyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &familyCount, nullptr);
				m_QueueFamilies.resize(familyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(
					m_PhysicalDevice,
					&familyCount,
					m_QueueFamilies.data());
				const std::vector<VkQueueFamilyProperties>& families = m_QueueFamilies;

				uint32_t mostQueues = 0;
				for (const VkQueueFamilyProperties& family : families)
					mostQueues = std::max(mostQueues, family.queueCount);
				const auto priorities = std::vector<float>(mostQueues, 1.0F);

				// A device's queues are fixed when it is created, and each owner takes its own later,
				// so it is made with every queue the hardware offers.
				auto queues = std::vector<VkDeviceQueueCreateInfo>();
				for (uint32_t i = 0; i < familyCount; ++i)
				{
					if (families[i].queueCount == 0)
						continue;

					auto queue             = VkDeviceQueueCreateInfo();
					queue.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					queue.queueFamilyIndex = i;
					queue.queueCount       = families[i].queueCount;
					queue.pQueuePriorities = priorities.data();
					queues.push_back(queue);
				}

				auto required                                    = FeatureChain();
				required.meshShader.meshShader                   = VK_TRUE;
				required.meshShader.taskShader                   = VK_TRUE;
				required.vulkan12.scalarBlockLayout              = VK_TRUE;
				required.vulkan12.timelineSemaphore              = VK_TRUE;
				required.vulkan13.synchronization2               = VK_TRUE;
				required.vulkan13.dynamicRendering               = VK_TRUE;
				required.mutableDescriptor.mutableDescriptorType = VK_TRUE;
				for (VkBool32 VkPhysicalDeviceVulkan12Features::* const feature :
				     c_DescriptorIndexingFeatures)
					required.vulkan12.*feature = VK_TRUE;
				for (VkBool32 VkPhysicalDeviceVulkan12Features::* const feature :
				     c_GraphicsFeatures12)
					required.vulkan12.*feature = VK_TRUE;
				for (VkBool32 VkPhysicalDeviceFeatures::* const feature : c_GraphicsFeatures)
					required.features2.features.*feature = VK_TRUE;

				const auto extensions = std::to_array<const char*>({
					VK_EXT_MESH_SHADER_EXTENSION_NAME,
					VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
				});

				auto info                    = VkDeviceCreateInfo();
				info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				info.pNext                   = &required.features2;
				info.queueCreateInfoCount    = static_cast<uint32_t>(queues.size());
				info.pQueueCreateInfos       = queues.data();
				info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
				info.ppEnabledExtensionNames = extensions.data();

				const VkResult created =
					vkCreateDevice(m_PhysicalDevice, &info, nullptr, &m_Device);
				if (created != VK_SUCCESS)
					ThrowFailed("vkCreateDevice", created);

				volkLoadDevice(m_Device);

				for (uint32_t family = 0; family < familyCount; ++family)
				{
					for (uint32_t index = 0; index < families[family].queueCount; ++index)
					{
						auto slot   = QueueSlot();
						slot.family = family;
						vkGetDeviceQueue(m_Device, family, index, &slot.queue);
						m_Queues.push_back(std::move(slot));
					}
				}
			}

		public:
			[[nodiscard]] VulkanQueue
			AcquireQueue(const std::span<const uint32_t> families) const noexcept
			{
				core::ensure(!families.empty(), "A queue is taken from at least one family");

				const std::lock_guard lock(m_QueueTableLock);
				QueueSlot*            best         = nullptr;
				size_t                bestPriority = 0;
				for (QueueSlot& slot : m_Queues)
				{
					const auto preferred = std::ranges::find(families, slot.family);
					if (preferred == families.end())
						continue;

					const auto priority = static_cast<size_t>(preferred - families.begin());
					if (best == nullptr || slot.holders < best->holders ||
					    (slot.holders == best->holders && priority < bestPriority))
					{
						best         = &slot;
						bestPriority = priority;
					}
				}
				core::ensure(best != nullptr, "No queue in the families asked for");

				++best->holders;
				return { best->queue, best->family, best->submitLock.get() };
			}

			void
			ReleaseQueue(const VulkanQueue& queue) const noexcept
			{
				const std::lock_guard lock(m_QueueTableLock);
				const auto slot = std::ranges::find(m_Queues, queue.queue, &QueueSlot::queue);
				core::ensure(
					slot != m_Queues.end() && slot->holders > 0,
					"Releasing a queue nothing took");
				--slot->holders;
			}

		private:
			// The validation layer names every object that outlived the device as the device is
			// destroyed, through the messenger, so the messenger goes after it.
			void
			DestroyAll() noexcept
			{
				if (m_Device != VK_NULL_HANDLE)
					vkDestroyDevice(m_Device, nullptr);
				if (m_Messenger != VK_NULL_HANDLE)
					vkDestroyDebugUtilsMessengerEXT(m_Instance, m_Messenger, nullptr);
				if (m_Instance != VK_NULL_HANDLE)
					vkDestroyInstance(m_Instance, nullptr);

				m_Device         = VK_NULL_HANDLE;
				m_Messenger      = VK_NULL_HANDLE;
				m_PhysicalDevice = VK_NULL_HANDLE;
				m_Instance       = VK_NULL_HANDLE;
				volkFinalize();
			}

			static VKAPI_ATTR VkBool32 VKAPI_CALL
			LogMessage(
				const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
				const VkDebugUtilsMessageTypeFlagsEXT        types,
				const VkDebugUtilsMessengerCallbackDataEXT*  data,
				void*                                        context)
			{
				// The message is optional in the registry, and fmt refuses a null string.
				const char* message = data->pMessage != nullptr ? data->pMessage : "";

				if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
					spdlog::error("[Vulkan] {}", message);
				else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
					spdlog::warn("[Vulkan] {}", message);
				else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
					spdlog::info("[Vulkan] {}", message);
				else
					spdlog::debug("[Vulkan] {}", message);

				// Only what the validation layer says of this process's own calls is strict: the
				// loader warns, as the general type, of other software's broken layers and drivers,
				// and the layer warns of its own setup when GPU validation is on.
				const bool severe = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT &&
				                    (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0 &&
				                    !IsLayerSetupNotice(data->pMessageIdName);
				const auto* self  = static_cast<const Context*>(context);
				if (severe && self->GetDesc().strictError)
					core::fatal(
						"[Vulkan] strict error ({}): {}",
						data->pMessageIdName != nullptr ? data->pMessageIdName : "no id",
						message);

				return VK_FALSE;
			}

			VkInstance               m_Instance       = VK_NULL_HANDLE;
			VkDebugUtilsMessengerEXT m_Messenger      = VK_NULL_HANDLE;
			VkPhysicalDevice         m_PhysicalDevice = VK_NULL_HANDLE;
			VkDevice                 m_Device         = VK_NULL_HANDLE;
			bool                     m_GpuValidation  = false;

			std::vector<VkQueueFamilyProperties> m_QueueFamilies;

			// Every queue of every family, with how many owners hold it. Owners come and go after
			// the device is made, so the table is mutable under its own lock.
			struct QueueSlot
			{
				VkQueue                     queue      = VK_NULL_HANDLE;
				uint32_t                    family     = 0;
				uint32_t                    holders    = 0;
				std::unique_ptr<std::mutex> submitLock = std::make_unique<std::mutex>();
			};
			mutable std::vector<QueueSlot> m_Queues;
			mutable std::mutex             m_QueueTableLock;
		};
	}

	VulkanHandles
	GetVulkanHandles(const GpuContext& context) noexcept
	{
		const auto* vulkan = dynamic_cast<const Context*>(&context);
		core::ensure(vulkan != nullptr, "The GPU context is not a Vulkan one");
		return vulkan->GetHandles();
	}

	std::span<const VkQueueFamilyProperties>
	GetVulkanQueueFamilies(const GpuContext& context) noexcept
	{
		const auto* vulkan = dynamic_cast<const Context*>(&context);
		core::ensure(vulkan != nullptr, "The GPU context is not a Vulkan one");
		return vulkan->GetQueueFamilies();
	}

	VulkanSharing
	GetVulkanSharing(const GpuContext& context)
	{
		const size_t familyCount = GetVulkanQueueFamilies(context).size();
		if (familyCount < 2)
			return {};

		auto sharing     = VulkanSharing();
		sharing.mode     = VK_SHARING_MODE_CONCURRENT;
		sharing.families = std::vector<uint32_t>(familyCount);
		std::iota(sharing.families.begin(), sharing.families.end(), 0U);
		return sharing;
	}

	VulkanQueue
	AcquireVulkanQueue(const GpuContext& context, const std::span<const uint32_t> families) noexcept
	{
		const auto* vulkan = dynamic_cast<const Context*>(&context);
		core::ensure(vulkan != nullptr, "The GPU context is not a Vulkan one");
		return vulkan->AcquireQueue(families);
	}

	void
	ReleaseVulkanQueue(const GpuContext& context, const VulkanQueue& queue) noexcept
	{
		const auto* vulkan = dynamic_cast<const Context*>(&context);
		core::ensure(vulkan != nullptr, "The GPU context is not a Vulkan one");
		vulkan->ReleaseQueue(queue);
	}

	GpuContextRef
	CreateGpuContext(const GpuContextDesc& desc)
	{
		return core::SharedRef<Context>::Make(desc);
	}
}
