#pragma once

#include <QString>

class QLineEdit;
class QVBoxLayout;
class QWidget;

namespace editor
{
	/** A folder field on an importer dialog: what it is labelled, and how it starts out. */
	struct FolderRowDesc
	{
		QString label;
		QString category;  // the fixed part of the destination, shown as an uneditable prefix
		QString objectName;
		QString text;  // what the field starts with; empty leaves an optional subfolder blank
		QString placeholder;
		QString tip;
	};

	/**
	 * Adds a folder field behind its category, shown as an uneditable prefix so the layout is obvious.
	 *
	 * @return The field, parented to `parent`, for the caller to read and to enable alongside whichever
	 *         piece of the import writes into it.
	 */
	QLineEdit*
	AddFolderRow(QVBoxLayout* layout, QWidget* parent, const FolderRowDesc& desc);
}
