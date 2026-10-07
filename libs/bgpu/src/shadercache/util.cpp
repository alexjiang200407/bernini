#include "shadercache/util.h"
#include <bgpu/reflection/ReflectedLayout.h>
#include <bgpu/uniforms/UniformValueType.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <cstdint>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>  // IWYU pragma: keep
#	include <fileapi.h>
#	include <handleapi.h>
#	include <winbase.h>
#	include <winnt.h>
#endif

namespace bgpu::shader_cache
{
	void
	WriteLayout(core::io::ByteWriter& writer, const ReflectedLayout& layout)
	{
		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.kind));
		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.valueType));
		writer.WritePod<uint32_t>(layout.size);
		writer.WritePod<uint32_t>(layout.arrayCount);
		writer.WritePod<uint32_t>(layout.arrayStride);

		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.fields.size()));
		for (const ReflectedField& field : layout.fields)
		{
			writer.WriteString(field.name);
			writer.WritePod<uint32_t>(field.offset);
			WriteLayout(writer, field.layout);
		}

		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.element.size()));
		for (const ReflectedLayout& element : layout.element) WriteLayout(writer, element);
	}

	ReflectedLayout
	ReadLayout(core::io::ByteReader& reader)
	{
		ReflectedLayout layout;
		layout.kind        = static_cast<UniformType>(reader.ReadPod<uint32_t>());
		layout.valueType   = static_cast<UniformValueType>(reader.ReadPod<uint32_t>());
		layout.size        = reader.ReadPod<uint32_t>();
		layout.arrayCount  = reader.ReadPod<uint32_t>();
		layout.arrayStride = reader.ReadPod<uint32_t>();

		const uint32_t fieldCount = reader.ReadPod<uint32_t>();
		layout.fields.reserve(fieldCount);
		for (uint32_t i = 0; i < fieldCount; ++i)
		{
			ReflectedField field;
			field.name   = reader.ReadString();
			field.offset = reader.ReadPod<uint32_t>();
			field.layout = ReadLayout(reader);
			layout.fields.push_back(std::move(field));
		}

		const uint32_t elementCount = reader.ReadPod<uint32_t>();
		layout.element.reserve(elementCount);
		for (uint32_t i = 0; i < elementCount; ++i) layout.element.push_back(ReadLayout(reader));

		return layout;
	}

	PipelineLibraryClaim::PipelineLibraryClaim(const std::filesystem::path& directory) noexcept
	{
#if defined(_WIN32)
		const std::filesystem::path lockPath =
			directory / (std::string(c_PipelineLibraryFile) + ".lock");

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
			return;
		}
		m_Lock = lock;
#else
		(void)directory;
#endif
	}

	PipelineLibraryClaim::~PipelineLibraryClaim() noexcept
	{
#if defined(_WIN32)
		if (m_Lock != nullptr)
			CloseHandle(m_Lock);
#endif
	}
}
