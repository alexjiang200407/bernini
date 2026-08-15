#pragma once

namespace core::file
{
	/**
	 * A file's size and modification time, which is what a caller compares to decide whether what it
	 * holds still describes the file.
	 *
	 * Deliberately not `assetlib::SourceStamp`, which is the same two fields serialized into asset
	 * containers: that is a file format, and it is defined in a library above this one.
	 */
	struct FileStamp
	{
		uint64_t size  = 0;
		int64_t  mtime = 0;  // seconds since the filesystem clock's epoch

		[[nodiscard]] bool
		operator==(const FileStamp&) const noexcept = default;
	};

	/**
	 * Read access to a set of files addressed by one normalized relative path, so that a directory
	 * on disk and an archive are the same thing to a caller.
	 *
	 * Paths are data-root-relative, `/`-separated and already normalized (`assetlib::normalizeRef`
	 * form). An implementation does not normalize what it is given: two spellings of one path are
	 * two paths here, which is why every producer of a path funnels through one normalizer.
	 *
	 * They are `std::string_view` and never `std::filesystem::path` for that reason -- a key that has
	 * been through `path` arrives `\`-separated on Windows, which an archive's byte-for-byte lookup
	 * misses while a directory still resolves it. See STYLE.md's Paths section.
	 *
	 * **Every method is safe to call concurrently on one instance.** An archive is a plausible place
	 * to keep a file handle open across reads, and the editor already decodes two textures on two
	 * threads at once, so an implementation that keeps a handle must read positionally rather than
	 * seek it.
	 */
	class IFileSystem
	{
	public:
		virtual ~IFileSystem() = default;

		// Held by pointer and never by value, so all four are declared rather than left implicit --
		// MSVC warns (as an error here) about every special member a derived class inherits as
		// implicitly deleted.
		IFileSystem()                   = default;
		IFileSystem(const IFileSystem&) = delete;
		IFileSystem(IFileSystem&&)      = delete;
		IFileSystem&
		operator=(const IFileSystem&) = delete;
		IFileSystem&
		operator=(IFileSystem&&) = delete;

		[[nodiscard]] virtual bool
		Exists(std::string_view path) const noexcept = 0;

		/** Nullopt when `path` is absent, which is not an error. */
		[[nodiscard]] virtual std::optional<FileStamp>
		Stat(std::string_view path) const noexcept = 0;

		/** @throws std::runtime_error if `path` is absent or cannot be read. */
		[[nodiscard]] virtual std::vector<std::byte>
		Read(std::string_view path) const = 0;

		/**
		 * `size` bytes of `path` from `offset`, and only those bytes.
		 *
		 * The reason this is on the interface rather than layered over Read: a container's chunk
		 * table is a few hundred bytes of a file of many megabytes, and a survey of every mesh in a
		 * project reads exactly that much of each. Served by a whole-file read, such a survey costs
		 * a full read of the project.
		 *
		 * @throws std::runtime_error if `path` is absent, cannot be read, or the range extends past
		 *         its end -- a short read is never returned as a short result.
		 */
		[[nodiscard]] virtual std::vector<std::byte>
		ReadRange(std::string_view path, uint64_t offset, uint64_t size) const = 0;

		/**
		 * Every path this filesystem carries beneath `prefix`, or all of them when it is empty.
		 * Unordered.
		 *
		 * `prefix` matches whole path components: `Textures` yields `Textures/a.ktx2` and never
		 * `TexturesSrc/a.ktx2`.
		 */
		[[nodiscard]] virtual std::vector<std::string>
		Enumerate(std::string_view prefix = {}) const = 0;

		/**
		 * Whether nothing here can be written, which is a property of the backing store and not of
		 * one path. An archive is read-only; a directory is not.
		 *
		 * What a caller holding a derived build product asks before deciding to regenerate it: a
		 * stale `.bvat` under a writable mount is re-baked, and under a read-only one is trusted,
		 * because a shipped archive was packed with a fresh one and has no writable input to
		 * re-bake from.
		 */
		[[nodiscard]] virtual bool
		IsReadOnly() const noexcept = 0;
	};
}
