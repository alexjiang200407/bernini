#include "ui/UiFileInterface.h"

namespace game
{
	UiFileInterface::UiFileInterface(const core::file::IFileSystem& files) noexcept : m_Files(files)
	{}

	Rml::FileHandle
	UiFileInterface::Open(const Rml::String& path)
	{
		// Zero is RmlUi's "could not open"; every live handle is above it.
		if (!m_Files.Exists(path))
		{
			return 0;
		}

		auto file = OpenFile();
		try
		{
			file.bytes = m_Files.Read(path);
		}
		catch (const std::exception& e)
		{
			logger::error("UI file '{}' could not be read: {}", path, e.what());
			return 0;
		}

		const Rml::FileHandle handle = m_NextHandle++;
		m_Open.emplace(handle, std::move(file));
		return handle;
	}

	void
	UiFileInterface::Close(Rml::FileHandle file)
	{
		m_Open.erase(file);
	}

	size_t
	UiFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
	{
		OpenFile* open = Find(file);
		if (open == nullptr || buffer == nullptr)
		{
			return 0;
		}

		const size_t served = std::min(size, open->bytes.size() - open->cursor);
		std::memcpy(buffer, open->bytes.data() + open->cursor, served);
		open->cursor += served;
		return served;
	}

	bool
	UiFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
	{
		OpenFile* open = Find(file);
		if (open == nullptr)
		{
			return false;
		}

		const auto length = static_cast<int64_t>(open->bytes.size());
		int64_t    base   = 0;
		switch (origin)
		{
		case SEEK_SET:
			base = 0;
			break;
		case SEEK_CUR:
			base = static_cast<int64_t>(open->cursor);
			break;
		case SEEK_END:
			base = length;
			break;
		default:
			return false;
		}

		const int64_t target = base + offset;
		if (target < 0 || target > length)
		{
			return false;
		}

		open->cursor = static_cast<size_t>(target);
		return true;
	}

	size_t
	UiFileInterface::Tell(Rml::FileHandle file)
	{
		const OpenFile* open = Find(file);
		return open != nullptr ? open->cursor : 0;
	}

	size_t
	UiFileInterface::Length(Rml::FileHandle file)
	{
		const OpenFile* open = Find(file);
		return open != nullptr ? open->bytes.size() : 0;
	}

	UiFileInterface::OpenFile*
	UiFileInterface::Find(Rml::FileHandle file) noexcept
	{
		const auto it = m_Open.find(file);
		return it != m_Open.end() ? &it->second : nullptr;
	}
}
