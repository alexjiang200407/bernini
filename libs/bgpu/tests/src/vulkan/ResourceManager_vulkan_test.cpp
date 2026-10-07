// The Vulkan resource manager on its own, before anything submits work: what it hands out, when it
// takes it back, and what keeps an imported buffer's memory alive. What a buffer holds once work is
// submitted is the RHI-level cases' to prove.
//
// bgpu_tests globs every .cpp under tests/ whatever the backend, so a Vulkan-only case has to exclude
// itself: the headers below do not exist on any other build.
#if defined(RENDERER_BACKEND_VULKAN)

#	include "native_device_vulkan.h"
#	include "resource/BufferMemory_vulkan.h"
#	include "resource/ImageMemory_vulkan.h"
#	include "resource/ResourceManager_vulkan.h"
#	include "volk_vulkan.h"

#	include <bgpu/GpuContext.h>
#	include <bgpu/cmd/CommandList.h>
#	include <bgpu/cmd/CommandQueue.h>
#	include <bgpu/resource/Buffer.h>
#	include <bgpu/resource/NativeBufferDesc.h>
#	include <bgpu/resource/NativeTextureDesc.h>
#	include <bgpu/resource/Texture.h>
#	include <bgpu/types/Format.h>
#	include <bgpu/resource/Readback.h>
#	include <bgpu/resource/ResourceManager.h>
#	include <bgpu/types/NativeObject.h>
#	include <catch2/catch_test_macros.hpp>
#	include <core/ref/RefCounter.h>
#	include <core/ref/SharedRef.h>
#	include <cstdint>
#	include <set>

namespace
{
	// A timeline the case moves by hand: what a deferred destroy is gated on.
	class FakeQueue final : public core::RefCounter<bgpu::ICommandQueue>
	{
	public:
		uint64_t next      = 1;
		uint64_t completed = 0;

		uint64_t
		ExecuteCommandList(bgpu::ICommandList* commandList) noexcept override
		{
			(void)commandList;
			return next++;
		}

		bool
		IsFenceComplete(uint64_t fenceValue) noexcept override
		{
			return fenceValue <= completed;
		}

		uint64_t
		PollCurrentFenceValue() noexcept override
		{
			return completed;
		}

		uint64_t
		GetLastCompletedFence() const noexcept override
		{
			return completed;
		}

		uint64_t
		GetNextFenceValue() const noexcept override
		{
			return next;
		}

		void
		InsertWait(uint64_t fenceValue) noexcept override
		{
			(void)fenceValue;
		}

		void
		InsertWaitForQueueFence(bgpu::ICommandQueue* cq, uint64_t fenceValue)
			const noexcept override
		{
			(void)cq;
			(void)fenceValue;
		}

		void
		InsertWaitForQueue(bgpu::ICommandQueue* otherQueue) const noexcept override
		{
			(void)otherQueue;
		}

		void
		WaitForFenceCPUBlocking(uint64_t fenceValue) noexcept override
		{
			(void)fenceValue;
		}

		void
		Flush() noexcept override
		{}

		double
		GetTimestampFrequency() const noexcept override
		{
			return 0.0;
		}
	};

	bgpu::GpuContextRef
	DebugContext()
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		desc.strictError      = true;
		return bgpu::CreateGpuContext(desc);
	}

	bgpu::StructBufferDesc
	Words(const uint32_t count, const char* name)
	{
		return bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(count).SetDebugName(
			name);
	}
}

TEST_CASE(
	"A Vulkan buffer and each view of it take a bindless index of their own",
	"[vulkan][descriptor]")
{
	auto context = DebugContext();
	auto rm      = core::SharedRef<bgpu::ResourceManager>::Make(
		context,
		bgpu::ResourceManagerDesc::ComputeOnly());

	const auto buffer = rm->CreateStructBuffer(Words(16, "indexed").SetAllowsUav());
	const auto raw = rm->CreateRawBuffer(bgpu::RawViewDesc().SetByteSize(64).SetDebugName("raw"));
	const auto srv =
		rm->CreateBufferSrv(raw, bgpu::BufferSrvDesc().SetElement<uint32_t>().SetDebugName("srv"));
	const auto uav = rm->CreateBufferUav(
		buffer,
		bgpu::BufferUavDesc().SetElement<uint32_t>().SetDebugName("uav"));
	REQUIRE(rm->ValidBufferHandle(buffer));
	REQUIRE(rm->ValidBufferHandle(raw));
	REQUIRE(rm->ValidBufferSrvHandle(srv));
	REQUIRE(rm->ValidBufferUavHandle(uav));

	const auto indices =
		std::set{ buffer.bindlessIndex, raw.bindlessIndex, srv.bindlessIndex, uav.bindlessIndex };
	CHECK(indices.size() == 4);
	CHECK_FALSE(indices.contains(0U));

	rm->DestroyBufferSrv(srv, false);
	rm->DestroyBufferUav(uav, false);
	rm->DestroyBuffer(raw, false);
	rm->DestroyBuffer(buffer, false);
}

