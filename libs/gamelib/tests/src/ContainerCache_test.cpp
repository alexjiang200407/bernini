#include <gamelib/AssetManager.h>

#include "util/RigFixture.h"
#include "util/TestOptions.h"

#include "StoreAt.h"
#include <assetlib/AssetStore.h>
#include <assetlib_structs/Animation.h>
#include <bgl/IGraphics.h>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/IFileSystem.h>
#include <core/file/LooseFileSystem.h>

// Acquiring a rig reads three containers, and deserializing one is most of a second on a dense rig.
// A rig drawn as many meshes acquires once per mesh entry, so what these pin is that the second
// acquire deserializes nothing -- and that the cache doing so cannot serve a file that has changed.

namespace
{
	using game::test::DataRoot;
	using game::test::WriteRig;

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = true;
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		return opts;
	}

	/**
	 * A mount that counts what is read through it, per path -- per path because an acquire also
	 * reads the materials and textures the submeshes name, which this task does not cache. `Stat` is
	 * counted apart from `Read`: a stamped cache keeps asking the cheap question and stops asking
	 * the expensive one.
	 */
	class CountingFiles final : public core::file::IFileSystem
	{
	public:
		explicit CountingFiles(std::filesystem::path root) : m_Inner(std::move(root)) {}

		// Declared rather than left implicit, for the reason IFileSystem states about itself: MSVC
		// warns -- as an error here -- about every special member a derived class inherits deleted.
		CountingFiles(const CountingFiles&) = delete;
		CountingFiles(CountingFiles&&)      = delete;
		CountingFiles&
		operator=(const CountingFiles&) = delete;
		CountingFiles&
		operator=(CountingFiles&&) = delete;

		[[nodiscard]] bool
		Exists(std::string_view path) const noexcept override
		{
			return m_Inner.Exists(path);
		}

		[[nodiscard]] std::optional<core::file::FileStamp>
		Stat(std::string_view path) const noexcept override
		{
			++stats[std::string(path)];
			return m_Inner.Stat(path);
		}

		[[nodiscard]] std::vector<std::byte>
		Read(std::string_view path) const override
		{
			++reads[std::string(path)];
			return m_Inner.Read(path);
		}

		[[nodiscard]] std::vector<std::byte>
		ReadRange(std::string_view path, uint64_t offset, uint64_t size) const override
		{
			++reads[std::string(path)];
			return m_Inner.ReadRange(path, offset, size);
		}

		[[nodiscard]] std::vector<std::string>
		Enumerate(std::string_view prefix = {}) const override
		{
			return m_Inner.Enumerate(prefix);
		}

		[[nodiscard]] bool
		IsReadOnly() const noexcept override
		{
			return m_Inner.IsReadOnly();
		}

		[[nodiscard]] int
		ReadsOf(const std::string& path) const
		{
			const auto it = reads.find(path);
			return it == reads.end() ? 0 : it->second;
		}

		[[nodiscard]] int
		StatsOf(const std::string& path) const
		{
			const auto it = stats.find(path);
			return it == stats.end() ? 0 : it->second;
		}

		mutable std::map<std::string, int> reads;
		mutable std::map<std::string, int> stats;

	private:
		core::file::LooseFileSystem m_Inner;
	};

	// The three an acquire reads, and the only three this task caches.
	const auto c_Containers = std::array<std::string, 3>{ { "Derived/Meshes/rig.bmesh",
		                                                    "Derived/Skeletons/rig.bskel",
		                                                    "Derived/Animations/rig.banim" } };
}

TEST_CASE("The static and VAT doors read their mesh once too", "[static][acquire][cache]")
{
	// A rig drawn as many meshes acquires once per mesh entry -- twenty-seven on the test project's
	// character -- and these two doors used to deserialize the whole `.bmesh` on every one of them,
	// where the skinned door has always gone through the cache.
	DataRoot root("bernini_container_cache_static");
	WriteRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);

	auto files  = std::make_shared<CountingFiles>(root.path);
	auto assets = game::AssetManager(scene, assetlib::AssetStore(root.path, files));

	const bgl::GeomHandle first = assets.AcquireMesh("Derived/Meshes/rig.bmesh");
	REQUIRE(first.IsValid());

	// Released to zero, so the geom cache cannot be what answers the second acquire.
	assets.ReleaseGeom(first);

	const int before = files->ReadsOf("Derived/Meshes/rig.bmesh");

	const bgl::GeomHandle second = assets.AcquireMesh("Derived/Meshes/rig.bmesh");
	REQUIRE(second.IsValid());

	// Two, not three: both staleness questions are asked again and the deserialize is what is
	// skipped, exactly as it is for the skinned door below.
	CHECK(files->ReadsOf("Derived/Meshes/rig.bmesh") == before + 2);
}

