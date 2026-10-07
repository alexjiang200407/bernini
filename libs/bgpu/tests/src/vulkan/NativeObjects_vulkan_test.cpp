// What a Vulkan device and queue answer to GetNativeObject: the handles a renderer's swapchain is made
// from, and a queue a caller can submit beside -- a present waiting for a frame the RHI submitted.
//
// bgpu_tests globs every .cpp under tests/ whatever the backend, so a Vulkan-only case has to exclude
// itself: the headers below do not exist on any other build.
#if defined(RENDERER_BACKEND_VULKAN)
#	include "native_device_vulkan.h"
#	include "volk_vulkan.h"
#	include <bgpu/GpuContext.h>
#	include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#	include <bgpu/cmd/CommandList.h>
#	include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#	include <bgpu/device/Device.h>
#	include <bgpu/resource/Buffer.h>
#	include <bgpu/resource/ResourceManager.h>
#	include <bgpu/types/NativeObject.h>
#	include <bgpu/types/NativeVkQueue.h>
#	include <bgpu/types/QueueType.h>
#	include <catch2/catch_test_macros.hpp>
#	include <cstdint>
#	include <limits>
#	include <mutex>

namespace
{
	bgpu::GpuContextRef
	DebugContext()
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		desc.strictError      = true;
		return bgpu::CreateGpuContext(desc);
	}
}

TEST_CASE("A Vulkan device answers its context's instance, physical device and device", "[vulkan]")
{
	auto context = DebugContext();
	auto device  = bgpu::CreateDevice(context);

	const bgpu::VulkanHandles handles = bgpu::GetVulkanHandles(*context);

	CHECK(
		device->GetNativeObject(bgpu::NativeObjectType::kVkInstance).As<VkInstance_T>() ==
		handles.instance);
	CHECK(
		device->GetNativeObject(bgpu::NativeObjectType::kVkPhysicalDevice)
			.As<VkPhysicalDevice_T>() == handles.physicalDevice);
	CHECK(
		device->GetNativeObject(bgpu::NativeObjectType::kVkDevice).As<VkDevice_T>() ==
		handles.device);

	CHECK_FALSE(device->GetNativeObject(bgpu::NativeObjectType::kD3D12Device));
	CHECK_FALSE(device->GetNativeObject(bgpu::NativeObjectType::kVkQueue));
}

TEST_CASE(
	"A Vulkan queue's timeline is its fence, and a submission beside it can wait on it",
	"[vulkan]")
{
	auto context = DebugContext();
	auto device  = bgpu::CreateDevice(context);
	auto rm      = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());
	auto queue   = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
	rm->RegisterQueue(queue.Get());

	const auto* native =
		queue->GetNativeObject(bgpu::NativeObjectType::kVkQueue).As<bgpu::NativeVkQueue>();
	REQUIRE(native != nullptr);
	REQUIRE(native->queue != nullptr);
	REQUIRE(native->timeline != nullptr);
	REQUIRE(native->submitLock != nullptr);
	CHECK(native->family < bgpu::GetVulkanQueueFamilies(*context).size());
	CHECK_FALSE(device->CreateCommandQueue(bgpu::QueueType::kGraphics)
	                ->GetNativeObject(bgpu::NativeObjectType::kVkBuffer));

	auto alloc    = device->CreateCommandAllocator();
	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kGraphics;
	auto list     = device->CreateCommandList(listDesc, alloc, rm);

	auto bufDesc                 = bgpu::RawViewDesc();
	bufDesc.byteSize             = 1024 * 1024;
	bufDesc.debugName            = "native queue probe";
	bufDesc.isUav                = true;
	const bgpu::BufferHandle src = rm->CreateRawBuffer(bufDesc);
	const bgpu::BufferHandle dst = rm->CreateRawBuffer(bufDesc);

	list->Open(queue.Get(), alloc.Get());
	list->CopyBuffer(dst, src, 0, 0, bufDesc.byteSize);
	list->Close();
	const uint64_t frame = queue->ExecuteCommandList(list.Get());

	// What a present does: an empty batch on the same VkQueue, behind the frame's fence value,
	// signalling something the queue's own submissions know nothing of.
	const VkDevice vkDevice  = bgpu::GetVulkanHandles(*context).device;
	auto           fenceInfo = VkFenceCreateInfo();
	fenceInfo.sType          = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence            = VK_NULL_HANDLE;
	REQUIRE(vkCreateFence(vkDevice, &fenceInfo, nullptr, &fence) == VK_SUCCESS);

	auto wait      = VkSemaphoreSubmitInfo();
	wait.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	wait.semaphore = static_cast<VkSemaphore>(native->timeline);
	wait.value     = frame;
	wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	auto submit                   = VkSubmitInfo2();
	submit.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount = 1;
	submit.pWaitSemaphoreInfos    = &wait;
	{
		const auto lock = std::scoped_lock(*native->submitLock);
		REQUIRE(
			vkQueueSubmit2(static_cast<VkQueue>(native->queue), 1, &submit, fence) == VK_SUCCESS);
	}
	REQUIRE(
		vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) ==
		VK_SUCCESS);
	vkDestroyFence(vkDevice, fence, nullptr);

	// The batch could only run once the timeline reached the frame's value, which is the fence the
	// RHI reports: what it cannot prove is that a present behind it shows the frame.
	CHECK(queue->IsFenceComplete(frame));

	rm->DestroyBuffer(dst, false);
	rm->DestroyBuffer(src, false);
	rm->UnregisterQueue(queue.Get());
	queue->Flush();
}
#endif
