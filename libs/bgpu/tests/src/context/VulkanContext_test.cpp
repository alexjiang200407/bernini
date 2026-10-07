// What the Vulkan context adds to the cases every backend's context passes: the device it creates,
// the validation layer's messages in the log, and SPIR-V out of the sessions.
//
// bgpu_tests globs every .cpp under tests/ whatever the backend, so a Vulkan-only case has to exclude
// itself: the headers below do not exist on any other build.
#if defined(RENDERER_BACKEND_VULKAN)

#	include "native_device_vulkan.h"
#	include "volk_vulkan.h"

#	include <algorithm>
#	include <bgpu/GpuContext.h>
#	include <catch2/catch_message.hpp>
#	include <catch2/catch_test_macros.hpp>
#	include <cstddef>
#	include <cstdint>
#	include <cstring>
#	include <memory>
#	include <slang-com-ptr.h>
#	include <slang.h>
#	include <spdlog/common.h>
#	include <spdlog/details/log_msg_buffer.h>
#	include <spdlog/sinks/ringbuffer_sink.h>
#	include <spdlog/sinks/sink.h>
#	include <spdlog/spdlog.h>
#	include <string_view>
#	include <vector>

namespace
{
	bgpu::GpuContextDesc
	DebugDesc(const bgpu::LogLevel logLevel)
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		desc.logLevel         = logLevel;
		return desc;
	}

	/**
	 * A second sink on the logger a context writes bgpu.log through, kept in memory: what a case
	 * reads back is what the file was given, without racing the other shards of the suite for it.
	 *
	 * Attached after the context exists, since creating the first one is what installs the logger,
	 * so it holds nothing the context logged while it was being created.
	 */
	class CapturedLog
	{
	public:
		CapturedLog() { spdlog::default_logger()->sinks().push_back(m_Sink); }

		~CapturedLog()
		{
			std::vector<spdlog::sink_ptr>& sinks = spdlog::default_logger()->sinks();
			std::erase(sinks, std::static_pointer_cast<spdlog::sinks::sink>(m_Sink));
		}

		CapturedLog(const CapturedLog&) = delete;
		CapturedLog(CapturedLog&&)      = delete;
		CapturedLog&
		operator=(const CapturedLog&) = delete;
		CapturedLog&
		operator=(CapturedLog&&) = delete;

		[[nodiscard]] size_t
		CountContaining(const std::string_view needle) const
		{
			return Count([needle](const spdlog::details::log_msg_buffer& message) {
				return std::string_view(message.payload.data(), message.payload.size())
				    .contains(needle);
			});
		}

		[[nodiscard]] size_t
		CountAtOrAbove(const spdlog::level::level_enum level) const
		{
			return Count([level](const spdlog::details::log_msg_buffer& message) {
				return message.level >= level;
			});
		}

	private:
		template <typename Predicate>
		[[nodiscard]] size_t
		Count(Predicate predicate) const
		{
			const std::vector<spdlog::details::log_msg_buffer> messages = m_Sink->last_raw();
			return static_cast<size_t>(std::ranges::count_if(messages, predicate));
		}

		std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> m_Sink =
			std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
	};
}

