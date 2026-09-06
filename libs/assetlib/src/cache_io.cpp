#include "cache_io.h"

#include "CheckedFileReader.h"
#include "IRangeReader.h"
#include "MountedFileReader.h"
#include <assetlib_structs/SourceRef.h>
#include <core/err/util.h>
#include <core/file/IFileSystem.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

namespace assetlib::cache
{
	namespace
	{
		Header
		readHeader(IRangeReader& source, uint32_t magic, std::string_view what)
		{
			Header header{};
			core::throw_runtime_error_if(
				source.GetSize() < sizeof(Header),
				"{}: {} bytes is shorter than a cache header",
				what,
				source.GetSize());
			source.ReadAt(&header, sizeof(Header), 0);
			core::throw_runtime_error_if(
				header.magic != magic,
				"{}: not this container's magic",
				what);
			// A chunk-era file carries a version pair where this field sits, so a mismatch here
			// cannot claim "newer" -- it may equally be a file from before the cache format.
			core::throw_runtime_error_if(
				header.headerVersion != c_HeaderVersion,
				"{}: cache header version {} is not the {} this build reads -- a newer build's "
				"entry, or a file from before the cache format; regenerate it from its source",
				what,
				header.headerVersion,
				c_HeaderVersion);
			core::throw_runtime_error_if(
				header.fileSize != source.GetSize(),
				"{}: header claims {} bytes, the file holds {} -- truncated or corrupt; "
				"regenerate it from its source",
				what,
				header.fileSize,
				source.GetSize());
			return header;
		}

		SourceRef
		readSource(IRangeReader& reader, const Header& header)
		{
			SourceRef source;
			source.stamp.size     = header.sourceSize;
			source.stamp.hash     = header.sourceHash;
			source.parametersHash = header.parametersHash;
			if (header.sourceKeyLength > 0)
			{
				reader.CheckRange(header.sourceKeyLength, sizeof(Header));
				source.key.resize(header.sourceKeyLength);
				reader.ReadAt(source.key.data(), header.sourceKeyLength, sizeof(Header));
			}
			return source;
		}

		std::vector<Entry>
		readTable(IRangeReader& source, const Header& header, std::string_view what)
		{
			const uint64_t tableBytes = uint64_t(header.chunkCount) * sizeof(Entry);
			source.CheckRange(tableBytes, header.chunkTableOffset);
			std::vector<Entry> table(header.chunkCount);
			source.ReadAt(table.data(), tableBytes, header.chunkTableOffset);
			for (const Entry& entry : table)
			{
				source.CheckRange(entry.byteSize, entry.offset);
				core::throw_runtime_error_if(
					entry.elementSize == 0 || entry.byteSize % entry.elementSize != 0,
					"{}: chunk {} is not a whole number of {}-byte elements",
					what,
					entry.id,
					entry.elementSize);
			}
			return table;
		}
	}

	void
	Writer::AddBytes(uint32_t id, uint32_t elementSize, std::span<const std::byte> bytes)
	{
		m_Bytes.AlignTo(c_Align);

		Entry entry{};
		entry.id          = id;
		entry.elementSize = elementSize;
		entry.offset      = m_Bytes.Size();  // patched by Finish: relative to the payload start
		entry.byteSize    = bytes.size();
		m_Chunks.push_back(entry);

		m_Bytes.WriteBytes(bytes);
	}

	std::vector<std::byte>
	Writer::Finish(uint32_t magic, uint64_t bakeToken, const SourceRef& source)
	{
		Header header{};
		header.magic           = magic;
		header.headerVersion   = c_HeaderVersion;
		header.bakeToken       = bakeToken;
		header.parametersHash  = source.parametersHash;
		header.sourceSize      = source.stamp.size;
		header.sourceHash      = source.stamp.hash;
		header.sourceKeyLength = static_cast<uint32_t>(source.key.size());
		header.chunkCount      = static_cast<uint32_t>(m_Chunks.size());

		core::io::ByteWriter out;
		out.WritePod(header);  // placeholder; patched once the offsets are known
		out.WriteBytes(std::as_bytes(std::span(source.key.data(), source.key.size())));
		out.AlignTo(c_Align);

		const size_t payloadStart = out.Size();
		const auto   payload      = m_Bytes.Take();
		out.WriteBytes(payload);

		out.AlignTo(c_Align);
		header.chunkTableOffset = out.Size();
		for (Entry entry : m_Chunks)
		{
			entry.offset += payloadStart;
			out.WritePod(entry);
		}

		header.fileSize = out.Size();
		out.PatchPod(0, header);
		return out.Take();
	}