TEST_CASE("Acquiring a rig twice reads its containers once", "[skinned][acquire][cache]")
{
	DataRoot root("bernini_container_cache");
	WriteRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);

	auto files  = std::make_shared<CountingFiles>(root.path);
	auto assets = game::AssetManager(scene, assetlib::AssetStore(root.path, files));

	const auto first =
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
	REQUIRE(first.geom.IsValid());

	// Four apiece: the stamp, the key the load's staleness refusal peeks at, the key the seam
	// peeks at behind it, and the deserialize.
	for (const std::string& path : c_Containers) REQUIRE(files->ReadsOf(path) == 4);

	SECTION("a re-acquire after a full release reads nothing back off the disk")
	{
		// Released to zero, so the geom cache cannot be what answers the second acquire -- the
		// containers have to be read again, or come from the cache under test.
		assets.ReleaseGeom(first.geom);

		auto readsBefore = std::map<std::string, int>();
		auto statsBefore = std::map<std::string, int>();
		for (const std::string& path : c_Containers)
		{
			readsBefore[path] = files->ReadsOf(path);
			statsBefore[path] = files->StatsOf(path);
		}

		const auto second =
			assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
		REQUIRE(second.geom.IsValid());

		// Two apiece, not three: both staleness questions are asked again -- the stamp, and whether
		// the entry's source has moved under it -- and the deserialize is what is skipped. Against
		// a directory mount, which is what the editor opens, the stamp read goes too: assetlib
		// memoizes it on the host path behind size and mtime, and only a mount it cannot identify
		// (this one) re-hashes.
		for (const std::string& path : c_Containers)
		{
			INFO(path);
			CHECK(files->ReadsOf(path) == readsBefore[path] + 2);

			// Still asked: a cache that stopped asking would serve a stale rig.
			CHECK(files->StatsOf(path) > statsBefore[path]);
		}
	}

	SECTION("a clip set rewritten on disk is picked up rather than served from the cache")
	{
		assets.ReleaseGeom(first.geom);

		auto clips = LoadAt<assetlib::AnimationSet>(root.path / "Derived/Animations/rig.banim");
		REQUIRE(clips.clips.size() == 1);
		clips.clips[0].nameOffset = clips.stringPool.add("renamed");
		SaveAt(clips, root.path / "Derived/Animations/rig.banim");

		const auto second =
			assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
		REQUIRE(second.clips.size() == 1);
		CHECK(second.clips[0].name == "renamed");
	}

	SECTION("a container that disappears is reported, not served empty from a zeroed stamp")
	{
		// StampOf zeroes an absent path, which is what a never-read cache entry already holds. A
		// cache keyed on the stamp alone would match those two and hand back an empty container.
		DataRoot missing("bernini_container_cache_missing");

		auto empty = std::make_shared<CountingFiles>(missing.path);
		auto other = game::AssetManager(scene, assetlib::AssetStore(missing.path, empty));

		const auto failureOf = [&] {
			try
			{
				(void)other.AcquireSkinnedMesh(
					"Derived/Meshes/rig.bmesh",
					"Derived/Animations/rig.banim");
			}
			catch (const std::exception& e)
			{
				return std::string(e.what());
			}
			return std::string("no failure");
		};

		// The same cause twice, not merely a failure twice. A failed read must leave nothing behind:
		// an entry inserted before the load that threw would hold the zeroed stamp StampOf gives an
		// absent path, the retry would match it, and the caller would get an empty container. That
		// still ends in an error here -- an empty clip set names no skeleton, so the next read fails
		// instead -- which is why this compares the messages rather than counting throws.
		const std::string firstFailure  = failureOf();
		const std::string secondFailure = failureOf();

		CHECK_THAT(
			firstFailure,
			Catch::Matchers::ContainsSubstring("Derived/Animations/rig.banim"));
		CHECK(secondFailure == firstFailure);
	}
}
