#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>

namespace core::file
{
	namespace
	{
		// Whether `normalized` names something strictly inside a root: the root itself, anything
		// above it, and any rooted path -- operator/ replaces the root for an absolute right-hand
		// side, and for a drive-relative `C:foo` on another drive as well -- are somebody else's
		// files.
		bool
		is_contained(const std::filesystem::path& normalized) noexcept
		{
			const std::string generic = normalized.generic_string();

			return !generic.empty() && generic != "." && generic != ".." &&
			       !generic.starts_with("../") && !generic.starts_with('/') &&
			       !normalized.is_absolute() && !normalized.has_root_name();
		}

		std::ifstream
		open_or_throw(const std::filesystem::path& absolute, std::string_view path)
		{
			// Cleared so the message cannot blame a stale errno from an unrelated call.
			errno = 0;
			std::ifstream in(absolute, std::ios::binary);
			if (!in)
				core::throw_runtime_error(
					"LooseFileSystem: cannot open '{}': {}",
					path,
					std::generic_category().message(errno));

			return in;
		}
	}

	LooseFileSystem::LooseFileSystem(std::filesystem::path root, bool readOnly) noexcept :
		m_Root(std::move(root)), m_ReadOnly(readOnly)
	{}

	std::filesystem::path
	LooseFileSystem::Resolve(std::string_view path) const
	{
		const std::filesystem::path relative = std::filesystem::path(path).lexically_normal();
		return is_contained(relative) ? m_Root / relative : std::filesystem::path();
	}

	bool
	LooseFileSystem::Exists(std::string_view path) const noexcept
	{
		const std::filesystem::path absolute = Resolve(path);
		if (absolute.empty())
			return false;

		std::error_code ec;
		return std::filesystem::is_regular_file(absolute, ec) && !ec;
	}

	std::optional<FileStamp>
	LooseFileSystem::Stat(std::string_view path) const noexcept
	{
		const std::filesystem::path absolute = Resolve(path);
		if (absolute.empty())
			return std::nullopt;

		std::error_code ec;

		const auto size = std::filesystem::file_size(absolute, ec);
		if (ec)
			return std::nullopt;

		const auto written = std::filesystem::last_write_time(absolute, ec);
		if (ec)
			return std::nullopt;

		// Seconds, not the native tick: file_time_type's resolution and epoch are implementation
		// defined, and a stamp has to survive being written on one machine and compared on another.
		// Must stay identical to assetlib::stampOf, which compares against stamps written here.
		const auto seconds =
			std::chrono::duration_cast<std::chrono::seconds>(written.time_since_epoch()).count();

		return FileStamp{ static_cast<uint64_t>(size), static_cast<int64_t>(seconds) };
	}

	std::vector<std::byte>
	LooseFileSystem::Read(std::string_view path) const
	{
		const std::filesystem::path absolute = Resolve(path);
		if (absolute.empty())
			core::throw_runtime_error("LooseFileSystem: '{}' is outside the root", path);

		std::ifstream in = open_or_throw(absolute, path);

		in.seekg(0, std::ios::end);
		const std::streamoff size = in.tellg();
		in.seekg(0, std::ios::beg);

		if (size < 0)
			core::throw_runtime_error("LooseFileSystem: cannot size '{}'", path);

		std::vector<std::byte> out(static_cast<size_t>(size));
		if (!out.empty() &&
		    !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)))
			core::throw_runtime_error("LooseFileSystem: failed to read '{}'", path);

		return out;
	}

	std::vector<std::byte>
	LooseFileSystem::ReadRange(std::string_view path, uint64_t offset, uint64_t size) const
	{
		const std::filesystem::path absolute = Resolve(path);
		if (absolute.empty())
			core::throw_runtime_error("LooseFileSystem: '{}' is outside the root", path);

		std::error_code ec;
		const auto      fileSize = std::filesystem::file_size(absolute, ec);
		if (ec)
			core::throw_runtime_error("LooseFileSystem: cannot size '{}': {}", path, ec.message());

		// Subtraction rather than addition: offset + size near UINT64_MAX would wrap past the sum.
		if (size > fileSize || offset > fileSize - size)
			core::throw_runtime_error(
				"LooseFileSystem: range [{}, {}) extends past the end of '{}' ({} bytes)",
				offset,
				offset + size,
				path,
				fileSize);

		std::ifstream in = open_or_throw(absolute, path);

		std::vector<std::byte> out(static_cast<size_t>(size));
		if (!out.empty())
		{
			in.seekg(static_cast<std::streamoff>(offset));
			if (!in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)))
				core::throw_runtime_error("LooseFileSystem: failed to read '{}'", path);
		}

		return out;
	}

	std::vector<std::string>
	LooseFileSystem::Enumerate(std::string_view prefix) const
	{
		const std::filesystem::path base = prefix.empty() ? m_Root : Resolve(prefix);
		if (base.empty())
			return {};

		std::error_code ec;
		if (!std::filesystem::is_directory(base, ec) || ec)
			return {};

		std::vector<std::string> out;

		for (auto it = std::filesystem::recursive_directory_iterator(base, ec);
		     !ec && it != std::filesystem::recursive_directory_iterator();
		     it.increment(ec))
		{
			if (!it->is_regular_file(ec) || ec)
				continue;

			out.push_back(
				std::filesystem::relative(it->path(), m_Root, ec)
					.lexically_normal()
					.generic_string());
		}

		return out;
	}
}
