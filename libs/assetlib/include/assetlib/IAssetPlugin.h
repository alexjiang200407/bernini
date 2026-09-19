#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assetlib
{
	struct AssetKindDesc
	{
		std::string id;
		std::string extension;
		bool        includeInPack = true;
	};

	struct DocumentReference
	{
		std::string target;
		std::string field;
	};

	/** One authored file type; callbacks must be safe for concurrent reads and throw on invalid bytes. */
	class IAssetKind
	{
	public:
		virtual ~IAssetKind() = default;

		[[nodiscard]] virtual const AssetKindDesc&
		GetDesc() const noexcept = 0;

		/** Fields uniquely locate references in these bytes; the host validates and normalizes target keys. */
		[[nodiscard]] virtual std::vector<DocumentReference>
		ReadReferences(std::span<const std::byte> bytes) const = 0;

		/** Replace only the named fields from ReadReferences on these same bytes; targets are normalized. */
		[[nodiscard]] virtual std::vector<std::byte>
		RewriteReferences(
			std::span<const std::byte>         bytes,
			std::span<const DocumentReference> replacements) const = 0;

		/** Preserve unknown fields. Return current-schema bytes; the host owns all writes and rollback. */
		[[nodiscard]] virtual std::vector<std::byte>
		Migrate(std::span<const std::byte> bytes) const = 0;
	};

	using AssetKindPtr = std::unique_ptr<IAssetKind>;

	class IAssetKindRegistry
	{
	public:
		virtual ~IAssetKindRegistry() = default;

		/** Takes ownership; reject duplicate IDs/extensions, including built-ins, and null kinds. */
		virtual void
		Add(AssetKindPtr kind) = 0;
	};

	class IAssetPlugin
	{
	public:
		virtual ~IAssetPlugin() = default;

		/** Startup before stores open; this object must outlive all its registered kinds. */
		virtual void
		RegisterKinds(IAssetKindRegistry& registry) = 0;
	};

	using AssetPluginPtr = std::unique_ptr<IAssetPlugin>;

	/** Called only after build compatibility is checked; ownership transfers to the host. */
	using CreateAssetPlugin                                   = IAssetPlugin* (*)();
	inline constexpr std::string_view c_AssetPluginEntryPoint = "BerniniCreateAssetPlugin";
}
