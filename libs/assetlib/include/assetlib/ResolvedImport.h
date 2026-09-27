#pragma once
#include <assetlib/import_document.h>
#include <string>

namespace assetlib
{
	/** An owned snapshot; retaining it does not retain the store or observe later document edits. */
	struct ResolvedImport
	{
		std::string    documentKey;
		std::string    outputKey;
		ImportDocument document;
	};
}
