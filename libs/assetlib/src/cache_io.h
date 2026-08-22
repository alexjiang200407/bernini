#pragma once
#include <core/file/IFileSystem.h>

#include "IRangeReader.h"
#include <assetlib_structs/SourceRef.h>
#include <core/err/util.h>
#include <core/io/ByteWriter.h>

namespace assetlib::cache
{
	/**
	 * The container format the derived geometry (`.bmesh`, `.bskel`, `.banim`) is a cache entry
	 * in: a frozen header carrying the cache key, a run of 16-byte-aligned chunks of raw
	 * current-layout structs, and a chunk table at the end. Chunks are addressed by id; an absent
	 * one is not a malformed file.
	 *
	 * The header and table are the one layout kept forever. The chunks have no self-description:
	 * a token that matches means the current code wrote them, so the fixed typed reader reads them
	 * raw; a token that does not match is a cache miss — the file is stale and its only future is
	 * regeneration from its source. There is nothing to convert and no old shape to parse.
	 */

	inline constexpr size_t   c_Align         = 16;
	inline constexpr uint32_t c_HeaderVersion = 1;

	/** Frozen forever. `sourceKeyLength` bytes of the source's mount key follow immediately. */
	struct Header
	{
		uint32_t magic;
		uint32_t headerVersion;
		uint64_t bakeToken;       // the writer's engine constant; any mismatch is a cache miss
		uint64_t parametersHash;  // the import document's parameter subtree, hashed
		uint64_t sourceSize;      // SourceStamp of the copied source; zeros when never recorded
		uint64_t sourceHash;
		uint32_t sourceKeyLength;
		uint32_t chunkCount;
		uint64_t chunkTableOffset;
		uint64_t fileSize;
	};

	static_assert(sizeof(Header) == 64);

	/** Frozen forever. */
	struct Entry
	{
		uint32_t id;
		uint32_t elementSize;
		uint64_t offset;
		uint64_t byteSize;
	};

	static_assert(sizeof(Entry) == 24);

	template <typename T>
	concept CacheElement = std::is_trivially_copyable_v<std::remove_cvref_t<T>>;

	/** A container's scoped ChunkId enum; the id stored on disk is its underlying value. */
	template <typename Id>
	concept ChunkIdType = std::is_enum_v<Id> && std::same_as<std::underlying_type_t<Id>, uint32_t>;

	/** Accumulates chunks behind a placeholder header, which Finish patches. */
	class Writer
	{
	public:
		template <CacheElement T, ChunkIdType Id>
		void
		Add(Id id, std::span<const T> values)
		{
			AddBytes(
				static_cast<uint32_t>(id),
				static_cast<uint32_t>(sizeof(T)),
				std::as_bytes(values));
		}

		template <CacheElement T, ChunkIdType Id>
		void
		Add(Id id, const std::vector<T>& values)
		{
			Add(id, std::span<const T>(values));
		}

		/**
		 * The finished container. The stored bake token is always the engine's constant --
		 * never a value carried in from a loaded struct, which is what keeps a synthetic
		 * fixture current by construction.
		 */
		[[nodiscard]] std::vector<std::byte>
		Finish(uint32_t magic, uint64_t bakeToken, const SourceRef& source);

	private:
		void
		AddBytes(uint32_t id, uint32_t elementSize, std::span<const std::byte> bytes);

		core::io::ByteWriter m_Bytes;
		std::vector<Entry>   m_Chunks;
	};

	/** A byte stream's validated header, key and chunk table, with the stream it indexes. */
	class Reader
	{
	public:
		/**
		 * @param what Container name the error messages are prefixed with, e.g. "bmesh".
		 * @throws std::runtime_error on bad magic, a header version newer than this build, a
		 *         stream that disagrees with its own sizes -- and on a bake token that is not
		 *         `bakeToken`: the file is stale (or a sibling branch's, or newer -- one case),
		 *         and the message says to regenerate it from its source.
		 */
		Reader(
			std::span<const std::byte> bytes,
			uint32_t                   magic,
			uint64_t                   bakeToken,
			std::string_view           what);

		/** The chunk's elements, or an empty vector when the container does not carry it. */
		template <CacheElement T, ChunkIdType Id>
		[[nodiscard]] std::vector<T>
		Read(Id id) const
		{
			const Entry* entry = Find(static_cast<uint32_t>(id));
			return entry == nullptr ? std::vector<T>() : ReadEntry<T>(*entry);
		}

