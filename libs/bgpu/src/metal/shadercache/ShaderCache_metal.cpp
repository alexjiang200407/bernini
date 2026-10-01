#include "shadercache/ShaderCache_metal.h"
#include <bgpu/metal/MetalErrorChecker.h>

#include "convert_metal.h"
#include "pipeline/MetalPipelineReflection.h"
#include "shadercache/util.h"
#include <bgpu/reflection/ReflectedLayout.h>
#include <bgpu/types/ShaderStage.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

#include <core/file/file.h>
#include <core/platform/util.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <string>

#include <bgpu/GpuContext.h>

namespace bgpu
{
	using shader_cache::ReadLayout;
	using shader_cache::WriteLayout;

	namespace
	{
		// Bump formatVersion when the encoding below changes -- or when MetalizeLayout's rules do,
		// since the layout it computed is what CachedCbuffer stores. It is in every key, so an entry
		// in the old layout is missed rather than misread.
		// 3: a Mixed cbuffer's stage binding is its constant-buffer offset, not getBindingIndex();
		// an entry written before that carries a wrong index for the same sources.
		constexpr auto c_Owner = bgpu::ProgramCacheOwner{ .tag = "bgl_metal", .formatVersion = 3 };

		// Named as on D3D12: one file per backend holding whatever its driver calls a pipeline
		// library, so a cache directory reads the same whichever backend wrote it.
		constexpr const char* c_PipelineLibraryFile = "pipelines.psolib";

		using core::io::ByteReader;
		using core::io::ByteWriter;

		std::vector<std::byte>
		Serialize(const CachedProgram& program)
		{
			ByteWriter writer;

			writer.WritePod<uint32_t>(static_cast<uint32_t>(program.cbuffers.size()));
			for (const CachedCbuffer& cbuffer : program.cbuffers)
			{
				writer.WriteString(cbuffer.name);
				writer.WritePod<uint32_t>(cbuffer.size);
				WriteLayout(writer, cbuffer.layout);

				writer.WritePod<uint32_t>(static_cast<uint32_t>(cbuffer.handles.size()));
				for (const HandleSlot& handle : cbuffer.handles)
				{
					writer.WritePod<uint32_t>(handle.offset);
					writer.WritePod<uint32_t>(static_cast<uint32_t>(handle.kind));
				}
			}

			writer.WritePod<uint32_t>(static_cast<uint32_t>(program.stages.size()));
			for (const CachedStage& stage : program.stages)
			{
				writer.WritePod<uint32_t>(std::to_underlying(stage.stage));
				writer.WriteString(stage.entryPoint);
				writer.WriteString(stage.msl);

				writer.WritePod<uint32_t>(static_cast<uint32_t>(stage.bindings.size()));
				for (const auto& [name, index] : stage.bindings)
				{
					writer.WriteString(name);
					writer.WritePod<uint32_t>(index);
				}

				for (uint32_t axis : stage.threadsPerThreadgroup) writer.WritePod<uint32_t>(axis);
			}

			return writer.Take();
		}

		CachedProgram
		Deserialize(const std::vector<std::byte>& bytes)
		{
			ByteReader    reader(bytes);
			CachedProgram program;

			const uint32_t cbufferCount = reader.ReadPod<uint32_t>();
			program.cbuffers.reserve(cbufferCount);
			for (uint32_t i = 0; i < cbufferCount; ++i)
			{
				CachedCbuffer cbuffer;
				cbuffer.name   = reader.ReadString();
				cbuffer.size   = reader.ReadPod<uint32_t>();
				cbuffer.layout = ReadLayout(reader);

				const uint32_t handleCount = reader.ReadPod<uint32_t>();
				cbuffer.handles.reserve(handleCount);
				for (uint32_t h = 0; h < handleCount; ++h)
				{
					HandleSlot handle;
					handle.offset = reader.ReadPod<uint32_t>();
					handle.kind   = static_cast<HandleKind>(reader.ReadPod<uint32_t>());
					cbuffer.handles.push_back(handle);
				}

				program.cbuffers.push_back(std::move(cbuffer));
			}

			const uint32_t stageCount = reader.ReadPod<uint32_t>();
			program.stages.reserve(stageCount);
			for (uint32_t i = 0; i < stageCount; ++i)
			{
				CachedStage stage;
				stage.stage      = static_cast<ShaderStage>(reader.ReadPod<uint32_t>());
				stage.entryPoint = reader.ReadString();
				stage.msl        = reader.ReadString();

				const uint32_t bindingCount = reader.ReadPod<uint32_t>();
				stage.bindings.reserve(bindingCount);
				for (uint32_t b = 0; b < bindingCount; ++b)
				{
					std::string    name  = reader.ReadString();
					const uint32_t index = reader.ReadPod<uint32_t>();
					stage.bindings.push_back({ .name = std::move(name), .index = index });
				}

				for (uint32_t& axis : stage.threadsPerThreadgroup)
					axis = reader.ReadPod<uint32_t>();

				program.stages.push_back(std::move(stage));
			}

			return program;
		}

