#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "slang/SlangSessions.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>

namespace
{
	void
	WriteText(const std::filesystem::path& path, std::string_view text)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << text;
	}

	// The probe program every case dispatches: one thread, one uint, whatever the module it imports
	// says. The import is the thing under test, so the program itself is written into the client
	// directory beside it rather than staged with the suite -- staged, the build-time DXIL check
	// would compile it against a module that only exists here.
	constexpr std::string_view c_ProbeProgram = R"(import ClientProbe;
import lib.types.ComputeBuffer;

struct Uniforms
{
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    gUniforms.outBuffer[0] = kClientValue;
}
)";

	std::string
	ProbeModule(uint32_t value)
	{
		return "public static const uint kClientValue = " + std::to_string(value) + "u;\n";
	}

	// Brings up a Graphics on `opts`, builds `program` as a compute kernel, dispatches it once and
	// returns the one uint it wrote. `configure` runs on the device before the kernel compiles.
	uint32_t
	ProbeValue(
		const bgl::GraphicsOptions&               opts,
		const std::string&                        program,
		const std::function<void(bgl::IDevice&)>& configure = {})
	{
		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto gfxBase = gfx->As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto resourceManager = gfxBase->GetResourceManagerCpy();
		auto device          = gfxBase->GetDevice();

		if (configure)
			configure(*device);

		auto cmdListDesc = bgl::CommandListDesc();
		cmdListDesc.type = bgl::QueueType::kGraphics;

		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

		auto bufDesc = bgl::ComputeBufferDesc();
		bufDesc.SetElement<uint32_t>().SetInitialCount(1).SetDebugName("Client Probe Out");
		auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);

		auto kernel = device->CreateComputeKernel(
			bgl::ComputePipelineDesc()
				.SetShader(device->CreateShader(program))
				.SetDebugName(program));
		kernel["gUniforms"]["outBuffer"] = outBuf;

		auto state   = bgl::ComputeState();
		state.kernel = &kernel;

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = sizeof(uint32_t);
		rbDesc.debugName = "Client Probe Readback";
		auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

		cmdList->Open(cmdQueue, cmdAllocator);
		cmdList->SetComputeState(state);
		cmdList->Dispatch(1, 1, 1);
		cmdList->Barrier(
			outBuf,
			bgl::BufferBarrierDesc()
				.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
				.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));
		cmdList->CopyBufferToReadback(rb, outBuf);
		cmdList->Close();

		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);
		const uint32_t value = mapped[0];
		resourceManager->UnmapReadback(rb);

		resourceManager->DestroyReadbackBuffer(rb, false);
		resourceManager->DestroyBuffer(outBuf, false);
		return value;
	}

	bgl::GraphicsOptions
	ProbeOptions()
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		return opts;
	}
}

// A client names a directory of its own modules and a program imports one by name, the way the
// engine's tree is imported. The second half is the cache: the directory's files are in the salt,
// so an edit to one is a miss rather than the stale program the first pass cached under the same
// key -- which is what the shared cache directory across the two passes is there to catch.
TEST_CASE(
	"A client's shader directory is a search path, and editing it misses the cache",
	"[slang][cache][compute]")
{
	const std::filesystem::path dir =
		std::filesystem::temp_directory_path() / "bernini_client_shaders";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	WriteText(dir / "CSClientProbe.slang", c_ProbeProgram);
	WriteText(dir / "ClientProbe.slang", ProbeModule(7));

	auto opts             = ProbeOptions();
	opts.surfaceShaderDir = dir;
	// The salt folds each file's path, and the temp directory is fresh per run, so a cache shared
	// with the suite would gain a generation no later run could hit. This one dies with the dir.
	opts.shaderCacheDir = (dir / "shadercache").string();

	CHECK(ProbeValue(opts, "CSClientProbe") == 7u);

	WriteText(dir / "ClientProbe.slang", ProbeModule(8));
	CHECK(ProbeValue(opts, "CSClientProbe") == 8u);
}

// The engine's programs import a slot's surface by a fixed dotted name; the tree ships a file under
// that name in a subdirectory, and a module loaded from text under it is what a registered surface
// substitutes. So an import must resolve to the text over the file, and the text must be in the
// salt: the middle pass reads 2 with the file still on disk saying 1, and the last pass, sharing
// the cache with the first, gets the first's program back rather than the middle's.
TEST_CASE(
	"A module loaded from source shadows the file of its name, and is in the salt",
	"[slang][cache][compute]")
{
	// A cache of this case's own: the third pass has to hit what the first wrote and nothing else,
	// and a suite-wide directory can carry an entry from a build whose loader behaved differently
	// under the same salt.
	const std::filesystem::path dir =
		std::filesystem::temp_directory_path() / "bernini_source_modules";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	auto opts           = ProbeOptions();
	opts.shaderCacheDir = (dir / "shadercache").string();

	CHECK(ProbeValue(opts, "CSSourceProbe") == 1u);

	CHECK(ProbeValue(opts, "CSSourceProbe", [](bgl::IDevice& device) {
			  device.AddSourceModule(
				  { "game.probe", "public static const uint kProbeValue = 2u;\n" });
		  }) == 2u);

	CHECK(ProbeValue(opts, "CSSourceProbe") == 1u);
}
