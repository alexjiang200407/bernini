#pragma once
#include <RmlUi/Core/FileInterface.h>
#include <core/file/IFileSystem.h>

namespace game
{
	/**
	 * RmlUi's file access, over the project's mount rather than the host filesystem: a document,
	 * stylesheet or font is addressed by the same data-root-relative key every other asset is, so a
	 * loose tree and a `.bpak` read alike (ADR-8).
	 *
	 * A handle holds the whole file. RmlUi reads a document once and a font face once, both small
	 * beside a texture, and `IFileSystem` has no streaming door -- `ReadRange` is positional, which
	 * would make a seek-and-read loop one syscall per call rather than one per file.
	 */
	class UiFileInterface final : public Rml::FileInterface
	{
	public:
		explicit UiFileInterface(const core::file::IFileSystem& files) noexcept;

		// All four declared rather than left implicit: the reference member deletes each, and MSVC
		// warns -- as an error here -- about every special member it deletes for you.
		UiFileInterface(const UiFileInterface&)     = delete;
		UiFileInterface(UiFileInterface&&) noexcept = delete;

		UiFileInterface&
		operator=(const UiFileInterface&) = delete;

		UiFileInterface&
		operator=(UiFileInterface&&) noexcept = delete;

		Rml::FileHandle
		Open(const Rml::String& path) override;

		void
		Close(Rml::FileHandle file) override;

		size_t
		Read(void* buffer, size_t size, Rml::FileHandle file) override;

		bool
		Seek(Rml::FileHandle file, long offset, int origin) override;

		size_t
		Tell(Rml::FileHandle file) override;

		size_t
		Length(Rml::FileHandle file) override;

	private:
		struct OpenFile
		{
			std::vector<std::byte> bytes;
			size_t                 cursor = 0;
		};

		[[nodiscard]] OpenFile*
		Find(Rml::FileHandle file) noexcept;

		const core::file::IFileSystem& m_Files;

		// Keyed by the handle RmlUi was given. Not the address of the record: a rehash would move
		// it, and RmlUi holds the handle across reads.
		std::unordered_map<Rml::FileHandle, OpenFile> m_Open;
		Rml::FileHandle                               m_NextHandle = 1;
	};
}
