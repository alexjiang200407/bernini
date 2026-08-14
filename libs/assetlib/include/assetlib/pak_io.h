#pragma once
#include <core/file/IFileSystem.h>
#include <core/str/str.h>

namespace assetlib
{
	/** What `.bpak` was written by, so a reader can say which. */
	inline constexpr uint16_t c_PakVersionMajor = 1;
	inline constexpr uint16_t c_PakVersionMinor = 0;

	/**
	 * Writes a `.bpak`: a header, 16-byte-aligned payloads, an entry table and a string pool.
	 *
	 * Streams. Each Add writes its payload to a temp file straight away, so only the table and the
	 * pool are held -- an archive is the whole project's cooked output, and building one in memory
	 * to write it would put a ceiling on how big a project can be packed.
	 *
	 * The temp is committed by Finish and removed by the destructor otherwise, so an abandoned or
	 * failed pack leaves whatever was at the target untouched.
	 */
	class PakWriter
	{
	public:
		/** @throws std::runtime_error if a temp file beside `target` cannot be opened. */
		explicit PakWriter(std::filesystem::path target);

		/** Removes the temp unless Finish committed it. */
		~PakWriter();

		PakWriter(const PakWriter&) = delete;
		PakWriter(PakWriter&&)      = delete;
		PakWriter&
		operator=(const PakWriter&) = delete;
		PakWriter&
		operator=(PakWriter&&) = delete;

		/**
		 * Adds one entry under `path`, which must be a normalized data-root-relative path -- the
		 * same string a reference in a container carries, and the key a reader will ask for.
		 *
		 * @throws std::runtime_error if `path` is empty, escapes the data root, or was already
		 *         added, or if the payload cannot be written.
		 */
		void
		Add(std::string path, std::span<const std::byte> bytes, core::file::FileStamp stamp);

		[[nodiscard]] size_t
		GetEntryCount() const noexcept
		{
			return m_Entries.size();
		}

		/**
		 * Writes the table, commits the archive over `target`, and leaves this writer spent -- a
		 * second Finish throws.
		 *
		 * Entries are sorted by path, so packing one tree twice produces identical bytes.
		 *
		 * @throws std::runtime_error if the archive cannot be written, flushed or renamed.
		 */
		void
		Finish();

	private:
		struct PendingEntry
		{
			std::string           path;
			uint64_t              offset = 0;
			uint64_t              size   = 0;
			core::file::FileStamp stamp;
		};

		std::filesystem::path     m_Target;
		std::filesystem::path     m_Temp;
		std::ofstream             m_Out;
		uint64_t                  m_Offset = 0;
		std::vector<PendingEntry> m_Entries;

		core::str::unordered_str_map<uint32_t> m_ByPath;

		bool m_Finished = false;
	};

	/**
	 * A mounted `.bpak`, read by the same data-root-relative paths a directory is.
	 *
	 * The header, entry table and string pool are read once at construction; a payload is read on
	 * demand, since the archive is far larger than a process wants resident. Reads open their own
	 * stream, so the concurrency IFileSystem promises costs no lock -- a shared handle would carry
	 * a shared seek position, and two decode threads reading two textures is the ordinary case.
	 *
	 * Always read-only. Editing a packed asset writes a loose file that shadows it; `pack` is what
	 * produces a new archive.
	 */
	class PakFile final : public core::file::IFileSystem
	{
	public:
		/**
		 * @throws std::runtime_error on bad magic, an unsupported major version or byte order, a
		 *         truncated file, or a table whose offsets do not lie inside it.
		 */
		explicit PakFile(std::filesystem::path path);

		PakFile(const PakFile&) = delete;
		PakFile(PakFile&&)      = delete;
		PakFile&
		operator=(const PakFile&) = delete;
		PakFile&
		operator=(PakFile&&) = delete;

		[[nodiscard]] const std::filesystem::path&
		GetPath() const noexcept
		{
			return m_Path;
		}

		[[nodiscard]] bool
		Exists(std::string_view path) const noexcept override;

		[[nodiscard]] std::optional<core::file::FileStamp>
		Stat(std::string_view path) const noexcept override;

		[[nodiscard]] std::vector<std::byte>
		Read(std::string_view path) const override;

		[[nodiscard]] std::vector<std::byte>
		ReadRange(std::string_view path, uint64_t offset, uint64_t size) const override;

		[[nodiscard]] std::vector<std::string>
		Enumerate(std::string_view prefix = {}) const override;

		[[nodiscard]] bool
		IsReadOnly() const noexcept override
		{
			return true;
		}

	private:
		struct Entry
		{
			std::string           path;
			uint64_t              offset = 0;
			uint64_t              size   = 0;
			core::file::FileStamp stamp;
		};

		[[nodiscard]] const Entry*
		Find(std::string_view path) const noexcept;

		std::filesystem::path m_Path;
		std::vector<Entry>    m_Entries;

		core::str::unordered_str_map<uint32_t> m_ByPath;
	};
}
