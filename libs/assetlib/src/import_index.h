#pragma once
#include <assetlib/AssetStore.h>
#include <core/str/str.h>
#include <mutex>
#include <string>

namespace assetlib
{
	struct AssetStore::ImportIndex
	{
		std::mutex                                mutex;
		core::str::unordered_str_map<std::string> documents;
		bool                                      initialized = false;

		ImportIndex()                   = default;
		~ImportIndex()                  = default;
		ImportIndex(const ImportIndex&) = delete;
		ImportIndex&
		operator=(const ImportIndex&) = delete;
		ImportIndex(ImportIndex&&)    = delete;
		ImportIndex&
		operator=(ImportIndex&&) = delete;
	};
}
