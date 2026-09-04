
#include "CheckedFileReader.h"
#include "fs_util.h"
#include "ref_paths.h"
#include <algorithm>
#include <array>
#include <assetlib/codecs.h>  // requireInsideDataRoot
#include <assetlib/pak.h>
#include <assetlib_structs/magic.h>
#include <atomic>
#include <cerrno>
#include <core/err/util.h>
#include <core/file/IFileSystem.h>
#include <core/io/ByteWriter.h>
#include <core/math.h>
#include <core/platform/util.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace assetlib
{
	namespace
	{
		// Deliberately not cache::c_Align, which happens to be the same number: that one is the
		// cache containers' format parameter and this is the archive's. Sharing it would make
		// bumping one of the two silently change the other's on-disk layout.
		constexpr size_t c_Align = 16;

		constexpr std::string_view c_What = "bpak";

		struct Header
		{
			uint32_t magic;
			uint16_t versionMajor;
			uint16_t versionMinor;
			uint8_t  byteOrder;  // 0 == little-endian
			uint8_t  pad[3];
			uint32_t entryCount;
			uint64_t entryTableOffset;
			uint64_t stringPoolOffset;
			uint64_t stringPoolSize;
			uint64_t fileSize;
		};

		struct TableEntry
		{
			uint32_t pathOffset;  // into the string pool
			uint32_t pathSize;    // bytes, not counting the terminator
			uint64_t offset;      // payload, from the start of the file
			uint64_t size;
			uint64_t stampSize;
			int64_t  stampMtime;
			uint32_t flags;  // reserved: per-entry compression
			uint32_t pad;
		};

		static_assert(sizeof(Header) == 48);
		static_assert(sizeof(TableEntry) == 48);

		std::filesystem::path
		tempBeside(const std::filesystem::path& target)
		{
			static std::atomic<uint32_t> g_Counter = 0;

			return std::format(
				"{}.{}.{}.tmp",
				target.string(),
				core::process_id(),
				g_Counter.fetch_add(1, std::memory_order_relaxed));
		}
	}

	// --- PakWriter ------------------------------------------------------------------------------

	PakWriter::PakWriter(std::filesystem::path target) :
		m_Target(std::move(target)), m_Temp(tempBeside(m_Target))
	{
		createDirectories(m_Target.parent_path());

		errno = 0;
		m_Out.open(m_Temp, std::ios::binary | std::ios::trunc);
		if (!m_Out)
			throw std::runtime_error(fileErrorMessage("bpak: cannot open for writing", m_Temp));

		// Patched by Finish, once the table's offsets are known.
		const Header placeholder{};
		m_Out.write(reinterpret_cast<const char*>(&placeholder), sizeof(placeholder));
		m_Offset = sizeof(placeholder);
	}

	PakWriter::~PakWriter()
	{
		if (m_Finished)
			return;

		m_Out.close();

		std::error_code ec;
		std::filesystem::remove(m_Temp, ec);
	}

	void
	PakWriter::Add(std::string path, std::span<const std::byte> bytes, core::file::FileStamp stamp)
	{
		if (m_Finished)
			core::throw_runtime_error("bpak: Add after Finish");

		path = normalizeRef(path);
		requireInsideDataRoot("bpak", path);

		if (m_ByPath.contains(path))
			core::throw_runtime_error("bpak: '{}' was added twice", path);

		const uint64_t aligned = core::align(m_Offset, c_Align);
		if (aligned > m_Offset)
		{
			static constexpr std::array<char, c_Align> c_Padding{};
			m_Out.write(c_Padding.data(), static_cast<std::streamsize>(aligned - m_Offset));
			m_Offset = aligned;
		}

		m_Out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));

		if (!m_Out)
			throw std::runtime_error(fileErrorMessage("bpak: cannot write payload to", m_Temp));

		m_ByPath.emplace(path, static_cast<uint32_t>(m_Entries.size()));
		m_Entries.push_back(PendingEntry{ std::move(path), m_Offset, bytes.size(), stamp });
		m_Offset += bytes.size();
	}

	void
	PakWriter::Finish()
	{
		if (m_Finished)
			core::throw_runtime_error("bpak: Finish called twice");

		// Sorted by path, so the entry table and the string pool do not depend on the order Add was
		// called in. Payloads do -- they were streamed as they arrived -- so byte-for-byte identical
		// archives are AssetStore::Pack's guarantee (it walks sorted), not this writer's. Lookup is the
		// map a reader builds at mount, so the order is for determinism and for `list`, not speed.
		std::ranges::sort(m_Entries, {}, &PendingEntry::path);

		core::io::ByteWriter  pool;
		std::vector<uint32_t> pathOffsets(m_Entries.size());

		for (size_t i = 0; i < m_Entries.size(); ++i)
		{
			pathOffsets[i] = static_cast<uint32_t>(pool.Size());
			pool.WritePodArray(std::span<const char>(m_Entries[i].path));
			pool.WritePod<char>('\0');
		}

		core::io::ByteWriter table;
		for (size_t i = 0; i < m_Entries.size(); ++i)
		{
			const PendingEntry& entry = m_Entries[i];

			TableEntry out{};
			out.pathOffset = pathOffsets[i];
			out.pathSize   = static_cast<uint32_t>(entry.path.size());
			out.offset     = entry.offset;
			out.size       = entry.size;
			out.stampSize  = entry.stamp.size;
			out.stampMtime = entry.stamp.mtime;
			table.WritePod(out);
		}

		const std::vector<std::byte> poolBytes  = pool.Take();
		const std::vector<std::byte> tableBytes = table.Take();

		const uint64_t tableOffset = m_Offset;
		const uint64_t poolOffset  = tableOffset + tableBytes.size();

		m_Out.write(
			reinterpret_cast<const char*>(tableBytes.data()),
			static_cast<std::streamsize>(tableBytes.size()));
		m_Out.write(
			reinterpret_cast<const char*>(poolBytes.data()),
			static_cast<std::streamsize>(poolBytes.size()));

		Header header{};
		header.magic            = magic::c_BPak;
		header.versionMajor     = c_PakVersionMajor;
		header.versionMinor     = c_PakVersionMinor;
		header.byteOrder        = 0;
		header.entryCount       = static_cast<uint32_t>(m_Entries.size());
		header.entryTableOffset = tableOffset;
		header.stringPoolOffset = poolOffset;
		header.stringPoolSize   = poolBytes.size();
		header.fileSize         = poolOffset + poolBytes.size();

		m_Out.seekp(0);
		m_Out.write(reinterpret_cast<const char*>(&header), sizeof(header));
		m_Out.close();

		if (!m_Out)
			throw std::runtime_error(fileErrorMessage("bpak: cannot write", m_Temp));

		// The archive is the shipped artifact, not a cache entry that could simply miss, so the
		// bytes reach the device before the name does.
		if (!core::sync_file(m_Temp))
			core::throw_runtime_error("bpak: cannot flush '{}'", m_Temp.string());

		std::error_code ec;
		std::filesystem::rename(m_Temp, m_Target, ec);
		if (ec)
			core::throw_runtime_error(
				"bpak: cannot commit '{}': {}",
				m_Target.string(),
				ec.message());

		(void)core::sync_directory(m_Target.parent_path());

		m_Finished = true;
	}

	// --- PakFile --------------------------------------------------------------------------------

	PakFile::PakFile(std::filesystem::path path) : m_Path(std::move(path))
	{
		CheckedFileReader in(m_Path, c_What);

		Header header{};
		in.ReadAt(&header, sizeof(header), 0);

		if (header.magic != magic::c_BPak)
			core::throw_runtime_error("{}: not a bpak archive", c_What);
		if (header.versionMajor != c_PakVersionMajor)
			core::throw_runtime_error(
				"{}: unsupported version {}, expected {}",
				c_What,
				header.versionMajor,
				c_PakVersionMajor);
		if (header.byteOrder != 0)
			core::throw_runtime_error("{}: unsupported byte order", c_What);
		if (header.fileSize != in.GetSize())
			core::throw_runtime_error(
				"{}: header claims {} bytes, file is {}",
				c_What,
				header.fileSize,
				in.GetSize());

		// Checked before the vectors are constructed, not after: entryCount and stringPoolSize come
		// out of the file, and a crafted pair would otherwise ask for hundreds of gigabytes and
		// fail as bad_alloc rather than as the malformed-file error a caller is handling. The
		// multiplication cannot overflow -- entryCount is 32-bit and the element is 48 bytes.
		const uint64_t tableBytes = static_cast<uint64_t>(header.entryCount) * sizeof(TableEntry);
		in.CheckRange(tableBytes, header.entryTableOffset);
		in.CheckRange(header.stringPoolSize, header.stringPoolOffset);

		std::vector<TableEntry> table(header.entryCount);
		if (!table.empty())
			in.ReadAt(table.data(), tableBytes, header.entryTableOffset);

		std::vector<char> pool(static_cast<size_t>(header.stringPoolSize));
		if (!pool.empty())
			in.ReadAt(pool.data(), pool.size(), header.stringPoolOffset);

		m_Entries.reserve(table.size());

		for (const TableEntry& entry : table)
		{
			in.CheckRange(entry.size, entry.offset);

			if (entry.pathSize > pool.size() || entry.pathOffset > pool.size() - entry.pathSize)
				core::throw_runtime_error(
					"{}: an entry's path lies outside the string pool",
					c_What);

			m_Entries.push_back(
				Entry{ std::string(pool.data() + entry.pathOffset, entry.pathSize),
			           entry.offset,
			           entry.size,
			           core::file::FileStamp{ entry.stampSize, entry.stampMtime } });
		}

		for (uint32_t i = 0; i < m_Entries.size(); ++i)
			if (!m_ByPath.emplace(m_Entries[i].path, i).second)
				core::throw_runtime_error(
					"{}: '{}' appears twice in the entry table",
					c_What,
					m_Entries[i].path);
	}

	const PakFile::Entry*
	PakFile::Find(std::string_view path) const noexcept
	{
		const auto it = m_ByPath.find(path);
		return it == m_ByPath.end() ? nullptr : &m_Entries[it->second];
	}

	bool
	PakFile::Exists(std::string_view path) const noexcept
	{
		return Find(path) != nullptr;
	}

	std::optional<core::file::FileStamp>
	PakFile::Stat(std::string_view path) const noexcept
	{
		const Entry* entry = Find(path);
		return entry != nullptr ? std::optional(entry->stamp) : std::nullopt;
	}

	std::vector<std::byte>
	PakFile::Read(std::string_view path) const
	{
		const Entry* entry = Find(path);
		if (entry == nullptr)
			core::throw_runtime_error("{}: '{}' is not in the archive", c_What, path);

		return ReadRange(path, 0, entry->size);
	}

	std::vector<std::byte>
	PakFile::ReadRange(std::string_view path, uint64_t offset, uint64_t size) const
	{
		const Entry* entry = Find(path);
		if (entry == nullptr)
			core::throw_runtime_error("{}: '{}' is not in the archive", c_What, path);

		if (size > entry->size || offset > entry->size - size)
			core::throw_runtime_error(
				"{}: range [{}, {}) extends past the end of '{}' ({} bytes)",
				c_What,
				offset,
				offset + size,
				path,
				entry->size);

		std::vector<std::byte> out(size);
		if (out.empty())
			return out;

		// A stream per call rather than one handle: a shared handle carries a shared seek position,
		// and two decode threads reading two textures out of one archive is the ordinary case.
		errno = 0;
		std::ifstream in(m_Path, std::ios::binary);
		if (!in)
			throw std::runtime_error(fileErrorMessage("bpak: cannot open for reading", m_Path));

		in.seekg(static_cast<std::streamoff>(entry->offset + offset));
		if (!in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)))
			throw std::runtime_error(fileErrorMessage("bpak: failed to read", m_Path));

		return out;
	}

	std::vector<std::string>
	PakFile::Enumerate(std::string_view prefix) const
	{
		std::vector<std::string> out;

		for (const Entry& entry : m_Entries)
		{
			if (!prefix.empty() &&
			    !(entry.path.starts_with(prefix) && entry.path.size() > prefix.size() &&
			      entry.path[prefix.size()] == '/'))
				continue;

			out.push_back(entry.path);
		}

		return out;
	}
}
