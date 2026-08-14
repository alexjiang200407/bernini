#include <core/file/LooseFileSystem.h>
#include <core/file/SearchPath.h>
#include <core/file/file.h>

namespace
{
	// A directory under the cwd, removed when the fixture dies. Each test names its own, so a
	// leftover from a crashed run cannot make the next one pass or fail for the wrong reason.
	class TempDir
	{
	public:
		explicit TempDir(const std::string& name) : m_Path(std::filesystem::path(name))
		{
			std::error_code ec;
			std::filesystem::remove_all(m_Path, ec);
			std::filesystem::create_directories(m_Path, ec);
		}

		~TempDir()
		{
			std::error_code ec;
			std::filesystem::remove_all(m_Path, ec);
		}

		TempDir(const TempDir&) = delete;
		TempDir&
		operator=(const TempDir&) = delete;

		[[nodiscard]] const std::filesystem::path&
		Path() const noexcept
		{
			return m_Path;
		}

		void
		Write(std::string_view relative, std::string_view contents) const
		{
			const std::filesystem::path file = m_Path / relative;

			std::error_code ec;
			std::filesystem::create_directories(file.parent_path(), ec);

			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		}

	private:
		std::filesystem::path m_Path;
	};

	std::string
	AsString(std::span<const std::byte> bytes)
	{
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	std::vector<std::string>
	Sorted(std::vector<std::string> paths)
	{
		std::ranges::sort(paths);
		return paths;
	}
}

TEST_CASE("LooseFileSystem reads whole files and ranges", "[filesystem]")
{
	const TempDir dir("fs_loose_read");
	dir.Write("Materials/kirk.bmaterial", "0123456789");

	const core::file::LooseFileSystem fs(dir.Path());

	CHECK(fs.Exists("Materials/kirk.bmaterial"));
	CHECK_FALSE(fs.Exists("Materials/nobody.bmaterial"));
	CHECK_FALSE(fs.IsReadOnly());

	CHECK(AsString(fs.Read("Materials/kirk.bmaterial")) == "0123456789");

	SECTION("a range reads only its bytes")
	{
		CHECK(AsString(fs.ReadRange("Materials/kirk.bmaterial", 3, 4)) == "3456");
	}

	SECTION("an empty range is legal and empty")
	{
		CHECK(fs.ReadRange("Materials/kirk.bmaterial", 10, 0).empty());
	}

	SECTION("a range past the end throws rather than returning short")
	{
		CHECK_THROWS_AS(fs.ReadRange("Materials/kirk.bmaterial", 8, 4), std::runtime_error);

		// offset + size would wrap; the check must not be an addition.
		CHECK_THROWS_AS(
			fs.ReadRange("Materials/kirk.bmaterial", UINT64_MAX, 4),
			std::runtime_error);
	}

	SECTION("a missing file throws")
	{
		CHECK_THROWS_AS(fs.Read("Materials/nobody.bmaterial"), std::runtime_error);
	}
}

TEST_CASE("LooseFileSystem stamps match the filesystem", "[filesystem]")
{
	const TempDir dir("fs_loose_stat");
	dir.Write("a.txt", "abcdef");

	const core::file::LooseFileSystem fs(dir.Path());

	const std::optional<core::file::FileStamp> stamp = fs.Stat("a.txt");
	REQUIRE(stamp.has_value());
	CHECK(stamp->size == 6);

	const auto written = std::filesystem::last_write_time(dir.Path() / "a.txt");
	CHECK(
		stamp->mtime ==
		std::chrono::duration_cast<std::chrono::seconds>(written.time_since_epoch()).count());

	CHECK_FALSE(fs.Stat("missing.txt").has_value());
}

TEST_CASE("LooseFileSystem refuses paths that escape its root", "[filesystem]")
{
	const TempDir dir("fs_loose_escape");
	dir.Write("inside.txt", "in");

	const core::file::LooseFileSystem fs(dir.Path() / "sub");

	// `sub` does not exist, and neither of these may reach `inside.txt` above it.
	CHECK_FALSE(fs.Exists("../inside.txt"));
	CHECK_FALSE(fs.Exists("/etc/passwd"));
	CHECK_THROWS_AS(fs.Read("../inside.txt"), std::runtime_error);
}

TEST_CASE("LooseFileSystem enumerates recursively and by prefix", "[filesystem]")
{
	const TempDir dir("fs_loose_enumerate");
	dir.Write("Textures/a.ktx2", "a");
	dir.Write("Textures/nested/b.ktx2", "b");
	dir.Write("TexturesSrc/c.png", "c");
	dir.Write("Materials/d.bmaterial", "d");

	const core::file::LooseFileSystem fs(dir.Path());

	CHECK(
		Sorted(fs.Enumerate()) == std::vector<std::string>{ "Materials/d.bmaterial",
	                                                        "Textures/a.ktx2",
	                                                        "Textures/nested/b.ktx2",
	                                                        "TexturesSrc/c.png" });

	// A prefix matches whole components, so TexturesSrc is not under Textures.
	CHECK(
		Sorted(fs.Enumerate("Textures")) ==
		std::vector<std::string>{ "Textures/a.ktx2", "Textures/nested/b.ktx2" });

	CHECK(fs.Enumerate("Nothing").empty());
}

TEST_CASE("SearchPath resolves to the first mount that carries a path", "[filesystem]")
{
	const TempDir over("fs_path_over");
	const TempDir under("fs_path_under");

	over.Write("shared.txt", "from the overlay");
	under.Write("shared.txt", "from the archive");
	under.Write("only_under.txt", "under");

	const auto overFs  = std::make_shared<core::file::LooseFileSystem>(over.Path());
	const auto underFs = std::make_shared<core::file::LooseFileSystem>(under.Path(), true);

	core::file::SearchPath path;
	path.Mount(overFs);
	path.Mount(underFs);

	CHECK(AsString(path.Read("shared.txt")) == "from the overlay");
	CHECK(AsString(path.Read("only_under.txt")) == "under");
	CHECK(path.Resolve("shared.txt") == overFs.get());
	CHECK(path.Resolve("only_under.txt") == underFs.get());
	CHECK(path.Resolve("absent.txt") == nullptr);

	CHECK_THROWS_AS(path.Read("absent.txt"), std::runtime_error);

	SECTION("a stamp comes from the mount that answered")
	{
		CHECK(path.Stat("shared.txt") == overFs->Stat("shared.txt"));
	}

	SECTION("read-only is the answering mount's answer, not the path's")
	{
		// The point of the query: this search path is writable for one path and not the next.
		CHECK_FALSE(path.IsPathReadOnly("shared.txt"));
		CHECK(path.IsPathReadOnly("only_under.txt"));
		CHECK_FALSE(path.IsReadOnly());
	}

	SECTION("the union is enumerated once per path")
	{
		CHECK(
			Sorted(path.Enumerate()) == std::vector<std::string>{ "only_under.txt", "shared.txt" });
	}
}

TEST_CASE("SearchPath is read-only when every mount is", "[filesystem]")
{
	const TempDir a("fs_path_ro_a");
	const TempDir b("fs_path_ro_b");

	core::file::SearchPath path;
	CHECK(path.IsReadOnly());  // nothing mounted: nothing can be written

	path.Mount(std::make_shared<core::file::LooseFileSystem>(a.Path(), true));
	CHECK(path.IsReadOnly());

	path.Mount(std::make_shared<core::file::LooseFileSystem>(b.Path(), false));
	CHECK_FALSE(path.IsReadOnly());
}

TEST_CASE("SearchPath masks a path every mount still carries", "[filesystem]")
{
	const TempDir over("fs_mask_over");
	const TempDir under("fs_mask_under");

	over.Write("gone.txt", "overlay copy");
	under.Write("gone.txt", "packed copy");
	under.Write("kept.txt", "kept");

	core::file::SearchPath path;
	path.Mount(std::make_shared<core::file::LooseFileSystem>(over.Path()));
	path.Mount(std::make_shared<core::file::LooseFileSystem>(under.Path(), true));

	const std::vector<std::string> mask = { "gone.txt" };
	path.SetMask(mask);

	CHECK(path.IsMasked("gone.txt"));
	CHECK_FALSE(path.Exists("gone.txt"));
	CHECK(path.Resolve("gone.txt") == nullptr);
	CHECK_FALSE(path.Stat("gone.txt").has_value());
	CHECK_THROWS_AS(path.Read("gone.txt"), std::runtime_error);
	CHECK(Sorted(path.Enumerate()) == std::vector<std::string>{ "kept.txt" });

	SECTION("setting the mask again replaces it")
	{
		path.SetMask({});
		CHECK(path.Exists("gone.txt"));
		CHECK(AsString(path.Read("gone.txt")) == "overlay copy");
	}
}

TEST_CASE("write_atomic leaves no partial file behind", "[filesystem]")
{
	const TempDir dir("fs_write_atomic");

	const std::filesystem::path target = dir.Path() / "out.bin";
	const std::string           first  = "first generation";

	core::file::write_atomic(target, std::as_bytes(std::span(first)));
	CHECK(AsString(core::file::read_file_bytes(target.string())) == first);

	SECTION("a second write replaces the first")
	{
		const std::string second = "second";
		core::file::write_atomic(target, std::as_bytes(std::span(second)));
		CHECK(AsString(core::file::read_file_bytes(target.string())) == second);
	}

	SECTION("a failed write throws and leaves the previous contents")
	{
		const std::string doomed = "never lands";

		// The parent of the temp is what has to be openable; a target inside a directory that does
		// not exist cannot be written, and must not damage anything.
		CHECK_THROWS_AS(
			core::file::write_atomic(
				dir.Path() / "nope" / "out.bin",
				std::as_bytes(std::span(doomed))),
			std::runtime_error);

		CHECK(AsString(core::file::read_file_bytes(target.string())) == first);
	}

	SECTION("no temp files are left in the directory")
	{
		std::vector<std::string> leftovers;
		for (const auto& entry : std::filesystem::directory_iterator(dir.Path()))
			if (entry.path().extension() == ".tmp")
				leftovers.push_back(entry.path().filename().string());

		CHECK(leftovers.empty());
	}
}
