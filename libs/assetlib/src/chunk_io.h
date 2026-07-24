#pragma once
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace assetlib::chunk
{
	/**
	 * The container format `.bmesh`, `.bskel` and `.banim` share: a fixed header, a run of
	 * 16-byte-aligned chunks, and a chunk table at the end. Chunks are addressed by id, so an
	 * absent one is not a malformed file -- which is what lets a minor version add data without
	 * invalidating what is already on disk.
	 */

	inline constexpr size_t c_Align = 16;

	struct Header
	{
		uint32_t magic;
		uint16_t versionMajor;
		uint16_t versionMinor;
		uint8_t  byteOrder;  // 0 == little-endian
		uint8_t  pad[3];
		uint32_t chunkCount;
		uint32_t chunkTableOffset;
		uint64_t fileSize;
	};

	static_assert(sizeof(Header) == 32);

	struct Entry
	{
		uint32_t id;
		uint32_t elementSize;
		uint64_t offset;
		uint64_t byteSize;
	};

	static_assert(sizeof(Entry) == 24);

	/** Accumulates chunks behind a placeholder header, which Finish patches. */
	class Writer
	{
	public:
		Writer() { m_Bytes.writePod(Header{}); }

		template <typename T>
		void
		Add(uint32_t id, const std::vector<T>& values)
		{
			m_Bytes.alignTo(c_Align);

			Entry entry{};
			entry.id          = id;
			entry.elementSize = static_cast<uint32_t>(sizeof(T));
			entry.offset      = m_Bytes.size();
			entry.byteSize    = values.size() * sizeof(T);
			m_Bytes.writePodArray(std::span<const T>(values));
			m_Chunks.push_back(entry);
		}

		[[nodiscard]] std::vector<std::byte>
		Finish(uint32_t magic, uint16_t versionMajor, uint16_t versionMinor);

	private:
		core::io::ByteWriter m_Bytes;
		std::vector<Entry>   m_Chunks;
	};

	/** A byte stream's validated header and chunk table, with the stream it indexes. */
	class Reader
	{
	public:
		/**
		 * @param what Container name the error messages are prefixed with, e.g. "bmesh".
		 * @throws std::runtime_error on bad magic, an unsupported major version or byte order, or a
		 *         stream shorter than the header claims.
		 */
		Reader(
			std::span<const std::byte> bytes,
			uint32_t                   magic,
			uint16_t                   versionMajor,
			std::string_view           what);

		[[nodiscard]] uint16_t
		VersionMinor() const noexcept
		{
			return m_VersionMinor;
		}

		/** The chunk's elements, or an empty vector when the container does not carry it. */
		template <typename T>
		[[nodiscard]] std::vector<T>
		Read(uint32_t id) const
		{
			const auto it = m_Table.find(id);
			return it == m_Table.end() ? std::vector<T>() : ReadEntry<T>(it->second);
		}

		/** @throws std::runtime_error if the chunk is absent. */
		template <typename T>
		[[nodiscard]] std::vector<T>
		Require(uint32_t id) const
		{
			const auto it = m_Table.find(id);
			if (it == m_Table.end())
				throw std::runtime_error(std::string(m_What) + ": missing required chunk");
			return ReadEntry<T>(it->second);
		}

	private:
		template <typename T>
		[[nodiscard]] std::vector<T>
		ReadEntry(const Entry& entry) const
		{
			CheckEntry(entry, sizeof(T));

			std::vector<T> out(entry.byteSize / sizeof(T));
			std::copy_n(
				m_Bytes.data() + entry.offset,
				entry.byteSize,
				reinterpret_cast<std::byte*>(out.data()));
			return out;
		}

		void
		CheckEntry(const Entry& entry, size_t elementSize) const;

		std::span<const std::byte>          m_Bytes;
		std::unordered_map<uint32_t, Entry> m_Table;
		std::string_view                    m_What;
		uint16_t                            m_VersionMinor = 0;
	};

	/**
	 * The bytes of the named chunks of `path`, and nothing else: the header, the chunk table and
	 * each requested chunk are the only reads. A survey of every container in a project has to come
	 * through here -- the chunks it wants are a few hundred bytes of a file of many megabytes.
	 *
	 * Chunks the file does not carry are simply absent from the result.
	 *
	 * @throws std::runtime_error if the file cannot be read, or is malformed.
	 */
	[[nodiscard]] std::unordered_map<uint32_t, std::vector<std::byte>>
	readChunksFromFile(
		const std::filesystem::path& path,
		uint32_t                     magic,
		uint16_t                     versionMajor,
		std::span<const uint32_t>    ids,
		std::string_view             what);

	/** A list of strings as one blob of NUL-terminated bytes (one terminator per string). */
	[[nodiscard]] std::vector<char>
	packStrings(std::span<const std::string> strings);

	[[nodiscard]] std::vector<std::string>
	unpackStrings(std::span<const char> pool);
}
