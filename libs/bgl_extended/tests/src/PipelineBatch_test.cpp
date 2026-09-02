#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/MeshletKernel.h"
#include "pipeline/PipelineBatch.h"
#include "util/GpuValidation.h"
#include <bgl/IGraphics.h>

namespace
{
	namespace fs = std::filesystem;

	// Every program cache entry in `dir`, by name, with its bytes.
	std::map<std::string, std::vector<std::byte>>
	ProgramCacheEntries(const fs::path& dir)
	{
		std::map<std::string, std::vector<std::byte>> entries;
		for (const auto& entry : fs::directory_iterator(dir))
		{
			if (entry.path().extension() != ".bsc")
				continue;

			std::ifstream          in(entry.path(), std::ios::binary);
			std::vector<std::byte> bytes(static_cast<size_t>(entry.file_size()));
			in.read(
				reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			entries.emplace(entry.path().filename().string(), std::move(bytes));
		}
		return entries;
	}

	// Brings a device up against an empty `cacheDir` (which builds the renderer's own pipelines
	// into it) and then batch-builds the test kernels on `threads` threads.
	void
	BuildTestKernels(const fs::path& cacheDir, uint32_t threads)
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = cacheDir.string();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto* device = gfx->As<bgl::GraphicsBase>()->GetDevice();

		std::array<bgl::ComputeKernel, 4> compute;
		std::array<bgl::MeshletKernel, 2> meshlet;

		auto batch = bgl::PipelineBatch(device);

		const char* const computeModules[] = { "CSComputeBufferTest",
			                                   "CSRawLoad",
			                                   "CSRawStore",
			                                   "CSTypedViewRead" };
		for (size_t i = 0; i < compute.size(); ++i)
		{
			batch.Add(
				compute[i],
				bgl::ComputePipelineDesc()
					.SetShader(device->CreateShader(computeModules[i]))
					.SetDebugName(computeModules[i]));
		}

		const char* const meshletModules[] = { "MeshUniformTest", "MeshTwoCbufferTest" };
		for (size_t i = 0; i < meshlet.size(); ++i)
		{
			batch.Add(
				meshlet[i],
				bgl::MeshletPipelineDesc()
					.SetMeshShader(device->CreateShader(meshletModules[i], "MSMain"))
					.SetPixelShader(device->CreateShader(meshletModules[i], "PSMain"))
					.AddRtvFormat(bgl::Format::RGBA32_FLOAT));
		}

		REQUIRE(batch.Pending() == compute.size() + meshlet.size());
		batch.Build(threads);
		CHECK(batch.Pending() == 0);

		for (const auto& kernel : compute) CHECK(kernel.pipeline != nullptr);
		for (const auto& kernel : meshlet)
		{
			CHECK(kernel.pipeline != nullptr);
			CHECK_FALSE(kernel.uniforms.empty());
		}
	}
}

// The batch is only a faster way of doing what one thread did: a cold cache written by many
// workers must hold the same entries, byte for byte, as one written by a single worker. The
// renderer's own pipelines are in both directories too, built by CreateGraphics on its own batch.
TEST_CASE(
	"A cold shader cache built on many threads matches one built on a single thread",
	"[shadercache]")
{
	const fs::path serialDir   = fs::temp_directory_path() / "bernini_pipelinebatch_serial";
	const fs::path parallelDir = fs::temp_directory_path() / "bernini_pipelinebatch_parallel";

	std::error_code ec;
	fs::remove_all(serialDir, ec);
	fs::remove_all(parallelDir, ec);

	BuildTestKernels(serialDir, 1);
	BuildTestKernels(parallelDir, 0);

	const auto serial   = ProgramCacheEntries(serialDir);
	const auto parallel = ProgramCacheEntries(parallelDir);

	REQUIRE_FALSE(serial.empty());
	CHECK(serial.size() == parallel.size());
	for (const auto& [name, bytes] : serial)
	{
		CAPTURE(name);
		auto found = parallel.find(name);
		REQUIRE(found != parallel.end());
		CHECK(found->second == bytes);
	}

	fs::remove_all(serialDir, ec);
	fs::remove_all(parallelDir, ec);
}