// The RHI's objects are built on these three, and an owner takes queues of its own from the device:
// a device's queues are fixed when it is created, so the context asks for all of them.
TEST_CASE(
	"The Vulkan context holds a 1.3 device with every queue the hardware offers",
	"[device][vulkan]")
{
	auto context = bgpu::CreateGpuContext(DebugDesc(bgpu::LogLevel::kWarn));

	const bgpu::VulkanHandles handles = bgpu::GetVulkanHandles(*context);
	REQUIRE(handles.instance != VK_NULL_HANDLE);
	REQUIRE(handles.physicalDevice != VK_NULL_HANDLE);
	REQUIRE(handles.device != VK_NULL_HANDLE);

	auto properties = VkPhysicalDeviceProperties();
	vkGetPhysicalDeviceProperties(handles.physicalDevice, &properties);
	CHECK(properties.apiVersion >= VK_API_VERSION_1_3);

	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(handles.physicalDevice, &familyCount, nullptr);
	auto families = std::vector<VkQueueFamilyProperties>(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(handles.physicalDevice, &familyCount, families.data());
	REQUIRE(familyCount > 0);

	// Under the validation layer, asking for a queue the device was not created with is an error.
	const auto captured = CapturedLog();
	size_t     queues   = 0;
	for (uint32_t family = 0; family < familyCount; ++family)
	{
		for (uint32_t index = 0; index < families[family].queueCount; ++index)
		{
			VkQueue queue = VK_NULL_HANDLE;
			vkGetDeviceQueue(handles.device, family, index, &queue);
			CHECK(queue != VK_NULL_HANDLE);
			++queues;
		}
	}
	CHECK(queues > 0);
	CHECK(captured.CountAtOrAbove(spdlog::level::warn) == 0);
}

// The minimum requirements are only worth checking if the device is then created with what they
// name. Nothing can ask a VkDevice what it was created with, so this uses one: a bindless table is a
// descriptor array that is partially bound and updated after bind, which the validation layer refuses
// on a device whose features do not include both. It is declared to the mesh and task stages, which
// the layer refuses in turn on a device created without them.
TEST_CASE(
	"The Vulkan device is created with the bindless features the bar names",
	"[device][vulkan]")
{
	auto                      context = bgpu::CreateGpuContext(DebugDesc(bgpu::LogLevel::kWarn));
	const bgpu::VulkanHandles handles = bgpu::GetVulkanHandles(*context);

	const auto captured = CapturedLog();

	auto binding            = VkDescriptorSetLayoutBinding();
	binding.binding         = 0;
	binding.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	binding.descriptorCount = 1024;
	binding.stageFlags =
		VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

	const VkDescriptorBindingFlags bindless =
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	auto flags          = VkDescriptorSetLayoutBindingFlagsCreateInfo();
	flags.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	flags.bindingCount  = 1;
	flags.pBindingFlags = &bindless;

	auto info         = VkDescriptorSetLayoutCreateInfo();
	info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	info.pNext        = &flags;
	info.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	info.bindingCount = 1;
	info.pBindings    = &binding;

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	REQUIRE(vkCreateDescriptorSetLayout(handles.device, &info, nullptr, &layout) == VK_SUCCESS);
	vkDestroyDescriptorSetLayout(handles.device, layout, nullptr);

	CHECK(captured.CountAtOrAbove(spdlog::level::warn) == 0);
}

// The messenger is the only route a validation message has out of the layer. A message the suite
// submits itself goes down the same route as one the layer raises, without needing an invalid call.
TEST_CASE("A Vulkan validation message is written to the log", "[device][vulkan]")
{
	auto                      context = bgpu::CreateGpuContext(DebugDesc(bgpu::LogLevel::kWarn));
	const bgpu::VulkanHandles handles = bgpu::GetVulkanHandles(*context);

	const auto captured = CapturedLog();

	auto message           = VkDebugUtilsMessengerCallbackDataEXT();
	message.sType          = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
	message.pMessageIdName = "bgpu_tests";
	message.pMessage       = "a warning bgpu_tests raised itself";
	vkSubmitDebugUtilsMessageEXT(
		handles.instance,
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
		&message);

	CHECK(captured.CountContaining("[Vulkan] a warning bgpu_tests raised itself") == 1);
}

// What D3D12's live-object report is there: each owner destroys what it made before dropping its
// context reference, so anything left when the device goes is a leak, and the log names it.
TEST_CASE("An object that outlives the Vulkan context is named in the log", "[device][vulkan]")
{
	auto                      context = bgpu::CreateGpuContext(DebugDesc(bgpu::LogLevel::kError));
	const bgpu::VulkanHandles handles = bgpu::GetVulkanHandles(*context);

	auto info  = VkSamplerCreateInfo();
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

	VkSampler sampler = VK_NULL_HANDLE;
	REQUIRE(vkCreateSampler(handles.device, &info, nullptr, &sampler) == VK_SUCCESS);

	auto name         = VkDebugUtilsObjectNameInfoEXT();
	name.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	name.objectType   = VK_OBJECT_TYPE_SAMPLER;
	name.objectHandle = reinterpret_cast<uint64_t>(sampler);
	name.pObjectName  = "leaked by bgpu_tests";
	REQUIRE(vkSetDebugUtilsObjectNameEXT(handles.device, &name) == VK_SUCCESS);

	const auto captured = CapturedLog();
	CHECK(captured.CountContaining("leaked by bgpu_tests") == 0);

	// Deliberately not destroyed: the device takes the sampler with it.
	context = nullptr;
	CHECK(captured.CountContaining("leaked by bgpu_tests") >= 1);
}

// Loading a module only parses and checks it. Generating code is what proves the sessions were made
// for this device's target, and that bgpu's own Slang tree is expressible in it.
TEST_CASE("A kernel compiles to SPIR-V through the Vulkan context's session", "[device][vulkan]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());

	slang::IModule* kernel = context->LoadModule("bgpu.CSEntrySquare");
	REQUIRE(kernel != nullptr);

	// Scoped, so every Slang object is released before the context that owns the session.
	{
		Slang::ComPtr<slang::IEntryPoint> entryPoint;
		REQUIRE(SLANG_SUCCEEDED(kernel->findEntryPointByName("main", entryPoint.writeRef())));

		slang::IComponentType* const         parts[] = { kernel, entryPoint.get() };
		Slang::ComPtr<slang::IComponentType> composed;
		REQUIRE(SLANG_SUCCEEDED(
			kernel->getSession()->createCompositeComponentType(parts, 2, composed.writeRef())));

		Slang::ComPtr<slang::IComponentType> linked;
		REQUIRE(SLANG_SUCCEEDED(composed->link(linked.writeRef())));

		Slang::ComPtr<slang::IBlob> code;
		Slang::ComPtr<slang::IBlob> diagnostics;
		const SlangResult           compiled =
			linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef());
		if (diagnostics != nullptr)
			UNSCOPED_INFO(static_cast<const char*>(diagnostics->getBufferPointer()));
		REQUIRE(SLANG_SUCCEEDED(compiled));

		constexpr uint32_t c_SpirvMagic = 0x07230203;
		REQUIRE(code->getBufferSize() >= sizeof(uint32_t));
		uint32_t magic = 0;
		std::memcpy(&magic, code->getBufferPointer(), sizeof(magic));
		CHECK(magic == c_SpirvMagic);
	}
}

#endif
