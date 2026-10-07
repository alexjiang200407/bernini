// The Vulkan compute pipeline on its own, before a command list can dispatch it: what its constant
// buffers reflect as, and that a program loaded from the cache builds the same pipeline.
//
// bgpu_tests globs every .cpp under tests/ whatever the backend, so a Vulkan-only case has to exclude
// itself: the headers below do not exist on any other build.
#if defined(RENDERER_BACKEND_VULKAN)

#	include "pipeline/ComputePipeline_vulkan.h"
#	include "shadercache/ShaderCache_vulkan.h"

#	include <algorithm>
#	include <bgpu/GpuContext.h>
#	include <bgpu/pipeline/ComputePipeline.h>
#	include <bgpu/reflection/ReflectedLayout.h>
#	include <bgpu/resource/Shader.h>
#	include <bgpu/uniforms/UniformLayoutEntry.h>
#	include <catch2/catch_test_macros.hpp>
#	include <core/ref/SharedRef.h>
#	include <cstdint>
#	include <filesystem>
#	include <string_view>
#	include <system_error>

namespace
{
	bgpu::GpuContextRef
	DebugContext(const std::filesystem::path& cacheDir = {})
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		desc.strictError      = true;
		desc.shaderCacheDir   = cacheDir;
		return bgpu::CreateGpuContext(desc);
	}

	bgpu::ComputePipelineDesc
	EntrySquare(const bgpu::GpuContextRef& context)
	{
		auto shader            = bgpu::ShaderDesc();
		shader.slangModuleName = "bgpu.CSEntrySquare";
		shader.entryPointName  = "main";
		shader.debugName       = "bgpu.CSEntrySquare:main";
		return bgpu::ComputePipelineDesc()
		    .SetShader(core::SharedRef<bgpu::Shader>::Make(shader, context))
		    .SetDebugName("bgpu.CSEntrySquare");
	}

	[[nodiscard]] uint32_t
	OffsetOf(const bgpu::UniformLayoutEntry& entry, const std::string_view field)
	{
		const auto found =
			std::ranges::find(entry.layout->fields, field, &bgpu::ReflectedField::name);
		REQUIRE(found != entry.layout->fields.end());
		return found->offset;
	}
}

// The C++ mirrors bgpu generates are D3D12's layout on every backend but Metal, so SPIR-V has to pack
// as FXC does. Under std140, Vulkan's default for a constant buffer, the struct after `values` would
// start a new 16-byte row.
TEST_CASE("A Vulkan kernel's constant buffer is packed as D3D12 packs it", "[vulkan][compute]")
{
	auto context = DebugContext();
	auto pipeline =
		core::SharedRef<bgpu::ComputePipeline>::Make(context, nullptr, EntrySquare(context));

	const bgpu::UniformLayoutEntry uniforms = pipeline->GetUniformLayoutEntry("gUniforms");
	CHECK(OffsetOf(uniforms, "values") == 0);
	CHECK(OffsetOf(uniforms, "outBuffer") == 8);
	CHECK(pipeline->GetVkPipeline() != VK_NULL_HANDLE);
	CHECK(pipeline->GetConstantBufferBindings().size() == pipeline->GetUniformBufferNames().size());
}

TEST_CASE("A Vulkan pipeline is built from its cached program", "[vulkan][shadercache]")
{
	const auto cacheDir = std::filesystem::temp_directory_path() / "bernini_vulkan_pipeline_cache";
	std::error_code ec;
	std::filesystem::remove_all(cacheDir, ec);

	uint32_t compiledOffset = 0;
	{
		auto context = DebugContext(cacheDir);
		auto cache   = bgpu::ShaderCache(context);
		auto pipeline =
			core::SharedRef<bgpu::ComputePipeline>::Make(context, &cache, EntrySquare(context));
		compiledOffset = OffsetOf(pipeline->GetUniformLayoutEntry("gUniforms"), "outBuffer");
	}

	// A context of its own, so the second build cannot reuse the first one's Slang session.
	auto context = DebugContext(cacheDir);
	auto cache   = bgpu::ShaderCache(context);

	auto program = bgpu::CachedProgram();
	REQUIRE(cache.TryLoad(cache.ComputeKey({ { "bgpu.CSEntrySquare", "main" } }), program));
	CHECK(program.entryPointSpirv.size() == 1);

	auto pipeline =
		core::SharedRef<bgpu::ComputePipeline>::Make(context, &cache, EntrySquare(context));
	CHECK(OffsetOf(pipeline->GetUniformLayoutEntry("gUniforms"), "outBuffer") == compiledOffset);
	CHECK(pipeline->GetVkPipeline() != VK_NULL_HANDLE);

	pipeline = nullptr;
	context  = nullptr;
	std::filesystem::remove_all(cacheDir, ec);
}

#endif
