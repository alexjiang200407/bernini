#include "chunk_io.h"

#include "CheckedFileReader.h"
#include "MountedFileReader.h"
#include "fs_util.h"

#include <core/err/util.h>

namespace assetlib::chunk
{
	using core::throw_runtime_error;
	using core::io::ByteReader;

	std::vector<std::byte>
	Writer::Finish(uint32_t magic, uint16_t versionMajor, uint16_t versionMinor)
	{
		m_Bytes.AlignTo(c_Align);
		const auto tableOffset = m_Bytes.Size();
		m_Bytes.WritePodArray(std::span<const Entry>(m_Chunks));

		Header header{};
		header.magic            = magic;
		header.versionMajor     = versionMajor;
		header.versionMinor     = versionMinor;
		header.byteOrder        = 0;
		header.chunkCount       = static_cast<uint32_t>(m_Chunks.size());
		header.chunkTableOffset = static_cast<uint32_t>(tableOffset);
		header.fileSize         = m_Bytes.Size();
		m_Bytes.PatchPod(0, header);

		return m_Bytes.Take();
	}

	namespace
	{
		void
		checkHeader(
			const Header&    header,
			uint32_t         magic,
			uint16_t         versionMajor,
			std::string_view what)
		{
			if (header.magic != magic)
				throw_runtime_error("{}: bad magic", what);
			if (header.versionMajor != versionMajor)
				throw_runtime_error("{}: unsupported major version", what);
			if (header.byteOrder != 0)
				throw_runtime_error("{}: unsupported byte order", what);
		}
	}

	Reader::Reader(
		std::span<const std::byte> bytes,
		uint32_t                   magic,
		uint16_t                   versionMajor,
		std::string_view           what) : m_Bytes(bytes), m_What(what)
	{
		ByteReader reader(bytes);
		const auto header = reader.ReadPod<Header>();

		checkHeader(header, magic, versionMajor, what);

		if (header.fileSize > bytes.size())
			throw_runtime_error("{}: stream shorter than declared file size", what);

		const auto tableBytes = static_cast<size_t>(header.chunkCount) * sizeof(Entry);
		if (tableBytes > bytes.size() || header.chunkTableOffset > bytes.size() - tableBytes)
			throw_runtime_error("{}: chunk table extends past end of stream", what);

		reader.Seek(header.chunkTableOffset);
		for (uint32_t i = 0; i < header.chunkCount; ++i)
		{
			const auto entry = reader.ReadPod<Entry>();
			m_Table.emplace(entry.id, entry);
		}
	}

	void
	Reader::CheckEntry(const Entry& entry, size_t elementSize) const
	{
		if (entry.elementSize != elementSize)
			throw_runtime_error("{}: chunk element size mismatch", m_What);
		if (entry.byteSize % elementSize != 0)
			throw_runtime_error(
				"{}: chunk byte size is not a multiple of the element size",
				m_What);
		// Subtraction rather than addition: a crafted offset near UINT64_MAX would wrap past the sum.
		if (entry.byteSize > m_Bytes.size() || entry.offset > m_Bytes.size() - entry.byteSize)
			throw_runtime_error("{}: chunk extends past end of stream", m_What);
	}

	ChunkData
	readChunksFromFile(
		const std::filesystem::path& path,
		uint32_t                     magic,
		uint16_t                     versionMajor,
		std::span<const uint32_t>    ids,
		std::string_view             what)
	{
		CheckedFileReader source(path, what);
		return readChunks(source, magic, versionMajor, ids, what);
	}

	ChunkData
	readChunksFrom(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		uint32_t                       magic,
		uint16_t                       versionMajor,
		std::span<const uint32_t>      ids,
		std::string_view               what)
	{
		MountedFileReader source(fileSystem, path, what);
		return readChunks(source, magic, versionMajor, ids, what);
	}

	ChunkData
	readChunks(
		IRangeReader&             source,
		uint32_t                  magic,
		uint16_t                  versionMajor,
		std::span<const uint32_t> ids,
		std::string_view          what)
	{
		Header header{};
		source.ReadAt(&header, sizeof(header), 0);
		checkHeader(header, magic, versionMajor, what);

		const uint64_t tableBytes = static_cast<uint64_t>(header.chunkCount) * sizeof(Entry);
		source.CheckRange(tableBytes, header.chunkTableOffset);

		std::vector<Entry> table(header.chunkCount);
		if (!table.empty())
			source.ReadAt(table.data(), tableBytes, header.chunkTableOffset);

		// Sized before anything is read, so the payloads land in one buffer rather than one
		// allocation each.
		std::vector<ChunkData::Slot> slots;
		uint64_t                     total = 0;
		for (const Entry& entry : table)
		{
			if (std::ranges::find(ids, entry.id) == ids.end())
				continue;

			source.CheckRange(entry.byteSize, entry.offset);

			slots.push_back(
				{ entry.id, static_cast<uint32_t>(total), static_cast<uint32_t>(entry.byteSize) });
			total += entry.byteSize;
		}

		std::vector<std::byte> bytes(total);
		for (size_t i = 0; i < slots.size(); ++i)
		{
			if (slots[i].size == 0)
				continue;

			const Entry& entry = *std::ranges::find(table, slots[i].id, &Entry::id);
			source.ReadAt(bytes.data() + slots[i].offset, slots[i].size, entry.offset);
		}

		return ChunkData(std::move(bytes), std::move(slots));
	}

	std::vector<char>
	packStrings(std::span<const std::string> strings)
	{
		std::vector<char> pool;
		for (const auto& s : strings)
		{
			pool.insert(pool.end(), s.begin(), s.end());
			pool.push_back('\0');
		}
		return pool;
	}

	std::vector<std::string>
	unpackStrings(std::span<const char> pool)
	{
		std::vector<std::string> out;
		std::string              current;
		for (const char c : pool)
		{
			if (c == '\0')
			{
				out.push_back(current);
				current.clear();
			}
			else
			{
				current.push_back(c);
			}
		}
		return out;
	}
}
