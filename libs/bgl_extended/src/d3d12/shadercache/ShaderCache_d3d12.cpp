#include "shadercache/ShaderCache_d3d12.h"

#include "shadercache/util.h"

#include <core/file/file.h>
#include <core/hash.h>

namespace bgl
{
	using namespace shader_cache;

	namespace
	{
		// Bump when the on-disk format below changes; folded into every key so old
		// files are missed rather than misread.
		constexpr uint32_t kCacheFormatVersion = 1;

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
				writer.WritePod<uint32_t>(cbuffer.rootParamIndex);
				writer.WritePod<uint32_t>(cbuffer.shaderRegister);
				writer.WritePod<uint32_t>(cbuffer.registerSpace);
				WriteLayout(writer, cbuffer.layout);
			}

			writer.WritePod<uint32_t>(static_cast<uint32_t>(program.entryPointDxil.size()));
			for (const auto& [entry, dxil] : program.entryPointDxil)
			{
				WriteString(writer, entry);
				WriteBlob(writer, dxil);
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
				cbuffer.name           = ReadString(reader);
				cbuffer.size           = reader.ReadPod<uint32_t>();
				cbuffer.rootParamIndex = reader.ReadPod<uint32_t>();
				cbuffer.shaderRegister = reader.ReadPod<uint32_t>();
				cbuffer.registerSpace  = reader.ReadPod<uint32_t>();
				cbuffer.layout         = ReadLayout(reader);
				program.cbuffers.push_back(std::move(cbuffer));
			}

			const uint32_t entryCount = reader.ReadPod<uint32_t>();
			program.entryPointDxil.reserve(entryCount);
			for (uint32_t i = 0; i < entryCount; ++i)
			{
				std::string            entry = ReadString(reader);
				std::vector<std::byte> dxil  = ReadBlob(reader);
				program.entryPointDxil.emplace_back(std::move(entry), std::move(dxil));
			}

			return program;
		}

		constexpr const char* kPipelineLibraryFile = "pipelines.psolib";
	}

	ShaderCache::ShaderCache(
		ID3D12Device*                   device,
		std::filesystem::path           cacheDir,
		std::string_view                optionsSalt,
		const std::vector<std::string>& searchPaths,
		bool                            usePipelineLibrary) :
		m_CacheDir(std::move(cacheDir)),
		m_SourceSalt(ComputeSourceSalt(optionsSalt, searchPaths, kCacheFormatVersion))
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
				const std::vector<std::byte> bytes = core::file::read_file_bytes(libPath.string());
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

		WriteFileAtomic(m_CacheDir / kPipelineLibraryFile, blob);
	}

	uint64_t
	ShaderCache::CombineHash(uint64_t seed, std::span<const std::byte> bytes)
	{
		return core::hash_bytes(bytes.data(), bytes.size(), seed);
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
