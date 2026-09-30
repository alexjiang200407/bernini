#include <algorithm>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <catch2/catch_test_macros.hpp>
#include <core/platform/util.h>
#include <crowdlib/HashFillJob.h>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
	bgpu::GpuContextRef
	MakeContext(const std::filesystem::path& shaderCacheDir = "shadercache")
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		desc.shaderCacheDir   = shaderCacheDir;
		return bgpu::CreateGpuContext(desc);
	}

	// Not a multiple of the kernel's 64-wide groups, so the last group's bounds check is exercised.
	constexpr uint32_t c_Count = 1000;

	// The first element that differs from the CPU reference, or the count when none does.
	uint32_t
	FirstMismatch(std::span<const uint32_t> readback, uint32_t seed)
	{
		for (uint32_t i = 0; i < readback.size(); ++i)
		{
			if (readback[i] != crowd::HashFillReference(i, seed))
				return i;
		}
		return static_cast<uint32_t>(readback.size());
	}
}

TEST_CASE("A hash fill reads back what the CPU reference computes", "[render]")
{
	auto context = MakeContext();
	auto job     = crowd::CreateHashFillJob(context, { .count = c_Count });
	REQUIRE(job->GetCount() == c_Count);
	CHECK(job->GetSubmittedFence() == 0);

	const uint64_t first = job->Submit(7);
	CHECK(first == job->GetSubmittedFence());
	job->Wait();
	CHECK_FALSE(job->InFlight());
	CHECK(job->GetCompletedFence() >= first);

	REQUIRE(job->GetReadback().size() == c_Count);
	CHECK(FirstMismatch(job->GetReadback(), 7) == c_Count);

	// A second submission replaces the result rather than leaving the first one behind.
	const uint64_t second = job->Submit(0xC0FFEEu);
	CHECK(second > first);
	job->Wait();
	CHECK(FirstMismatch(job->GetReadback(), 0xC0FFEEu) == c_Count);
	CHECK(FirstMismatch(job->GetReadback(), 7) == 0);
}

// The shape the crowd simulation is built on: one context, a renderer drawing on its queue and the
// job computing on its own, each polled and neither waiting on the other.
TEST_CASE("A hash fill runs on its own queue beside a renderer on the same context", "[render]")
{
	auto context = MakeContext();

	auto gfx = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	REQUIRE(gfx != nullptr);

	auto job = crowd::CreateHashFillJob(context, { .count = c_Count });

	constexpr uint32_t c_Size = 64;

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = c_Size;
	targetDesc.height   = c_Size;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);
	view->CreateStaticMeshInstance(scene->AddCubeGeom(), glm::mat4(1.0f));

	auto camera = bgl::Camera();
	camera.LookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);

	auto renderJob     = bgl::RenderJob();
	renderJob.view     = view;
	renderJob.camera   = camera;
	renderJob.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));

	constexpr uint32_t c_Frames = 8;

	uint32_t seed      = 1;
	uint32_t completed = 0;
	job->Submit(seed);
	for (uint32_t frame = 0; frame < c_Frames; ++frame)
	{
		gfx->DrawFrame(target, renderJob);

		if (!job->InFlight())
		{
			CHECK(FirstMismatch(job->GetReadback(), seed) == c_Count);
			++completed;
			job->Submit(++seed);
		}
	}

	job->Wait();
	gfx->WaitIdle();
	CHECK(FirstMismatch(job->GetReadback(), seed) == c_Count);
	CHECK(job->GetCompletedFence() == completed + 1u);
}

// One context is live per process, so a new one can be made only once the job has let go of its
// reference -- which it does after draining its queue.
TEST_CASE("A hash fill releases its context when it is destroyed mid-flight", "[render]")
{
	auto context = MakeContext();
	auto job     = crowd::CreateHashFillJob(context, { .count = c_Count });
	job->Submit(3);

	job     = nullptr;
	context = nullptr;

	auto next = MakeContext();
	CHECK(next != nullptr);
}

// Caller misuse is an error the caller can catch, never the end of the process.
TEST_CASE("A hash fill refuses misuse by throwing", "[render]")
{
	auto context = MakeContext();

	CHECK_THROWS_AS(crowd::CreateHashFillJob(nullptr), std::runtime_error);
	CHECK_THROWS_AS(crowd::CreateHashFillJob(context, { .count = 0 }), std::runtime_error);

	auto job = crowd::CreateHashFillJob(context, { .count = c_Count });
	CHECK_THROWS_AS(job->GetReadback(), std::runtime_error);

	job->Submit(1);
	if (job->InFlight())
	{
		CHECK_THROWS_AS(job->Submit(2), std::runtime_error);
		CHECK_THROWS_AS(job->GetReadback(), std::runtime_error);
	}

	// The refused calls left the first submission's result intact.
	job->Wait();
	CHECK(FirstMismatch(job->GetReadback(), 1) == c_Count);
}

// The kernel compiles once and is loaded after, on a context of its own each run as a process would
// have. A warm run rewriting no entry is the proof it never reached Slang.
TEST_CASE(
	"A hash fill's kernel is compiled once and loaded from the program cache after",
	"[render][shadercache]")
{
	namespace fs = std::filesystem;

	const fs::path dir =
		fs::temp_directory_path() / std::format("bernini_crowd_cache_{}", core::process_id());

	std::error_code ec;
	fs::remove_all(dir, ec);

	auto entries = [&] {
		std::vector<fs::path> files;
		for (const auto& entry : fs::directory_iterator(dir))
		{
			if (entry.path().extension() == ".bsc")
				files.push_back(entry.path());
		}
		std::ranges::sort(files);
		return files;
	};

	auto mismatchAfterFill = [&](uint32_t seed) {
		auto context = MakeContext(dir);
		auto job     = crowd::CreateHashFillJob(context, { .count = c_Count });
		job->Submit(seed);
		job->Wait();
		return FirstMismatch(job->GetReadback(), seed);
	};

	CHECK(mismatchAfterFill(7) == c_Count);
	const std::vector<fs::path> cold = entries();
	REQUIRE(cold.size() == 1);
	const auto coldTime = fs::last_write_time(cold.front());

	CHECK(mismatchAfterFill(11) == c_Count);
	CHECK(entries() == cold);
	CHECK((fs::last_write_time(cold.front()) == coldTime));

	{
		std::ofstream(cold.front(), std::ios::binary | std::ios::trunc) << "garbage";
	}
	CHECK(mismatchAfterFill(13) == c_Count);
	CHECK(entries() == cold);
	CHECK(fs::file_size(cold.front()) > std::string_view("garbage").size());

	fs::remove_all(dir, ec);
}
