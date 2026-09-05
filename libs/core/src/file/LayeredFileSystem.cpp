#include <algorithm>
#include <core/err/util.h>
#include <core/file/IFileSystem.h>
#include <core/file/LayeredFileSystem.h>
#include <core/str/str.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace core::file
{
	void
	LayeredFileSystem::Mount(std::shared_ptr<IFileSystem> mount)
	{
		if (mount != nullptr)
			m_Mounts.push_back(std::move(mount));
	}

	void
	LayeredFileSystem::SetMask(std::span<const std::string> paths)
	{
		m_Mask.clear();
		m_Mask.insert(paths.begin(), paths.end());
	}

	bool
	LayeredFileSystem::IsMasked(std::string_view path) const noexcept
	{
		return m_Mask.contains(path);
	}

	const IFileSystem*
	LayeredFileSystem::Resolve(std::string_view path) const noexcept
	{
		if (IsMasked(path))
			return nullptr;

		for (const std::shared_ptr<IFileSystem>& mount : m_Mounts)
			if (mount->Exists(path))
				return mount.get();

		return nullptr;
	}

	bool
	LayeredFileSystem::IsPathReadOnly(std::string_view path) const noexcept
	{
		const IFileSystem* mount = Resolve(path);
		return mount != nullptr ? mount->IsReadOnly() : IsReadOnly();
	}

	bool
	LayeredFileSystem::Exists(std::string_view path) const noexcept
	{
		return Resolve(path) != nullptr;
	}

	std::optional<FileStamp>
	LayeredFileSystem::Stat(std::string_view path) const noexcept
	{
		const IFileSystem* mount = Resolve(path);
		return mount != nullptr ? mount->Stat(path) : std::nullopt;
	}

	std::vector<std::byte>
	LayeredFileSystem::Read(std::string_view path) const
	{
		const IFileSystem* mount = Resolve(path);
		if (mount == nullptr)
			core::throw_runtime_error("LayeredFileSystem: no mount carries '{}'", path);

		return mount->Read(path);
	}

	std::vector<std::byte>
	LayeredFileSystem::ReadRange(std::string_view path, uint64_t offset, uint64_t size) const
	{
		const IFileSystem* mount = Resolve(path);
		if (mount == nullptr)
			core::throw_runtime_error("LayeredFileSystem: no mount carries '{}'", path);

		return mount->ReadRange(path, offset, size);
	}

	std::vector<std::string>
	LayeredFileSystem::Enumerate(std::string_view prefix) const
	{
		std::unordered_set<std::string, str::transparent_string_hash, std::equal_to<>> seen;
		std::vector<std::string>                                                       out;

		for (const std::shared_ptr<IFileSystem>& mount : m_Mounts)
			for (std::string& path : mount->Enumerate(prefix))
			{
				if (IsMasked(path) || seen.contains(path))
					continue;

				seen.insert(path);
				out.push_back(std::move(path));
			}

		return out;
	}

	bool
	LayeredFileSystem::IsReadOnly() const noexcept
	{
		return std::ranges::all_of(m_Mounts, [](const std::shared_ptr<IFileSystem>& mount) {
			return mount->IsReadOnly();
		});
	}
}
