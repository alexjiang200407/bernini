#pragma once
#include <assetlib/RegenMesh.h>
#include <assetlib/ResolvedImport.h>
#include <assetlib/asset_refs.h>
#include <core/err/util.h>
#include <core/file/IFileSystem.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assetlib::test
{
	class ImportHost final : public core::file::IFileSystem
	{
	public:
		ImportHost()                  = default;
		ImportHost(const ImportHost&) = delete;
		ImportHost(ImportHost&&)      = delete;
		ImportHost&
		operator=(const ImportHost&) = delete;
		ImportHost&
		operator=(ImportHost&&) = delete;

		std::map<std::pair<std::string, AssetType>, ResolvedImport> imports;
		std::map<std::string, RegenMesh>                            meshes;
		std::string                                                 failure;
		bool                                                        readOnly = true;

		bool
		Exists(std::string_view) const noexcept override
		{
			return false;
		}
		std::optional<core::file::FileStamp>
		Stat(std::string_view) const noexcept override
		{
			return std::nullopt;
		}
		std::vector<std::byte>
		Read(std::string_view) const override
		{
			core::throw_runtime_error("contract host carries no files");
		}
		std::vector<std::byte>
		ReadRange(std::string_view, uint64_t, uint64_t) const override
		{
			core::throw_runtime_error("contract host carries no files");
		}
		std::vector<std::string>
		Enumerate(std::string_view) const override
		{
			return {};
		}
		bool
		IsReadOnly() const noexcept override
		{
			return readOnly;
		}
	};
}
