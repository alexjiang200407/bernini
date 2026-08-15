#pragma once

#include "Windows/AssetImporter/folder_row.h"

#include <QWidget>

class QFormLayout;
class QLineEdit;
class QToolButton;
class QVBoxLayout;

namespace editor
{
	/** A file field inside a section: what the file is, and what it starts out called. */
	struct FileRowDesc
	{
		QString label;
		QString stem;       // what the field starts with
		QString extension;  // shown after the field and never editable -- see AddFile
		QString objectName;
		QString tip;
	};

	/**
	 * A category's destination on an importer dialog: the folder field, and the files that will be
	 * written into it folded away beneath it.
	 *
	 * A section starts collapsed, so a dialog of them reads as the one that only ever offered folders
	 * -- and a source carrying thirty materials does not open thirty fields in front of a user who
	 * wanted the defaults.
	 *
	 * Disabling the section disables the folder and every file under it, because Qt disables a
	 * widget's children with it; callers do that rather than tracking the fields themselves.
	 */
	class ImportSection : public QWidget
	{
	public:
		ImportSection(QVBoxLayout* layout, QWidget* parent, const FolderRowDesc& desc);

		/**
		 * Adds a name field for one file this section writes, revealing the fold-out toggle if this is
		 * the first.
		 *
		 * The extension sits after the field and cannot be edited: a name chooses which file, never
		 * which format, and a typed `.bmesh` would otherwise land as `coyote.bmesh.bmesh`.
		 *
		 * @return The field, for the caller to read and to validate.
		 */
		QLineEdit*
		AddFile(const FileRowDesc& desc);

		/** The folder field, to read and to enable alongside the piece of the import that writes here. */
		[[nodiscard]] QLineEdit*
		GetFolder() const;

	private:
		void
		SetExpanded(bool expanded);

		QToolButton* m_Toggle = nullptr;
		QLineEdit*   m_Folder = nullptr;
		QWidget*     m_Body   = nullptr;
		QFormLayout* m_Files  = nullptr;
	};
}
