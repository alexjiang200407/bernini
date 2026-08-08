#include "chunk_io.h"

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
		if (header.chunkTableOffset + tableBytes > bytes.size())
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

	std::unordered_map<uint32_t, std::vector<std::byte>>
	readChunksFromFile(
		const std::filesystem::path& path,
		uint32_t                     magic,
		uint16_t                     versionMajor,
		std::span<const uint32_t>    ids,
		std::string_view             what)
	{
		const std::string prefix(what);

		// Cleared so fileErrorMessage cannot blame a stale errno from an unrelated call for the failure.
		errno = 0;
		std::ifstream in(path, std::ios::binary);
		if (!in)
			throw std::runtime_error(
				fileErrorMessage(prefix + ": cannot open file for reading", path));

		std::error_code ec;
		const auto      fileSize = std::filesystem::file_size(path, ec);
		if (ec)
			throw std::runtime_error(fileErrorMessage(prefix + ": cannot size file", path));

		// Every offset and size below comes out of the file, so each is checked against the real size
		// before anything is allocated or seeked to. Subtraction rather than addition: a crafted
		// offset near UINT64_MAX would wrap past the sum.
		const auto checkRange = [&](uint64_t bytes, uint64_t offset) {
			if (bytes > fileSize || offset > fileSize - bytes)
				throw_runtime_error("{}: chunk extends past end of file", what);
		};

		const auto readAt = [&](void* dst, uint64_t bytes, uint64_t offset) {
			checkRange(bytes, offset);

			in.seekg(static_cast<std::streamoff>(offset));
			in.read(static_cast<char*>(dst), static_cast<std::streamsize>(bytes));
			if (!in)
				throw std::runtime_error(fileErrorMessage(prefix + ": failed to read file", path));
		};

		Header header{};
		readAt(&header, sizeof(header), 0);
		checkHeader(header, magic, versionMajor, what);

		checkRange(
			static_cast<uint64_t>(header.chunkCount) * sizeof(Entry),
			header.chunkTableOffset);

		std::vector<Entry> table(header.chunkCount);
		if (!table.empty())
			readAt(table.data(), table.size() * sizeof(Entry), header.chunkTableOffset);

		std::unordered_map<uint32_t, std::vector<std::byte>> out;
		for (const Entry& entry : table)
		{
			if (std::ranges::find(ids, entry.id) == ids.end())
				continue;

			checkRange(entry.byteSize, entry.offset);

			std::vector<std::byte> chunk(entry.byteSize);
			if (!chunk.empty())
				readAt(chunk.data(), chunk.size(), entry.offset);

			out.emplace(entry.id, std::move(chunk));
		}

		return out;
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
