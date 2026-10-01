// Held through SharedRef via `auto` and dereferenced: both need the complete type, which
// include-cleaner cannot see through the template.
#include <bgpu/GpuContext.h>
#include <bgpu/buffer/EntryBuffer.h>
#include <bgpu/buffer/RawBuffer.h>
#include <bgpu/buffer/UploadBuffer.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <utility>

// A buffer owns its storage from construction to destruction: there is no Init to forget and no
// Release to call. A destroy bumps the handle's generation whether or not the free is deferred, so
// ValidBufferHandle reads exactly what a destructor gave back.
namespace
{
	bgpu::ResourceManagerRef
	CreateResourceManager()
	{
		auto contextDesc             = bgpu::GpuContextDesc();
		contextDesc.enableDebugLayer = true;
		auto context                 = bgpu::CreateGpuContext(contextDesc);
		REQUIRE(context != nullptr);

		auto device = bgpu::CreateDevice(context);
		REQUIRE(device != nullptr);

		auto rm = device->CreateResourceManager(bgpu::ResourceManagerDesc());
		REQUIRE(rm != nullptr);
		return rm;
	}

	bgpu::UploadBuffer<uint32_t>
	MakeUploadBuffer(const bgpu::ResourceManagerRef& rm, const char* name)
	{
		return bgpu::UploadBuffer<uint32_t>(
			rm,
			bgpu::UploadBufferDesc().SetInitialCount(4).SetDebugName(name));
	}

	bgpu::RawBuffer<>
	MakeViewedArena(const bgpu::ResourceManagerRef& rm, const char* name)
	{
		return bgpu::RawBuffer<>(
			rm,
			bgpu::RawBufferDesc()
				.SetInitialBytes(256)
				.SetHandleStride(sizeof(uint32_t))
				.SetDebugName(name));
	}
}

TEST_CASE("A buffer frees its storage when it is destroyed", "[buffer][lifetime]")
{
	const auto rm = CreateResourceManager();

	auto storage = bgpu::BufferHandle();
	{
		const auto buffer = MakeUploadBuffer(rm, "Destroyed");
		storage           = buffer.GetBufferHandle();
		REQUIRE(rm->ValidBufferHandle(storage));
	}

	CHECK_FALSE(rm->ValidBufferHandle(storage));
}

TEST_CASE("A moved-from buffer frees nothing", "[buffer][lifetime]")
{
	const auto rm = CreateResourceManager();

	auto owner   = std::optional<bgpu::UploadBuffer<uint32_t>>();
	auto storage = bgpu::BufferHandle();
	{
		auto moved = MakeUploadBuffer(rm, "Moved");
		storage    = moved.GetBufferHandle();
		owner.emplace(std::move(moved));
	}

	CHECK(rm->ValidBufferHandle(storage));
	CHECK(owner->GetBufferHandle().bindlessIndex == storage.bindlessIndex);

	owner.reset();
	CHECK_FALSE(rm->ValidBufferHandle(storage));
}

TEST_CASE("Move-assigning a buffer frees the storage it replaces", "[buffer][lifetime]")
{
	const auto rm = CreateResourceManager();

	auto kept     = MakeUploadBuffer(rm, "Kept");
	auto replaced = MakeUploadBuffer(rm, "Replaced");

	const auto keptStorage     = kept.GetBufferHandle();
	const auto replacedStorage = replaced.GetBufferHandle();

	replaced = std::move(kept);

	CHECK_FALSE(rm->ValidBufferHandle(replacedStorage));
	CHECK(rm->ValidBufferHandle(keptStorage));
}

TEST_CASE(
	"A buffer destroyed before its growth is flushed frees both resources",
	"[buffer][lifetime]")
{
	const auto rm = CreateResourceManager();

	auto original = bgpu::BufferHandle();
	auto grown    = bgpu::BufferHandle();
	{
		auto buffer = bgpu::EntryBuffer<uint32_t>(
			rm,
			bgpu::EntryBufferDesc().SetInitialCount(1).SetDebugName("Grown"));
		original = buffer.GetBufferHandle();

		// Past the one element asked for, so the next add replaces the resource; no Update runs,
		// so the original is still waiting to be retired when the buffer dies.
		while (buffer.GetBufferHandle().bindlessIndex == original.bindlessIndex)
		{
			[[maybe_unused]] const auto slot = buffer.Add(7u);
		}
		grown = buffer.GetBufferHandle();
		REQUIRE(rm->ValidBufferHandle(original));
	}

	CHECK_FALSE(rm->ValidBufferHandle(original));
	CHECK_FALSE(rm->ValidBufferHandle(grown));
}

TEST_CASE("A raw arena frees its handle view with its storage", "[buffer][lifetime]")
{
	const auto rm = CreateResourceManager();

	auto storage = bgpu::BufferHandle();
	auto view    = bgpu::BufferSrvHandle();
	{
		const auto arena = MakeViewedArena(rm, "Viewed");
		storage          = arena.GetBufferHandle();
		view             = arena.GetHandleView();
		REQUIRE(rm->ValidBufferSrvHandle(view));
	}

	CHECK_FALSE(rm->ValidBufferHandle(storage));
	CHECK_FALSE(rm->ValidBufferSrvHandle(view));
}

TEST_CASE("A moved-from raw arena frees neither its storage nor its view", "[buffer][lifetime]")
{
	const auto rm = CreateResourceManager();

	auto owner   = std::optional<bgpu::RawBuffer<>>();
	auto storage = bgpu::BufferHandle();
	auto view    = bgpu::BufferSrvHandle();
	{
		auto moved = MakeViewedArena(rm, "Moved arena");
		storage    = moved.GetBufferHandle();
		view       = moved.GetHandleView();
		owner.emplace(std::move(moved));
	}

	CHECK(rm->ValidBufferHandle(storage));
	CHECK(rm->ValidBufferSrvHandle(view));

	owner.reset();
	CHECK_FALSE(rm->ValidBufferHandle(storage));
	CHECK_FALSE(rm->ValidBufferSrvHandle(view));
}

TEST_CASE("Move-assigning a raw arena frees the storage and view it replaces", "[buffer][lifetime]")
{
	const auto rm = CreateResourceManager();

	auto kept     = MakeViewedArena(rm, "Kept arena");
	auto replaced = MakeViewedArena(rm, "Replaced arena");

	const auto keptStorage     = kept.GetBufferHandle();
	const auto keptView        = kept.GetHandleView();
	const auto replacedStorage = replaced.GetBufferHandle();
	const auto replacedView    = replaced.GetHandleView();

	replaced = std::move(kept);

	CHECK_FALSE(rm->ValidBufferHandle(replacedStorage));
	CHECK_FALSE(rm->ValidBufferSrvHandle(replacedView));
	CHECK(rm->ValidBufferHandle(keptStorage));
	CHECK(rm->ValidBufferSrvHandle(keptView));
	CHECK(replaced.GetHandleView().bindlessIndex == keptView.bindlessIndex);
}
