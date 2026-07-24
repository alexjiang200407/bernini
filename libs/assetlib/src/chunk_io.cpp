#include "chunk_io.h"

#include "fs_util.h"

namespace assetlib::chunk
{
	using core::io::ByteReader;

	std::vector<std::byte>
	Writer::Finish(uint32_t magic, uint16_t versionMajor, uint16_t versionMinor)
	{
		m_Bytes.alignTo(c_Align);
		const auto tableOffset = m_Bytes.size();
		m_Bytes.writePodArray(std::span<const Entry>(m_Chunks));

		Header header{};
		header.magic            = magic;
		header.versionMajor     = versionMajor;
		header.versionMinor     = versionMinor;
		header.byteOrder        = 0;
		header.chunkCount       = static_cast<uint32_t>(m_Chunks.size());
		header.chunkTableOffset = static_cast<uint32_t>(tableOffset);
		header.fileSize         = m_Bytes.size();
		m_Bytes.patchPod(0, header);

		return m_Bytes.take();
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
			const std::string prefix(what);

			if (header.magic != magic)
				throw std::runtime_error(prefix + ": bad magic");
			if (header.versionMajor != versionMajor)
				throw std::runtime_error(prefix + ": unsupported major version");
			if (header.byteOrder != 0)
				throw std::runtime_error(prefix + ": unsupported byte order");
		}
	}

	Reader::Reader(
		std::span<const std::byte> bytes,
		uint32_t                   magic,
		uint16_t                   versionMajor,
		std::string_view           what) : m_Bytes(bytes), m_What(what)
	{
		ByteReader reader(bytes);
		const auto header = reader.readPod<Header>();

		checkHeader(header, magic, versionMajor, what);

		const std::string prefix(what);
		if (header.fileSize > bytes.size())
			throw std::runtime_error(prefix + ": stream shorter than declared file size");

		const auto tableBytes = static_cast<size_t>(header.chunkCount) * sizeof(Entry);
		if (header.chunkTableOffset + tableBytes > bytes.size())
			throw std::runtime_error(prefix + ": chunk table extends past end of stream");

		reader.seek(header.chunkTableOffset);
		for (uint32_t i = 0; i < header.chunkCount; ++i)
		{
			const auto entry = reader.readPod<Entry>();
			m_Table.emplace(entry.id, entry);
		}

		m_VersionMinor = header.versionMinor;
	}

	void
	Reader::CheckEntry(const Entry& entry, size_t elementSize) const
	{
		const std::string prefix(m_What);

		if (entry.elementSize != elementSize)
			throw std::runtime_error(prefix + ": chunk element size mismatch");
		if (entry.byteSize % elementSize != 0)
			throw std::runtime_error(
				prefix + ": chunk byte size is not a multiple of the element size");
		if (entry.offset + entry.byteSize > m_Bytes.size())
			throw std::runtime_error(prefix + ": chunk extends past end of stream");
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

		// Every offset below comes out of the file, so each is checked against its real size before it is
		// seeked to: a corrupt chunkCount would otherwise size an allocation.
		const auto readAt = [&](void* dst, uint64_t bytes, uint64_t offset) {
			if (offset + bytes > fileSize)
				throw std::runtime_error(prefix + ": chunk extends past end of file");

			in.seekg(static_cast<std::streamoff>(offset));
			in.read(static_cast<char*>(dst), static_cast<std::streamsize>(bytes));
			if (!in)
				throw std::runtime_error(fileErrorMessage(prefix + ": failed to read file", path));
		};

		Header header{};
		readAt(&header, sizeof(header), 0);
		checkHeader(header, magic, versionMajor, what);

		std::vector<Entry> table(header.chunkCount);
		if (!table.empty())
			readAt(table.data(), table.size() * sizeof(Entry), header.chunkTableOffset);

		std::unordered_map<uint32_t, std::vector<std::byte>> out;
		for (const Entry& entry : table)
		{
			if (std::ranges::find(ids, entry.id) == ids.end())
				continue;

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