TEST_CASE(
	"A deferred Vulkan destroy keeps its index until every registered queue passes",
	"[vulkan][descriptor]")
{
	auto context = DebugContext();
	auto rm      = core::SharedRef<bgpu::ResourceManager>::Make(
		context,
		bgpu::ResourceManagerDesc::ComputeOnly());
	auto queue = core::SharedRef<FakeQueue>::Make();
	rm->RegisterQueue(queue.Get());

	const auto first = rm->CreateStructBuffer(Words(4, "first"));
	REQUIRE(rm->ValidBufferHandle(first));
	rm->DestroyBuffer(first);
	CHECK_FALSE(rm->ValidBufferHandle(first));

	// Gated on the queue's next fence: until it completes, the index is not handed out again.
	rm->CleanupExpiredResources();
	const auto second = rm->CreateStructBuffer(Words(4, "second"));
	CHECK(second.bindlessIndex != first.bindlessIndex);

	queue->completed = queue->next;
	rm->CleanupExpiredResources();
	const auto third = rm->CreateStructBuffer(Words(4, "third"));
	CHECK(third.bindlessIndex == first.bindlessIndex);

	rm->DestroyBuffer(second, false);
	rm->DestroyBuffer(third, false);
	rm->UnregisterQueue(queue.Get());
}

TEST_CASE(
	"A Vulkan pool of buffers or views refuses a create past its size",
	"[vulkan][descriptor]")
{
	auto context       = DebugContext();
	auto desc          = bgpu::ResourceManagerDesc::ComputeOnly();
	desc.maxBuffers    = 2;
	desc.maxBufferSrvs = 1;
	desc.maxBufferUavs = 0;
	desc.maxCbvSrvUavs = desc.maxBuffers + desc.maxBufferSrvs + 1;
	auto rm            = core::SharedRef<bgpu::ResourceManager>::Make(context, desc);

	const auto a = rm->CreateStructBuffer(Words(4, "a").SetAllowsUav());
	const auto b = rm->CreateStructBuffer(Words(4, "b"));
	CHECK(rm->CreateStructBuffer(Words(4, "c")).IsNull());

	const auto view = rm->CreateBufferSrv(a, bgpu::BufferSrvDesc().SetElement<uint32_t>());
	CHECK_FALSE(view.IsNull());
	CHECK(rm->CreateBufferSrv(b, bgpu::BufferSrvDesc().SetElement<uint32_t>()).IsNull());
	CHECK(rm->CreateBufferUav(a, bgpu::BufferUavDesc().SetElement<uint32_t>()).IsNull());

	rm->DestroyBufferSrv(view, false);
	rm->DestroyBuffer(a, false);
	rm->DestroyBuffer(b, false);
}

// Vulkan counts no references to a buffer, so the manager does: an import holds the memory as the
// producer's handle does, and the memory goes when both have let go.
TEST_CASE("An imported Vulkan buffer outlives its producer's release", "[vulkan][import]")
{
	auto context  = DebugContext();
	auto producer = core::SharedRef<bgpu::ResourceManager>::Make(
		context,
		bgpu::ResourceManagerDesc::ComputeOnly());
	auto consumer = core::SharedRef<bgpu::ResourceManager>::Make(
		context,
		bgpu::ResourceManagerDesc::ComputeOnly());

	const auto made     = producer->CreateStructBuffer(Words(8, "produced"));
	const auto exported = producer->GetNativeBuffer(made, bgpu::NativeObjectType::kVkBuffer);
	REQUIRE(exported);
	CHECK_FALSE(producer->GetNativeBuffer(made, bgpu::NativeObjectType::kD3D12Resource));

	const auto imported = consumer->ImportNativeBuffer(
		bgpu::NativeBufferDesc()
			.SetObject(bgpu::NativeObjectType::kVkBuffer, exported)
			.SetBuffer(Words(8, "imported")));
	REQUIRE(consumer->ValidBufferHandle(imported));
	CHECK(
		consumer->GetNativeBuffer(imported, bgpu::NativeObjectType::kVkBuffer).pointer ==
		exported.pointer);

	auto* const buffer = exported.As<VkBuffer_T>();
	producer->DestroyBuffer(made, false);
	CHECK(bgpu::BufferMemory::Find(buffer) != nullptr);

	consumer->DestroyBuffer(imported, false);
	CHECK(bgpu::BufferMemory::Find(buffer) == nullptr);
}

TEST_CASE("A Vulkan import of a buffer no manager made is refused", "[vulkan][import]")
{
	auto context = DebugContext();
	auto rm      = core::SharedRef<bgpu::ResourceManager>::Make(
		context,
		bgpu::ResourceManagerDesc::ComputeOnly());

	auto info        = VkBufferCreateInfo();
	info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size        = 64;
	info.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	const VkDevice device  = bgpu::GetVulkanHandles(*context).device;
	VkBuffer       foreign = VK_NULL_HANDLE;
	REQUIRE(vkCreateBuffer(device, &info, nullptr, &foreign) == VK_SUCCESS);

	CHECK(rm->ImportNativeBuffer(
				bgpu::NativeBufferDesc()
					.SetObject(bgpu::NativeObjectType::kVkBuffer, bgpu::NativeObject{ foreign })
					.SetBuffer(Words(16, "foreign")))
	          .IsNull());

	vkDestroyBuffer(device, foreign, nullptr);
}

