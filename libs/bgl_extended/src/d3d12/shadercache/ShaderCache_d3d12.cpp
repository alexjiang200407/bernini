#include "shadercache/ShaderCache_d3d12.h"

#include <bgl_common/shadercache/util.h>

#include <core/file/file.h>
#include <core/hash.h>
#include <string_view>

#include <string>

#include <bgpu/GpuContext.h>
#include <spdlog/spdlog.h>

namespace bgl
{
	using shader_cache::ReadLayout;
	using shader_cache::WriteLayout;

	namespace
	{
		// Bump formatVersion when the encoding below changes: it is in every key, so an entry in the
		// old layout is missed rather than misread.
		constexpr auto c_Owner = bgpu::ProgramCacheOwner{ .tag = "bgl_d3d12", .formatVersion = 1 };

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
				writer.WritePod<uint32_t>(cbuffer.rootParamIndex);
				writer.WritePod<uint32_t>(cbuffer.shaderRegister);
				writer.WritePod<uint32_t>(cbuffer.registerSpace);
				WriteLayout(writer, cbuffer.layout);
			}

			writer.WritePod<uint32_t>(static_cast<uint32_t>(program.entryPointDxil.size()));
			for (const auto& [entry, dxil] : program.entryPointDxil)
			{
				writer.WriteString(entry);
				writer.WriteBlob(dxil);
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
				cbuffer.name           = reader.ReadString();
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
				std::string            entry = reader.ReadString();
				std::vector<std::byte> dxil  = reader.ReadBlob();
				program.entryPointDxil.emplace_back(std::move(entry), std::move(dxil));
			}

			return program;
		}

		constexpr const char* c_PipelineLibraryFile     = "pipelines.psolib";
		constexpr const char* c_PipelineLibraryLockFile = "pipelines.psolib.lock";
	}

	ShaderCache::ShaderCache(
		bgpu::GpuContextRef context,
		ID3D12Device*       device,
		bool                usePipelineLibrary) :
		m_Context(std::move(context)), m_Programs(*m_Context->GetProgramCache())
	{
		if (!usePipelineLibrary)
			return;

		// The library is replaced whole by whoever writes it last, so two writers on one directory
		// -- the suite's four shards, or two renderers in one process -- would each discard the
		// other's. One claims it and the rest run without a driver library, which costs PSO
		// creation and nothing else: the program cache beside it is content-keyed and shared safely.
		if (!ClaimPipelineLibrary())
			return;

		wrl::ComPtr<ID3D12Device1> device1;
		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))))
			return;

		const std::filesystem::path libPath = m_Programs.GetDirectory() / c_PipelineLibraryFile;
		std::error_code             ec;
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

	bool
	ShaderCache::ClaimPipelineLibrary()
	{
		const std::filesystem::path lockPath =
			m_Programs.GetDirectory() / c_PipelineLibraryLockFile;

		// No sharing, so a second opener is refused rather than queued, and delete-on-close so the
		// claim ends with the process however it ends.
		const HANDLE lock = CreateFileW(
			lockPath.wstring().c_str(),
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
			nullptr);

		if (lock == INVALID_HANDLE_VALUE)
		{
			spdlog::debug(
				"Another writer holds {}; this device builds its pipelines without the driver "
				"library",
				lockPath.string());
			return false;
		}

		m_PsoLibraryLock = lock;
		return true;
	}
	ShaderCache::~ShaderCache()
	{
		if (m_PsoLibrary && m_PsoLibraryDirty)
		{
			const SIZE_T           size = m_PsoLibrary->GetSerializedSize();
			std::vector<std::byte> blob(size);
			if (SUCCEEDED(m_PsoLibrary->Serialize(blob.data(), size)))
			{
				const std::filesystem::path libPath =
					m_Programs.GetDirectory() / c_PipelineLibraryFile;
				try
				{
					core::file::write_atomic(libPath, blob);
				}
				catch (const std::exception& e)
				{
					spdlog::warn(
						"Could not write the pipeline library {}: {}",
						libPath.string(),
						e.what());
				}
			}
		}

		// After the write: the claim is what makes this process the file's one writer.
		if (m_PsoLibraryLock != nullptr)
			CloseHandle(m_PsoLibraryLock);
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
		const auto         held = std::lock_guard(m_PsoLibraryMutex);
		return SUCCEEDED(
			m_PsoLibrary->LoadPipeline(name.c_str(), &desc, IID_PPV_ARGS(outPipeline)));
	}

	void
	ShaderCache::StorePipeline(uint64_t identity, ID3D12PipelineState* pipeline)
	{
		if (!m_PsoLibrary || pipeline == nullptr)
			return;

		const std::wstring name = std::format(L"{:016x}", identity);
		const auto         held = std::lock_guard(m_PsoLibraryMutex);
		if (SUCCEEDED(m_PsoLibrary->StorePipeline(name.c_str(), pipeline)))
			m_PsoLibraryDirty = true;
	}

	uint64_t
	ShaderCache::ComputeKey(std::vector<std::pair<std::string, std::string>> moduleEntries) const
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
