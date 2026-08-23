#include "Windows/MaterialEditor/CachedMaterial.h"

#include <QTemporaryDir>

#include <assetlib/bmaterial_io.h>

#include <assetlib/AssetStore.h>
#include <catch2/catch_test_macros.hpp>

namespace
{
	/**
	 * A `.bmaterial` on disk whose modification time the test controls.
	 *
	 * Holding the stamp still is what makes the caching observable: the same path with different
	 * contents and an unchanged stamp must still read as what was there before, because a cache that
	 * re-read would say otherwise.
	 */
	struct Sandbox
	{
		QTemporaryDir temp;

		[[nodiscard]] QString
		Path() const
		{
			return temp.filePath("rust.bmaterial");
		}

		/** The temp dir as a data root, which is what a store over this sandbox addresses. */
		[[nodiscard]] std::filesystem::path
		Root() const
		{
			return std::filesystem::path(temp.path().toStdWString());
		}

		void
		Write(const std::string& name) const
		{
			auto material         = assetlib::BMaterial();
			material.name         = name;
			material.shadingModel = assetlib::ShadingModel::kPbr;

			assetlib::AssetStore(Root()).Save(material, "rust.bmaterial");
		}

		[[nodiscard]] std::filesystem::file_time_type
		Stamp() const
		{
			return std::filesystem::last_write_time(std::filesystem::path(Path().toStdWString()));
		}

		void
		SetStamp(std::filesystem::file_time_type when) const
		{
			std::filesystem::last_write_time(std::filesystem::path(Path().toStdWString()), when);
		}
	};
}

TEST_CASE("An unchanged material is not read twice", "[materialeditor]")
{
	// Reading one parses the editor graph saved inside it, which is the bulk of the file and none of
	// what the properties panel wants. The panel asks on every submesh selection.
	const Sandbox  sandbox;
	CachedMaterial cached;

	sandbox.Write("first");
	const std::filesystem::file_time_type stamp = sandbox.Stamp();

	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path()) != nullptr);
	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path())->name == "first");

	// Different contents, same stamp. A cache that went back to the file would see "second".
	sandbox.Write("second");
	sandbox.SetStamp(stamp);

	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path())->name == "first");
}

TEST_CASE("A material rewritten on disk is read again", "[materialeditor]")
{
	// The editor is also the cook host: Bake rewrites the file under an open graph, and the panel is
	// showing what it said before.
	const Sandbox  sandbox;
	CachedMaterial cached;

	sandbox.Write("first");
	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path())->name == "first");

	sandbox.Write("second");
	sandbox.SetStamp(sandbox.Stamp() + std::chrono::seconds(1));

	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path())->name == "second");
}

TEST_CASE("Forgetting forces the next read", "[materialeditor]")
{
	// A stamp has millisecond resolution, so a save and the refresh behind it can land in the same
	// one. Save forgets rather than trusting the clock to have moved.
	const Sandbox  sandbox;
	CachedMaterial cached;

	sandbox.Write("first");
	const std::filesystem::file_time_type stamp = sandbox.Stamp();
	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path())->name == "first");

	sandbox.Write("second");
	sandbox.SetStamp(stamp);

	cached.Forget();

	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path())->name == "second");
}

TEST_CASE("A different path is read even at the same stamp", "[materialeditor]")
{
	// Submeshes sharing a material share one graph, but selecting a submesh backed by another one
	// asks the same cache about a different file.
	const Sandbox sandbox;
	sandbox.Write("first");

	CachedMaterial cached;
	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path())->name == "first");

	auto other = assetlib::BMaterial();
	other.name = "other";
	const auto otherPath =
		std::filesystem::path(sandbox.temp.filePath("other.bmaterial").toStdWString());
	assetlib::AssetStore(sandbox.Root()).Save(other, "other.bmaterial");

	REQUIRE(
		cached.Get(sandbox.Root(), QString::fromStdWString(otherPath.wstring()))->name == "other");
}

TEST_CASE("A material that is not there yet reads as nothing", "[materialeditor]")
{
	// Save As sets the path before anything is written, and the panel refreshes in between.
	const Sandbox  sandbox;
	CachedMaterial cached;

	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path()) == nullptr);
	REQUIRE(cached.Get(sandbox.Root(), "") == nullptr);
}

TEST_CASE("A material that appears later is picked up", "[materialeditor]")
{
	const Sandbox  sandbox;
	CachedMaterial cached;

	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path()) == nullptr);

	sandbox.Write("first");

	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path()) != nullptr);
	REQUIRE(cached.Get(sandbox.Root(), sandbox.Path())->name == "first");
}