namespace
{
	bgpu::TextureDesc
	SmallTexture(const char* debugName)
	{
		auto desc      = bgpu::TextureDesc();
		desc.width     = 4;
		desc.height    = 4;
		desc.format    = bgpu::Format::RGBA8_UNORM;
		desc.debugName = debugName;
		return desc;
	}
}

// An image is held as a buffer is: an import of one a manager made keeps it alive past its maker's
// release, and the image goes when both have let go.
TEST_CASE("An imported Vulkan texture outlives its producer's release", "[vulkan][import]")
{
	auto context = DebugContext();
	auto producer =
		core::SharedRef<bgpu::ResourceManager>::Make(context, bgpu::ResourceManagerDesc());
	auto consumer =
		core::SharedRef<bgpu::ResourceManager>::Make(context, bgpu::ResourceManagerDesc());

	const auto made     = producer->CreateTexture(SmallTexture("produced"));
	const auto exported = producer->GetNativeTexture(made, bgpu::NativeObjectType::kVkImage);
	REQUIRE(exported);
	CHECK_FALSE(producer->GetNativeTexture(made, bgpu::NativeObjectType::kD3D12Resource));

	const auto imported = consumer->ImportNativeTexture(
		bgpu::NativeTextureDesc()
			.SetObject(bgpu::NativeObjectType::kVkImage, exported)
			.SetTexture(SmallTexture("imported")));
	REQUIRE(consumer->ValidTextureHandle(imported));
	CHECK(
		consumer->GetNativeTexture(imported, bgpu::NativeObjectType::kVkImage).pointer ==
		exported.pointer);

	auto* const image = exported.As<VkImage_T>();
	producer->DestroyTexture(made, false);
	CHECK(bgpu::ImageMemory::Find(image) != nullptr);

	consumer->DestroyTexture(imported, false);
	CHECK(bgpu::ImageMemory::Find(image) == nullptr);
}

// A swapchain's images are the swapchain's: the manager views one and never destroys it.
TEST_CASE("A Vulkan image no manager made is borrowed, not destroyed", "[vulkan][import]")
{
	auto context = DebugContext();
	auto rm = core::SharedRef<bgpu::ResourceManager>::Make(context, bgpu::ResourceManagerDesc());

	auto info          = VkImageCreateInfo();
	info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType     = VK_IMAGE_TYPE_2D;
	info.format        = VK_FORMAT_R8G8B8A8_UNORM;
	info.extent        = VkExtent3D{ 4, 4, 1 };
	info.mipLevels     = 1;
	info.arrayLayers   = 1;
	info.samples       = VK_SAMPLE_COUNT_1_BIT;
	info.tiling        = VK_IMAGE_TILING_OPTIMAL;
	info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
	info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	const VkDevice device  = bgpu::GetVulkanHandles(*context).device;
	VkImage        foreign = VK_NULL_HANDLE;
	REQUIRE(vkCreateImage(device, &info, nullptr, &foreign) == VK_SUCCESS);

	const auto borrowed = rm->ImportNativeTexture(
		bgpu::NativeTextureDesc()
			.SetObject(bgpu::NativeObjectType::kVkImage, bgpu::NativeObject{ foreign })
			.SetTexture(SmallTexture("foreign")));
	REQUIRE(rm->ValidTextureHandle(borrowed));
	CHECK(rm->GetNativeTexture(borrowed, bgpu::NativeObjectType::kVkImage).pointer == foreign);
	CHECK(bgpu::ImageMemory::Find(foreign) == nullptr);

	// The layer would report a second destroy of the image, or a leak of it, as the context dies.
	rm->DestroyTexture(borrowed, false);
	vkDestroyImage(device, foreign, nullptr);
}

TEST_CASE("A Vulkan readback buffer is mapped for the CPU", "[vulkan][readback]")
{
	auto context = DebugContext();
	auto rm      = core::SharedRef<bgpu::ResourceManager>::Make(
		context,
		bgpu::ResourceManagerDesc::ComputeOnly());

	auto desc      = bgpu::ReadbackBufferDesc();
	desc.byteSize  = 256;
	desc.debugName = "mapped";
	const auto rb  = rm->CreateReadbackBuffer(desc);
	REQUIRE(rm->ValidReadbackBufferHandle(rb));
	CHECK(rm->GetReadbackBuffer(rb).GetByteSize() == 256);
	CHECK(rm->MapReadback(rb) != nullptr);
	rm->UnmapReadback(rb);
	rm->DestroyReadbackBuffer(rb, false);
	CHECK_FALSE(rm->ValidReadbackBufferHandle(rb));
}

#endif