	namespace
	{
		/** IRangeReader over bytes already in memory, so one validation serves both entries. */
		class SpanReader final : public IRangeReader
		{
		public:
			SpanReader(std::span<const std::byte> bytes, std::string_view what) :
				IRangeReader(what), m_Bytes(bytes)
			{}

			SpanReader(const SpanReader&) = delete;
			SpanReader(SpanReader&&)      = delete;
			SpanReader&
			operator=(const SpanReader&) = delete;
			SpanReader&
			operator=(SpanReader&&) = delete;

			[[nodiscard]] uint64_t
			GetSize() const noexcept override
			{
				return m_Bytes.size();
			}

			void
			ReadAt(void* destination, uint64_t bytes, uint64_t offset) override
			{
				CheckRange(bytes, offset);
				std::copy_n(m_Bytes.data() + offset, bytes, static_cast<std::byte*>(destination));
			}

		private:
			std::span<const std::byte> m_Bytes;
		};
	}

	Reader::Reader(
		std::span<const std::byte> bytes,
		uint32_t                   magic,
		uint64_t                   bakeToken,
		std::string_view           what) : m_Bytes(bytes), m_What(what)
	{
		SpanReader reader(bytes, what);
		m_Header = readHeader(reader, magic, what);
		core::throw_runtime_error_if(
			m_Header.bakeToken != bakeToken,
			"{}: written at another bake revision -- a cache miss, whether stale, newer, or a "
			"sibling branch's; regenerate it from its source",
			what);
		m_Source = readSource(reader, m_Header);
		m_Table  = readTable(reader, m_Header, what);
	}

	const Entry*
	Reader::Find(uint32_t id) const noexcept
	{
		for (const Entry& entry : m_Table)
			if (entry.id == id)
				return &entry;
		return nullptr;
	}

	bool
	isCacheEntry(std::span<const std::byte> bytes) noexcept
	{
		if (bytes.size() < sizeof(Header))
			return false;

		Header header{};
		std::memcpy(&header, bytes.data(), sizeof(header));
		return header.headerVersion == c_HeaderVersion && header.fileSize == bytes.size();
	}

	PeekedKey
	peekKey(IRangeReader& source, uint32_t magic, std::string_view what)
	{
		const Header header = readHeader(source, magic, what);
		PeekedKey    key;
		key.bakeToken = header.bakeToken;
		key.source    = readSource(source, header);
		return key;
	}

	PeekedKey
	peekKey(std::span<const std::byte> bytes, uint32_t magic, std::string_view what)
	{
		SpanReader reader(bytes, what);
		return peekKey(reader, magic, what);
	}

	CacheData
	readCacheChunks(
		IRangeReader&             source,
		uint32_t                  magic,
		uint64_t                  bakeToken,
		std::span<const uint32_t> ids,
		std::string_view          what)
	{
		ZoneScopedN("assetlib cache chunks");
		ZoneTextF("%.*s, %zu chunks", static_cast<int>(what.size()), what.data(), ids.size());

		const Header header = readHeader(source, magic, what);
		core::throw_runtime_error_if(
			header.bakeToken != bakeToken,
			"{}: written at another bake revision -- a cache miss, whether stale, newer, or a "
			"sibling branch's; regenerate it from its source",
			what);

		CacheData data;
		data.key.bakeToken = header.bakeToken;
		data.key.source    = readSource(source, header);

		for (const Entry& entry : readTable(source, header, what))
		{
			if (std::ranges::find(ids, entry.id) == ids.end())
				continue;
			CacheSlot slot;
			slot.id          = entry.id;
			slot.elementSize = entry.elementSize;
			slot.bytes.resize(entry.byteSize);
			source.ReadAt(slot.bytes.data(), entry.byteSize, entry.offset);
			data.slots.push_back(std::move(slot));
		}
		return data;
	}

	CacheData
	readCacheChunksFromFile(
		const std::filesystem::path& path,
		uint32_t                     magic,
		uint64_t                     bakeToken,
		std::span<const uint32_t>    ids,
		std::string_view             what)
	{
		CheckedFileReader reader(path, what);
		return readCacheChunks(reader, magic, bakeToken, ids, what);
	}

	CacheData
	readCacheChunksFrom(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		uint32_t                       magic,
		uint64_t                       bakeToken,
		std::span<const uint32_t>      ids,
		std::string_view               what)
	{
		MountedFileReader reader(fileSystem, path, what);
		return readCacheChunks(reader, magic, bakeToken, ids, what);
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
