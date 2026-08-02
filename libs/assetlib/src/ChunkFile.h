#pragma once
#include <core/io/ByteWriter.h>
#include <core/type_traits.h>

namespace assetlib
{
	/**
	 * The fixed prefix a chunked container opens with. `.bmesh` and `.benv` both begin with these 20
	 * bytes and follow them with fields of their own, so each keeps a header struct that embeds this
	 * one rather than restating it.
	 */
	struct ChunkHeader
	{
		uint32_t magic;
		uint16_t versionMajor;
		uint16_t versionMinor;
		uint8_t  byteOrder;  // 0 == little-endian
		uint8_t  pad[3];
		uint32_t chunkCount;
		uint32_t chunkTableOffset;
	};

	static_assert(sizeof(ChunkHeader) == 20);

	/**
	 * One row of the chunk table: where a chunk's bytes are and what they are made of.
	 *
	 * `elementSize` is the stride of a typed chunk, or 0 for one holding an opaque blob. A reader that
	 * means to reinterpret the bytes as an array checks it first, so a chunk whose element type
	 * changed is refused rather than misread.
	 */
	struct ChunkEntry
	{
		uint32_t id;
		uint32_t elementSize;
		uint64_t offset;
		uint64_t byteSize;
	};

	static_assert(sizeof(ChunkEntry) == 24);

	/** Chunks start on this boundary, so a typed chunk stays aligned for a read in place. */
	constexpr size_t c_ChunkAlign = 16;

	/** A container's chunk-id enum. Each format numbers its own chunks; the table is agnostic. */
	template <typename T>
	concept ChunkTag = std::is_enum_v<T>;

	/**
	 * Whether `byteSize` bytes at `offset` lie within `total`.
	 *
	 * Subtracts rather than adds: both operands come straight out of a file, so `offset + byteSize`
	 * can wrap back under the bound while still naming a region far past the stream.
	 */
	[[nodiscard]] constexpr bool
	fitsWithin(uint64_t offset, uint64_t byteSize, uint64_t total) noexcept
	{
		return offset <= total && byteSize <= total - offset;
	}

	/**
	 * Checks the header prefix against what this build writes.
	 *
	 * @param context Prefixed to any message thrown, naming the container -- "bmesh", or a path.
	 * @throws std::runtime_error on a foreign magic, a major version this build does not read, or a
	 *         stream that is not little-endian.
	 */
	void
	validateHeader(
		const ChunkHeader& header,
		uint32_t           magic,
		uint16_t           versionMajor,
		std::string_view   context);

	/** Appends `values` on the next chunk boundary and returns the entry naming them. */
	template <ChunkTag Id, core::type_traits::trivially_copyable T>
	[[nodiscard]] ChunkEntry
	appendChunk(core::io::ByteWriter& writer, Id id, const std::vector<T>& values)
	{
		writer.alignTo(c_ChunkAlign);

		ChunkEntry entry{};
		entry.id          = static_cast<uint32_t>(id);
		entry.elementSize = static_cast<uint32_t>(sizeof(T));
		entry.offset      = writer.size();
		entry.byteSize    = values.size() * sizeof(T);
		writer.writePodArray(std::span<const T>(values));
		return entry;
	}

	/** Appends an opaque blob on the next chunk boundary, leaving its `elementSize` 0. */
	template <ChunkTag Id>
	[[nodiscard]] ChunkEntry
	appendBlob(core::io::ByteWriter& writer, Id id, std::span<const std::byte> bytes)
	{
		writer.alignTo(c_ChunkAlign);

		ChunkEntry entry{};
		entry.id       = static_cast<uint32_t>(id);
		entry.offset   = writer.size();
		entry.byteSize = bytes.size();
		writer.writeBytes(bytes);
		return entry;
	}

	/**
	 * The chunk table of a container held in memory, and bounds-checked access to what it names.
	 *
	 * Every offset and size in the table came out of the file, so none of them is trusted: a chunk is
	 * sliced only once it has been proven to lie inside the stream. Chunks are looked up by id rather
	 * than read in table order, so a container may carry them in any order and may carry ones this
	 * build does not know.
	 */
	class ChunkTable
	{
	public:
		/**
		 * @param context Prefixed to any message thrown, naming the container.
		 * @throws std::runtime_error if the table `header` names does not lie within `bytes`.
		 */
		ChunkTable(
			std::span<const std::byte> bytes,
			const ChunkHeader&         header,
			std::string_view           context);

		/** The entry for `id`, or nullptr if the container does not carry that chunk. */
		template <ChunkTag Id>
		[[nodiscard]] const ChunkEntry*
		Find(Id id) const noexcept
		{
			const auto it = m_Entries.find(static_cast<uint32_t>(id));
			return it == m_Entries.end() ? nullptr : &it->second;
		}

		/**
		 * The chunk's elements.
		 *
		 * @throws std::runtime_error if the chunk is absent, runs past the stream, or is not made of
		 *         `T`-sized elements.
		 */
		template <core::type_traits::trivially_copyable T, ChunkTag Id>
		[[nodiscard]] std::vector<T>
		Read(Id id) const
		{
			const ChunkEntry* entry = Find(id);
			if (entry == nullptr)
				ThrowMissing(static_cast<uint32_t>(id));
			return Elements<T>(*entry);
		}

		/** As Read, but a chunk the container does not carry reads as no elements rather than throwing. */
		template <core::type_traits::trivially_copyable T, ChunkTag Id>
		[[nodiscard]] std::vector<T>
		ReadOptional(Id id) const
		{
			const ChunkEntry* entry = Find(id);
			return entry == nullptr ? std::vector<T>() : Elements<T>(*entry);
		}

		/**
		 * The chunk's bytes, uninterpreted.
		 *
		 * @throws std::runtime_error if the chunk is absent or runs past the stream.
		 */
		template <ChunkTag Id>
		[[nodiscard]] std::span<const std::byte>
		Blob(Id id) const
		{
			const ChunkEntry* entry = Find(id);
			if (entry == nullptr)
				ThrowMissing(static_cast<uint32_t>(id));
			return Bytes(*entry);
		}

	private:
		[[noreturn]] void
		ThrowMissing(uint32_t id) const;

		[[nodiscard]] std::span<const std::byte>
		Bytes(const ChunkEntry& entry) const;

		void
		RequireElements(const ChunkEntry& entry, size_t elementSize) const;

		template <core::type_traits::trivially_copyable T>
		[[nodiscard]] std::vector<T>
		Elements(const ChunkEntry& entry) const
		{
			RequireElements(entry, sizeof(T));

			const auto     raw = Bytes(entry);
			std::vector<T> out(raw.size() / sizeof(T));
			std::copy_n(raw.data(), raw.size(), reinterpret_cast<std::byte*>(out.data()));
			return out;
		}

		std::span<const std::byte>               m_Bytes;
		std::string                              m_Context;
		std::unordered_map<uint32_t, ChunkEntry> m_Entries;
	};
}
