#include "shadercache/ShaderCache_vulkan.h"
#include "native_device_vulkan.h"
#include "shadercache/util.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <core/file/file.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>
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

		// A driver must accept another device's or version's data and ignore it, but not every one
		// does; one whose header names this device and driver is the only one handed to it.
		[[nodiscard]] bool
		MatchesDevice(const std::vector<std::byte>& data, const VkPhysicalDevice physical) noexcept
		{
			auto header = VkPipelineCacheHeaderVersionOne();
			if (data.size() < sizeof(header))
				return false;
			std::memcpy(&header, data.data(), sizeof(header));

			auto properties = VkPhysicalDeviceProperties();
			vkGetPhysicalDeviceProperties(physical, &properties);
			return header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
			       header.vendorID == properties.vendorID &&
			       header.deviceID == properties.deviceID &&
			       std::memcmp(
					   header.pipelineCacheUUID,
					   properties.pipelineCacheUUID,
					   VK_UUID_SIZE) == 0;
		}
	}

	ShaderCache::ShaderCache(GpuContextRef context, const bool usePipelineCache) :
		m_Context(std::move(context)), m_Programs(*m_Context->GetProgramCache())
	{
		if (!usePipelineCache)
			return;

		m_Claim = std::make_unique<shader_cache::PipelineLibraryClaim>(m_Programs.GetDirectory());
		if (!m_Claim->Held())
			return;

		const VulkanHandles handles = GetVulkanHandles(*m_Context);
		auto                saved   = std::vector<std::byte>();
		const auto          path = m_Programs.GetDirectory() / shader_cache::c_PipelineLibraryFile;
		std::error_code     ec;
		if (std::filesystem::exists(path, ec))
		{
			try
			{
				saved = core::file::read_file_bytes(path.string());
			}
			catch (const std::exception& e)
			{
				spdlog::warn("Could not read the pipeline cache {}: {}", path.string(), e.what());
			}
		}
		if (!MatchesDevice(saved, handles.physicalDevice))
			saved.clear();

		auto info            = VkPipelineCacheCreateInfo();
		info.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		info.initialDataSize = saved.size();
		info.pInitialData    = saved.empty() ? nullptr : saved.data();
		EnsureVk(
			vkCreatePipelineCache(handles.device, &info, nullptr, &m_PipelineCache),
			"vkCreatePipelineCache");
	}

	ShaderCache::~ShaderCache() noexcept
	{
		if (m_PipelineCache == VK_NULL_HANDLE)
			return;

		const VkDevice device = GetVulkanHandles(*m_Context).device;
		size_t         size   = 0;
		auto           blob   = std::vector<std::byte>();
		if (vkGetPipelineCacheData(device, m_PipelineCache, &size, nullptr) == VK_SUCCESS)
		{
			blob.resize(size);
			if (vkGetPipelineCacheData(device, m_PipelineCache, &size, blob.data()) != VK_SUCCESS)
				blob.clear();
		}
		vkDestroyPipelineCache(device, m_PipelineCache, nullptr);

		if (blob.empty())
			return;
		const auto path = m_Programs.GetDirectory() / shader_cache::c_PipelineLibraryFile;
		try
		{
			core::file::write_atomic(path, blob);
		}
		catch (const std::exception& e)
		{
			spdlog::warn("Could not write the pipeline cache {}: {}", path.string(), e.what());
		}
	}

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