		NS::URL*
		FileUrl(const std::filesystem::path& path)
		{
			return NS::URL::fileURLWithPath(ConvertString(path.string()));
		}
	}

	ShaderCache::ShaderCache(
		bgpu::GpuContextRef context,
		MTL::Device*        device,
		bool                usePipelineLibrary) :
		m_Context(std::move(context)), m_Programs(*m_Context->GetProgramCache())
	{
		if (!usePipelineLibrary)
			return;

		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL::BinaryArchiveDescriptor> desc =
			NS::TransferPtr(MTL::BinaryArchiveDescriptor::alloc()->init());

		const std::filesystem::path libPath = m_Programs.GetDirectory() / c_PipelineLibraryFile;
		std::error_code             ec;
		if (std::filesystem::exists(libPath, ec))
			desc->setUrl(FileUrl(libPath));

		// An archive written by another GPU or a newer Metal is rejected here; fall back to an empty
		// one so those pipelines are recompiled and re-added rather than the cache being lost.
		NS::Error* error = nullptr;
		m_Archive        = NS::TransferPtr(device->newBinaryArchive(desc.get(), &error));
		if (!m_Archive && desc->url() != nullptr)
		{
			desc->setUrl(nullptr);
			error     = nullptr;
			m_Archive = NS::TransferPtr(device->newBinaryArchive(desc.get(), &error));
		}

		if (!m_Archive)
		{
			spdlog::warn(
				"Metal binary archive unavailable, driver pipelines will not be cached: {}",
				GetErrorDescription(error));
		}
	}

	ShaderCache::~ShaderCache()
	{
		if (!m_Archive || !m_ArchiveDirty)
			return;

		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		// An atomic replace, like core::file::write_atomic -- done by hand because serializeToURL owns
		// the write and refuses an existing file. The temp name carries the process id: several
		// processes may share one cache directory -- a sharded test run does -- and a fixed name
		// would let them serialize into each other's file.
		const std::filesystem::path libPath = m_Programs.GetDirectory() / c_PipelineLibraryFile;
		const std::filesystem::path tmp =
			std::format("{}.{}.tmp", libPath.string(), core::process_id());

		std::error_code ec;
		std::filesystem::remove(tmp, ec);

		NS::Error* error = nullptr;
		if (!m_Archive->serializeToURL(FileUrl(tmp), &error))
		{
			spdlog::warn(
				"Could not serialize the Metal binary archive: {}",
				GetErrorDescription(error));
			return;
		}

		std::filesystem::rename(tmp, libPath, ec);
		if (ec)
		{
			spdlog::warn("Could not commit {}: {}", libPath.string(), ec.message());
			std::filesystem::remove(tmp, ec);
		}
	}

	void
	ShaderCache::WithArchive(const std::function<void(MTL::BinaryArchive*)>& build)
	{
		if (!m_Archive)
		{
			build(nullptr);
			return;
		}

		const auto held = std::lock_guard(m_ArchiveMutex);
		build(m_Archive.get());
	}

	uint64_t
	ShaderCache::ComputeKey(std::vector<bgpu::ProgramEntryPoint> moduleEntries) const
	{
		return m_Programs.ComputeKey(c_Owner, std::move(moduleEntries));
	}

	bool
	ShaderCache::TryLoad(uint64_t key, CachedProgram& out) const
	{
		std::vector<std::byte> bytes;
		if (!m_Programs.TryLoadProgram(key, bytes))
			return false;

		try
		{
			out = Deserialize(bytes);
			return true;
		}
		catch (const std::exception& e)
		{
			spdlog::warn("Ignoring an undecodable shader cache entry {:016x}: {}", key, e.what());
			return false;
		}
	}

	void
	ShaderCache::Store(uint64_t key, const CachedProgram& program) const
	{
		m_Programs.StoreProgram(key, Serialize(program));
	}
}