		/** @throws std::runtime_error if the chunk is absent. */
		template <CacheElement T, ChunkIdType Id>
		[[nodiscard]] std::vector<T>
		Require(Id id) const
		{
			const Entry* entry = Find(static_cast<uint32_t>(id));
			if (entry == nullptr)
				core::throw_runtime_error(
					"{}: missing required chunk {}",
					m_What,
					static_cast<uint32_t>(id));
			return ReadEntry<T>(*entry);
		}

		[[nodiscard]] const SourceRef&
		GetSource() const noexcept
		{
			return m_Source;
		}

	private:
		template <CacheElement T>
		[[nodiscard]] std::vector<T>
		ReadEntry(const Entry& entry) const
		{
			core::throw_runtime_error_if(
				entry.elementSize != sizeof(T),
				"{}: chunk {} stores {}-byte elements where {} are expected -- the file is "
				"malformed for this build; regenerate it from its source",
				m_What,
				entry.id,
				entry.elementSize,
				sizeof(T));
			std::vector<T> values(entry.byteSize / sizeof(T));
			std::copy_n(
				m_Bytes.data() + entry.offset,
				values.size() * sizeof(T),
				reinterpret_cast<std::byte*>(values.data()));
			return values;
		}

		[[nodiscard]] const Entry*
		Find(uint32_t id) const noexcept;

		std::span<const std::byte> m_Bytes;
		Header                     m_Header{};
		SourceRef                  m_Source;
		std::vector<Entry>         m_Table;
		std::string_view           m_What;
	};

	/** A container's key, read without its payload: the token it was written at, and its source. */
	struct PeekedKey
	{
		uint64_t  bakeToken = 0;
		SourceRef source;
	};

	/**
	 * The header and source key alone -- two small ranged reads, so a staleness survey of a whole
	 * project never fetches a payload.
	 * @throws std::runtime_error on bad magic or a malformed header; a foreign bake token is NOT
	 *         an error here -- reporting it is the caller's whole purpose.
	 */
	[[nodiscard]] PeekedKey
	peekKey(IRangeReader& source, uint32_t magic, std::string_view what);

	/** peekKey over bytes already in memory. */
	[[nodiscard]] PeekedKey
	peekKey(std::span<const std::byte> bytes, uint32_t magic, std::string_view what);

	/** One named chunk's raw bytes and element size, from `readCacheChunks`. */
	struct CacheSlot
	{
		uint32_t               id          = 0;
		uint32_t               elementSize = 0;
		std::vector<std::byte> bytes;
	};

	/** The named chunks of one container, and its key -- a survey's view. */
	class CacheData
	{
	public:
		PeekedKey              key;
		std::vector<CacheSlot> slots;

		template <CacheElement T, ChunkIdType Id>
		[[nodiscard]] std::vector<T>
		Read(Id id, std::string_view what) const
		{
			for (const CacheSlot& slot : slots)
			{
				if (slot.id != static_cast<uint32_t>(id))
					continue;
				core::throw_runtime_error_if(
					slot.elementSize != sizeof(T),
					"{}: chunk {} stores {}-byte elements where {} are expected",
					what,
					slot.id,
					slot.elementSize,
					sizeof(T));
				std::vector<T> values(slot.bytes.size() / sizeof(T));
				std::copy_n(
					slot.bytes.data(),
					values.size() * sizeof(T),
					reinterpret_cast<std::byte*>(values.data()));
				return values;
			}
			return {};
		}
	};

	/**
	 * The bytes of the named chunks of `source`, and nothing else: the header, the source key,
	 * the chunk table and each requested chunk are the only reads -- a survey of every container
	 * in a project reads a few hundred bytes of a file of many megabytes.
	 *
	 * @throws std::runtime_error if the source cannot be read, is malformed, or was written at a
	 *         bake token other than `bakeToken`.
	 */
	[[nodiscard]] CacheData
	readCacheChunks(
		IRangeReader&             source,
		uint32_t                  magic,
		uint64_t                  bakeToken,
		std::span<const uint32_t> ids,
		std::string_view          what);

	/** readCacheChunks against a file on disk: one open, one seek per chunk. */
	[[nodiscard]] CacheData
	readCacheChunksFromFile(
		const std::filesystem::path& path,
		uint32_t                     magic,
		uint64_t                     bakeToken,
		std::span<const uint32_t>    ids,
		std::string_view             what);

	/** readCacheChunks against a mounted filesystem, which may be an archive. */
	[[nodiscard]] CacheData
	readCacheChunksFrom(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		uint32_t                       magic,
		uint64_t                       bakeToken,
		std::span<const uint32_t>      ids,
		std::string_view               what);
}
