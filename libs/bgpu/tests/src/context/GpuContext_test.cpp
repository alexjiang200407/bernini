#include <bgpu/GpuContext.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <slang.h>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	bgpu::GpuContextDesc
	DebugDesc()
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		return desc;
	}

	// A module of bgpu's own tree, staged beside the suite by bgpu_copy_shaders, spelled as an import
	// spells it. It imports others, so a load resolves more than one file.
	constexpr const char* c_Module = "lib.types.RawBuffer";
}

// The premise of a shared context: the application creates one and hands it to every owner, and
// D3D12 hands back one device per adapter, so a second live context could never be independent of
// the first. The rule is portable: refused on Metal too, where two would work.
TEST_CASE("One GPU context is live per process, and another may follow it", "[render][device]")
{
	auto first = bgpu::CreateGpuContext(DebugDesc());
	REQUIRE(first != nullptr);

	CHECK_THROWS_AS(bgpu::CreateGpuContext(DebugDesc()), std::runtime_error);

	first = nullptr;

	// The successor compiles: the sessions it creates are its own.
	auto second = bgpu::CreateGpuContext(DebugDesc());
	REQUIRE(second != nullptr);
	CHECK(second->LoadModule(c_Module) != nullptr);
}

TEST_CASE("The search paths are the staged tree, then the client's directory", "[device]")
{
	SECTION("no client directory")
	{
		auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
		REQUIRE(context->GetShaderSearchPaths().size() == 2);
		CHECK(context->GetShaderSearchPaths().front() == "./shaders/src");
		CHECK(context->GetShaderSearchPaths().back() == "./shaders/tests");
	}

	SECTION("a client directory comes last, so the engine's modules cannot be shadowed by a file")
	{
		auto desc            = bgpu::GpuContextDesc();
		desc.clientShaderDir = "client_shaders";
		auto context         = bgpu::CreateGpuContext(desc);
		REQUIRE(context->GetShaderSearchPaths().size() == 3);
		CHECK(context->GetShaderSearchPaths().back() == "client_shaders");
	}
}

// One owner builds its pipelines and drops the sessions to reclaim their memory; the next owner's
// compile, or the same owner's next batch, must find a working compiler again.
TEST_CASE("A released session is recreated by the next load", "[render][device]")
{
	auto context = bgpu::CreateGpuContext(DebugDesc());

	REQUIRE(context->LoadModule(c_Module) != nullptr);
	context->ReleaseSlangSessions();
	CHECK(context->LoadModule(c_Module) != nullptr);
}

// Sessions are per thread, and a source module is part of the description every session is created
// from -- so a thread that has never compiled sees a module registered before it started.
TEST_CASE("A source module is loaded into every thread's session", "[render][device]")
{
	auto context = bgpu::CreateGpuContext(DebugDesc());

	context->AddSourceModule(
		{ .name = "probe.Shadowed", .source = "public float Shadowed() { return 1.0f; }" });

	CHECK(context->LoadModule("probe.Shadowed") != nullptr);

	slang::IModule* onOtherThread = nullptr;
	std::thread([&] { onOtherThread = context->LoadModule("probe.Shadowed"); }).join();
	CHECK(onOtherThread != nullptr);
}

TEST_CASE("GPU validation asked for is GPU validation active", "[render][device]")
{
	auto desc                     = DebugDesc();
	desc.enableGPUValidationLayer = true;

	auto context = bgpu::CreateGpuContext(desc);
	CHECK(context->GpuValidationActive());
	CHECK(context->GetDesc().enableGPUValidationLayer);
}

// What an owner's shader cache keys on: the fold moves with a new or changed text, and only then.
TEST_CASE("The source salt follows the registered texts", "[render][device]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());

	const uint64_t bare = context->GetSourceSalt();

	context->AddSourceModule({ .name = "probe.A", .source = "public float A() { return 1.0f; }" });
	const uint64_t withA = context->GetSourceSalt();
	CHECK(withA != bare);

	context->AddSourceModule({ .name = "probe.A", .source = "public float A() { return 1.0f; }" });
	CHECK(context->GetSourceSalt() == withA);

	context->AddSourceModule({ .name = "probe.A", .source = "public float A() { return 2.0f; }" });
	const uint64_t withA2 = context->GetSourceSalt();
	CHECK(withA2 != withA);
	CHECK(withA2 != bare);

	// Order-independent: B then A folds to the same salt as A then B.
	context->AddSourceModule({ .name = "probe.B", .source = "public float B() { return 1.0f; }" });
	const uint64_t withAB = context->GetSourceSalt();

	context    = nullptr;
	auto other = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	other->AddSourceModule({ .name = "probe.B", .source = "public float B() { return 1.0f; }" });
	other->AddSourceModule({ .name = "probe.A", .source = "public float A() { return 2.0f; }" });
	CHECK(other->GetSourceSalt() == withAB);
}
