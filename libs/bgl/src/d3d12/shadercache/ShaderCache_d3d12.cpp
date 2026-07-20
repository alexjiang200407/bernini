#include "shadercache/ShaderCache_d3d12.h"
#include <core/file/file.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace bgl
{
	namespace
	{
		// Bump when the on-disk format below changes; folded into every key so old
		// files are missed rather than misread.
		constexpr uint32_t kCacheFormatVersion = 1;

		constexpr uint64_t kFnvOffset = 14695981039346656037ull;
		constexpr uint64_t kFnvPrime  = 1099511628211ull;

		uint64_t
		HashBytes(const void* data, size_t size, uint64_t seed = kFnvOffset)
		{
			const auto* bytes = static_cast<const uint8_t*>(data);
			uint64_t    hash  = seed;
			for (size_t i = 0; i < size; ++i)
			{
				hash ^= bytes[i];
				hash *= kFnvPrime;
			}
			return hash;
		}

		uint64_t
		HashString(std::string_view str, uint64_t seed)
		{
			return HashBytes(str.data(), str.size(), seed);
		}

		// One hash over the compile options and the content of every shader source
		// file. Any edit to a shader, or a change of compiler/options, moves this and
		// therefore every derived key.
		uint64_t
		ComputeSourceSalt(std::string_view optionsSalt, const std::vector<std::string>& searchPaths)
		{
			namespace fs = std::filesystem;

			uint64_t salt = HashString(optionsSalt, kFnvOffset);
			salt          = HashBytes(&kCacheFormatVersion, sizeof(kCacheFormatVersion), salt);

			// Sort by path so the salt is order-independent across filesystem walks.
			std::vector<fs::path> files;
			for (const std::string& root : searchPaths)
			{
				std::error_code ec;
				if (!fs::exists(root, ec))
					continue;

				for (auto it = fs::recursive_directory_iterator(root, ec);
				     !ec && it != fs::recursive_directory_iterator();
				     it.increment(ec))
				{
					if (it->is_regular_file(ec))
						files.push_back(it->path());
				}
			}

			std::sort(files.begin(), files.end());

			for (const fs::path& file : files)
			{
				const std::string path = file.generic_string();
				salt                   = HashString(path, salt);

				const std::vector<std::byte> bytes = core::file::readFileBytes(path);
				salt                               = HashBytes(bytes.data(), bytes.size(), salt);
			}

			return salt;
		}

		using core::io::ByteReader;
		using core::io::ByteWriter;

		void
		WriteString(ByteWriter& writer, std::string_view value)
		{
			writer.writePod<uint32_t>(static_cast<uint32_t>(value.size()));
			writer.writeBytes(std::as_bytes(std::span<const char>(value.data(), value.size())));
		}

		std::string
		ReadString(ByteReader& reader)
		{
			const uint32_t                   size = reader.readPod<uint32_t>();
			const std::span<const std::byte> raw  = reader.readBytes(size);
			return std::string(reinterpret_cast<const char*>(raw.data()), size);
		}

		void
		WriteBlob(ByteWriter& writer, const std::vector<std::byte>& value)
		{
			writer.writePod<uint32_t>(static_cast<uint32_t>(value.size()));
			writer.writeBytes(value);
		}

		std::vector<std::byte>
		ReadBlob(ByteReader& reader)
		{
			const uint32_t                   size = reader.readPod<uint32_t>();
			const std::span<const std::byte> raw  = reader.readBytes(size);
			return std::vector<std::byte>(raw.begin(), raw.end());
		}

		void
		WriteLayout(ByteWriter& writer, const ReflectedLayout& layout)
		{
			writer.writePod<uint32_t>(static_cast<uint32_t>(layout.kind));
			writer.writePod<uint32_t>(static_cast<uint32_t>(layout.valueType));
			writer.writePod<uint32_t>(layout.size);
			writer.writePod<uint32_t>(layout.arrayCount);
			writer.writePod<uint32_t>(layout.arrayStride);

			writer.writePod<uint32_t>(static_cast<uint32_t>(layout.fields.size()));
			for (const ReflectedField& field : layout.fields)
			{
				WriteString(writer, field.name);
				writer.writePod<uint32_t>(field.offset);
				WriteLayout(writer, field.layout);
			}

			writer.writePod<uint32_t>(static_cast<uint32_t>(layout.element.size()));
			for (const ReflectedLayout& element : layout.element) WriteLayout(writer, element);
		}

		ReflectedLayout
		ReadLayout(ByteReader& reader)
		{
			ReflectedLayout layout;
			layout.kind        = static_cast<UniformType>(reader.readPod<uint32_t>());
			layout.valueType   = static_cast<UniformValueType>(reader.readPod<uint32_t>());
			layout.size        = reader.readPod<uint32_t>();
			layout.arrayCount  = reader.readPod<uint32_t>();
			layout.arrayStride = reader.readPod<uint32_t>();

			const uint32_t fieldCount = reader.readPod<uint32_t>();
			layout.fields.reserve(fieldCount);
			for (uint32_t i = 0; i < fieldCount; ++i)
			{
				ReflectedField field;
				field.name   = ReadString(reader);
				field.offset = reader.readPod<uint32_t>();
				field.layout = ReadLayout(reader);
				layout.fields.push_back(std::move(field));
			}

			const uint32_t elementCount = reader.readPod<uint32_t>();
			layout.element.reserve(elementCount);
			for (uint32_t i = 0; i < elementCount; ++i)
				layout.element.push_back(ReadLayout(reader));

			return layout;
		}

		std::vector<std::byte>
		Serialize(const CachedProgram& program)
		{
			ByteWriter writer;

			writer.writePod<uint32_t>(static_cast<uint32_t>(program.cbuffers.size()));
			for (const CachedCbuffer& cbuffer : program.cbuffers)
			{
				WriteString(writer, cbuffer.name);
				writer.writePod<uint32_t>(cbuffer.size);
				writer.writePod<uint32_t>(cbuffer.rootParamIndex);
				writer.writePod<uint32_t>(cbuffer.shaderRegister);
				writer.writePod<uint32_t>(cbuffer.registerSpace);
				WriteLayout(writer, cbuffer.layout);
			}

			writer.writePod<uint32_t>(static_cast<uint32_t>(program.entryPointDxil.size()));
			for (const auto& [entry, dxil] : program.entryPointDxil)
			{
				WriteString(writer, entry);
				WriteBlob(writer, dxil);
			}

			return writer.take();
		}

		CachedProgram
		Deserialize(const std::vector<std::byte>& bytes)
		{
			ByteReader    reader(bytes);
			CachedProgram program;

			const uint32_t cbufferCount = reader.readPod<uint32_t>();
			program.cbuffers.reserve(cbufferCount);
			for (uint32_t i = 0; i < cbufferCount; ++i)
			{
				CachedCbuffer cbuffer;
				cbuffer.name           = ReadString(reader);
				cbuffer.size           = reader.readPod<uint32_t>();
				cbuffer.rootParamIndex = reader.readPod<uint32_t>();
				cbuffer.shaderRegister = reader.readPod<uint32_t>();
				cbuffer.registerSpace  = reader.readPod<uint32_t>();
				cbuffer.layout         = ReadLayout(reader);
				program.cbuffers.push_back(std::move(cbuffer));
			}

			const uint32_t entryCount = reader.readPod<uint32_t>();
			program.entryPointDxil.reserve(entryCount);
			for (uint32_t i = 0; i < entryCount; ++i)
			{
				std::string            entry = ReadString(reader);
				std::vector<std::byte> dxil  = ReadBlob(reader);
				program.entryPointDxil.emplace_back(std::move(entry), std::move(dxil));
			}

			return program;
		}

		std::filesystem::path
		KeyPath(const std::filesystem::path& dir, uint64_t key)
		{
			return dir / std::format("{:016x}.bsc", key);
		}

		constexpr const char* kPipelineLibraryFile = "pipelines.psolib";

		// Writes via a temp file then renames, so a crash mid-write never leaves a
		// half-written file that would later look valid. The temp name carries the process
		// id because several processes may share one cache directory -- a sharded test run
		// does -- and a fixed name would let them write into each other's file.
		bool
		WriteFileAtomic(const std::filesystem::path& path, const std::byte* data, size_t size)
		{
			static std::atomic<uint32_t> counter = 0;

			const std::filesystem::path tmp = std::format(
				"{}.{}.{}.tmp",
				path.string(),
				GetCurrentProcessId(),
				counter.fetch_add(1, std::memory_order_relaxed));

			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				logger::warn("Could not open cache file for writing: {}", tmp.string());
				return false;
			}

			out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
			out.close();

			std::error_code ec;
			std::filesystem::rename(tmp, path, ec);
			if (ec)
			{
				logger::warn("Could not commit cache file {}: {}", path.string(), ec.message());
				std::filesystem::remove(tmp, ec);
				return false;
			}
			return true;
		}
	}

	ShaderCache::ShaderCache(
		ID3D12Device*                   device,
		std::filesystem::path           cacheDir,
		std::string_view                optionsSalt,
		const std::vector<std::string>& searchPaths,
		bool                            usePipelineLibrary) :
		m_CacheDir(std::move(cacheDir)), m_SourceSalt(ComputeSourceSalt(optionsSalt, searchPaths))
	{
		std::error_code ec;
		std::filesystem::create_directories(m_CacheDir, ec);

		if (!usePipelineLibrary)
			return;

		wrl::ComPtr<ID3D12Device1> device1;
		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))))
			return;

		const std::filesystem::path libPath = m_CacheDir / kPipelineLibraryFile;
		if (std::filesystem::exists(libPath, ec))
		{
			try
			{
				const std::vector<std::byte> bytes = core::file::readFileBytes(libPath.string());
				m_PsoLibraryBlob.assign(bytes.begin(), bytes.end());
			}
			catch (const std::exception&)
			{
				m_PsoLibraryBlob.clear();
			}
		}

		// A blob from a different driver/adapter/runtime is rejected here; fall back to
		// an empty library so those PSOs are simply recompiled and re-stored.
		HRESULT hr = device1->CreatePipelineLibrary(
			m_PsoLibraryBlob.empty() ? nullptr : m_PsoLibraryBlob.data(),
			m_PsoLibraryBlob.size(),
			IID_PPV_ARGS(&m_PsoLibrary));

		if (FAILED(hr))
		{
			m_PsoLibraryBlob.clear();
			device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_PsoLibrary));
		}
	}

	ShaderCache::~ShaderCache()
	{
		if (!m_PsoLibrary || !m_PsoLibraryDirty)
			return;

		const SIZE_T           size = m_PsoLibrary->GetSerializedSize();
		std::vector<std::byte> blob(size);
		if (FAILED(m_PsoLibrary->Serialize(blob.data(), size)))
			return;

		WriteFileAtomic(m_CacheDir / kPipelineLibraryFile, blob.data(), blob.size());
	}

	uint64_t
	ShaderCache::CombineHash(uint64_t seed, std::span<const std::byte> bytes)
	{
		return HashBytes(bytes.data(), bytes.size(), seed);
	}

	bool
	ShaderCache::LoadPipeline(
		uint64_t                                identity,
		const D3D12_PIPELINE_STATE_STREAM_DESC& desc,
		ID3D12PipelineState**                   outPipeline)
	{
		if (!m_PsoLibrary)
			return false;

		const std::wstring name = std::format(L"{:016x}", identity);
		return SUCCEEDED(
			m_PsoLibrary->LoadPipeline(name.c_str(), &desc, IID_PPV_ARGS(outPipeline)));
	}

	void
	ShaderCache::StorePipeline(uint64_t identity, ID3D12PipelineState* pipeline)
	{
		if (!m_PsoLibrary || pipeline == nullptr)
			return;

		const std::wstring name = std::format(L"{:016x}", identity);
		if (SUCCEEDED(m_PsoLibrary->StorePipeline(name.c_str(), pipeline)))
			m_PsoLibraryDirty = true;
	}

	uint64_t
	ShaderCache::ComputeKey(std::vector<std::pair<std::string, std::string>> moduleEntries) const
	{
		std::sort(moduleEntries.begin(), moduleEntries.end());

		uint64_t key = m_SourceSalt;
		for (const auto& [module, entry] : moduleEntries)
		{
			key = HashString(module, key);
			key = HashString(entry, key);
		}
		return key;
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
			out = Deserialize(core::file::readFileBytes(path.string()));
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
		WriteFileAtomic(KeyPath(m_CacheDir, key), bytes.data(), bytes.size());
	}
}
