#include "shadercache/ShaderCache_metal.h"
#include "MetalErrorChecker.h"

#include "convert_metal.h"
#include <bgl_common/shadercache/util.h>

#include <core/file/file.h>
#include <core/platform/util.h>

namespace bgl
{
	using namespace shader_cache;

	namespace
	{
		// Bump when the on-disk format below changes -- or when MetalizeLayout's rules do, since the
		// layout it computed is what CachedCbuffer stores. Folded into every key so old files are
		// missed rather than misread.
		constexpr uint32_t c_CacheFormatVersion = 2;

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
				WriteString(writer, cbuffer.name);
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
				writer.WritePod<uint32_t>(static_cast<uint32_t>(stage.stage));
				WriteString(writer, stage.entryPoint);
				WriteString(writer, stage.msl);

				writer.WritePod<uint32_t>(static_cast<uint32_t>(stage.bindings.size()));
				for (const auto& [name, index] : stage.bindings)
				{
					WriteString(writer, name);
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
				cbuffer.name   = ReadString(reader);
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
				stage.entryPoint = ReadString(reader);
				stage.msl        = ReadString(reader);

				const uint32_t bindingCount = reader.ReadPod<uint32_t>();
				stage.bindings.reserve(bindingCount);
				for (uint32_t b = 0; b < bindingCount; ++b)
				{
					std::string    name  = ReadString(reader);
					const uint32_t index = reader.ReadPod<uint32_t>();
					stage.bindings.emplace_back(std::move(name), index);
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
		MTL::Device*                    device,
		std::filesystem::path           cacheDir,
		std::string_view                optionsSalt,
		const std::vector<std::string>& searchPaths,
		bool                            usePipelineLibrary) :
		m_CacheDir(std::move(cacheDir)),
		m_SourceSalt(ComputeSourceSalt(optionsSalt, searchPaths, c_CacheFormatVersion))
	{
		std::error_code ec;
		std::filesystem::create_directories(m_CacheDir, ec);

		if (!usePipelineLibrary)
			return;

		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::SharedPtr<MTL::BinaryArchiveDescriptor> desc =
			NS::TransferPtr(MTL::BinaryArchiveDescriptor::alloc()->init());

		const std::filesystem::path libPath = m_CacheDir / c_PipelineLibraryFile;
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
			logger::warn(
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

		// An atomic replace, like shader_cache::WriteFileAtomic -- done by hand because serializeToURL
		// owns the write and refuses an existing file. The temp name carries the process id: several
		// processes may share one cache directory -- a sharded test run does -- and a fixed name
		// would let them serialize into each other's file.
		const std::filesystem::path libPath = m_CacheDir / c_PipelineLibraryFile;
		const std::filesystem::path tmp =
			std::format("{}.{}.tmp", libPath.string(), core::process_id());

		std::error_code ec;
		std::filesystem::remove(tmp, ec);

		NS::Error* error = nullptr;
		if (!m_Archive->serializeToURL(FileUrl(tmp), &error))
		{
			logger::warn(
				"Could not serialize the Metal binary archive: {}",
				GetErrorDescription(error));
			return;
		}

		std::filesystem::rename(tmp, libPath, ec);
		if (ec)
		{
			logger::warn("Could not commit {}: {}", libPath.string(), ec.message());
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
	ShaderCache::ComputeKey(std::vector<std::pair<std::string, std::string>> moduleEntries) const
	{
		// Qualified: the member of the same name would otherwise recurse.
		return shader_cache::ComputeKey(m_SourceSalt, std::move(moduleEntries));
	}

	bool
	ShaderCache::TryLoad(uint64_t key, CachedProgram& out) const
	{
		const std::filesystem::path path = KeyPath(m_CacheDir, key);

		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return false;

		try
		{
			out = Deserialize(core::file::read_file_bytes(path.string()));
			return true;
		}
		catch (const std::exception& e)
		{
			logger::warn("Ignoring unreadable shader cache entry {}: {}", path.string(), e.what());
			return false;
		}
	}

	void
	ShaderCache::Store(uint64_t key, const CachedProgram& program) const
	{
		const std::vector<std::byte> bytes = Serialize(program);
		WriteFileAtomic(KeyPath(m_CacheDir, key), bytes);
	}
}
