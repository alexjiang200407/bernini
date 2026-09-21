#pragma once
#include <editor_api/IEditorHost.h>
#include <filesystem>
#include <memory>
#include <string_view>

namespace editor
{
	// Registry-owned; call arguments are borrowed and must not be retained.
	class IEditorImporter
	{
	public:
		virtual ~IEditorImporter()              = default;
		IEditorImporter(const IEditorImporter&) = delete;
		IEditorImporter(IEditorImporter&&)      = delete;
		IEditorImporter&
		operator=(const IEditorImporter&) = delete;
		IEditorImporter&
		operator=(IEditorImporter&&) = delete;

		virtual void
		Import(
			IEditorHost&                 host,
			const std::filesystem::path& source,
			std::string_view             targetFolder) = 0;

	protected:
		IEditorImporter() = default;
	};
	using EditorImporterPtr = std::unique_ptr<IEditorImporter>;

}
