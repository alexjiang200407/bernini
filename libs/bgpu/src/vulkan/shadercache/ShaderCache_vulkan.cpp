#include "shadercache/ShaderCache_vulkan.h"
#include "shadercache/util.h"  // IWYU pragma: keep
#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <core/err/util.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>
#include <vector>

namespace bgpu
{
	namespace
	{
		// Bump formatVersion when the encoding below changes: it is in every key, so an entry in the
		// old layout is missed rather than misread.
		constexpr auto c_Owner = ProgramCacheOwner{ .tag = "bgpu_vulkan", .formatVersion = 1 };

		[[nodiscard]] std::vector<std::byte>
		Serialize(const CachedProgram& program)
		{
			auto writer = core::io::ByteWriter();

			writer.WritePod<uint32_t>(static_cast<uint32_t>(program.cbuffers.size()));
			for (const CachedCbuffer& cbuffer : program.cbuffers)
			{
				writer.WriteString(cbuffer.name);
				writer.WritePod<uint32_t>(cbuffer.size);
				writer.WritePod<uint32_t>(cbuffer.rootParamIndex);
				writer.WritePod<uint32_t>(cbuffer.binding);
				writer.WritePod<uint32_t>(cbuffer.set);
				shader_cache::WriteLayout(writer, cbuffer.layout);
			}

			writer.WritePod<uint32_t>(static_cast<uint32_t>(program.entryPointSpirv.size()));
			for (const auto& [entry, spirv] : program.entryPointSpirv)
			{
				writer.WriteString(entry);
				writer.WriteBlob(spirv);
			}

			return writer.Take();
		}

		[[nodiscard]] CachedProgram
		Deserialize(const std::vector<std::byte>& bytes)
		{
			auto reader  = core::io::ByteReader(bytes);
			auto program = CachedProgram();

			const auto cbufferCount = reader.ReadPod<uint32_t>();
			program.cbuffers.reserve(cbufferCount);
			for (uint32_t i = 0; i < cbufferCount; ++i)
			{
				auto cbuffer           = CachedCbuffer();
				cbuffer.name           = reader.ReadString();
				cbuffer.size           = reader.ReadPod<uint32_t>();
				cbuffer.rootParamIndex = reader.ReadPod<uint32_t>();
				cbuffer.binding        = reader.ReadPod<uint32_t>();
				cbuffer.set            = reader.ReadPod<uint32_t>();
				cbuffer.layout         = shader_cache::ReadLayout(reader);
				program.cbuffers.push_back(std::move(cbuffer));
			}

			const auto entryCount = reader.ReadPod<uint32_t>();
			program.entryPointSpirv.reserve(entryCount);
			for (uint32_t i = 0; i < entryCount; ++i)
			{
				std::string            entry = reader.ReadString();
				std::vector<std::byte> spirv = reader.ReadBlob();
				program.entryPointSpirv.push_back(
					{ .entryPoint = std::move(entry), .spirv = std::move(spirv) });
			}

			return program;
		}
	}

	ShaderCache::ShaderCache(GpuContextRef context) :
		m_Context(std::move(context)), m_Programs(*m_Context->GetProgramCache())
	{}

	uint64_t
	ShaderCache::ComputeKey(std::vector<ProgramEntryPoint> moduleEntries) const
	{
		return m_Programs.ComputeKey(c_Owner, std::move(moduleEntries));
	}

	bool
	ShaderCache::TryLoad(const uint64_t key, CachedProgram& out) const
	{
		auto bytes = std::vector<std::byte>();
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
	ShaderCache::Store(const uint64_t key, const CachedProgram& program) const
	{
		m_Programs.StoreProgram(key, Serialize(program));
	}
}
