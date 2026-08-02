#include "ChunkFile.h"

#include <core/err/util.h>
#include <core/io/ByteReader.h>

namespace assetlib
{
	void
	validateHeader(
		const ChunkHeader& header,
		uint32_t           magic,
		uint16_t           versionMajor,
		std::string_view   context)
	{
		if (header.magic != magic)
			core::throw_runtime_error("{}: bad magic", context);

		if (header.versionMajor != versionMajor)
			core::throw_runtime_error(
				"{}: unsupported major version {} (this build reads {})",
				context,
				header.versionMajor,
				versionMajor);

		if (header.byteOrder != 0)
			core::throw_runtime_error("{}: unsupported byte order", context);
	}

	ChunkTable::ChunkTable(
		std::span<const std::byte> bytes,
		const ChunkHeader&         header,
		std::string_view           context) : m_Bytes(bytes), m_Context(context)
	{
		// Bounded before the table is sized from it, not after: a corrupt chunkCount would otherwise
		// allocate its way to 96 GB on the way to being rejected.
		const auto tableBytes = static_cast<uint64_t>(header.chunkCount) * sizeof(ChunkEntry);
		if (!fitsWithin(header.chunkTableOffset, tableBytes, bytes.size()))
			core::throw_runtime_error("{}: chunk table extends past end of stream", m_Context);

		auto reader = core::io::ByteReader(bytes);
		reader.seek(header.chunkTableOffset);

		m_Entries.reserve(header.chunkCount);
		for (uint32_t i = 0; i < header.chunkCount; ++i)
		{
			const auto entry = reader.readPod<ChunkEntry>();
			m_Entries.emplace(entry.id, entry);
		}
	}

	void
	ChunkTable::ThrowMissing(uint32_t id) const
	{
		core::throw_runtime_error("{}: missing required chunk {}", m_Context, id);
	}

	std::span<const std::byte>
	ChunkTable::Bytes(const ChunkEntry& entry) const
	{
		if (!fitsWithin(entry.offset, entry.byteSize, m_Bytes.size()))
			core::throw_runtime_error(
				"{}: chunk {} extends past end of stream",
				m_Context,
				entry.id);

		return m_Bytes.subspan(
			static_cast<size_t>(entry.offset),
			static_cast<size_t>(entry.byteSize));
	}

	void
	ChunkTable::RequireElements(const ChunkEntry& entry, size_t elementSize) const
	{
		if (entry.elementSize != elementSize)
			core::throw_runtime_error(
				"{}: chunk {} element size mismatch (stored {}, this build reads {})",
				m_Context,
				entry.id,
				entry.elementSize,
				elementSize);

		if (entry.byteSize % elementSize != 0)
			core::throw_runtime_error(
				"{}: chunk {} byte size is not a multiple of the element size",
				m_Context,
				entry.id);
	}
}
