#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/cancel.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

#include "MountAt.h"
#include "bmesh_texture.h"
#include "fs_util.h"

using namespace assetlib;

namespace
{
	// A scratch directory that cleans up after itself.
	struct ScratchDir
	{
		std::filesystem::path path;

		explicit ScratchDir(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}
		~ScratchDir() { std::filesystem::remove_all(path); }
	};

	// An import carrying `count` tiny textures and nothing else -- enough for WriteTextures, and small
	// enough that the Basis encode of each is quick.
	imp::BMeshImport
	ImportWithTextures(size_t count)
	{
		auto mesh = imp::BMeshImport();

		const std::vector<std::byte> rgba(4 * 4 * 4, std::byte{ 128 });
		for (size_t i = 0; i < count; ++i) mesh.textures.push_back(rgba8ToImage(rgba, 4, 4));

		return mesh;
	}

	// Writes a `size` x `size` uncompressed RGBA8 .ktx2, for a material to route at.
	void
	WriteSource(const std::filesystem::path& path, uint32_t size)
	{
		const std::vector<std::byte> pixels(static_cast<size_t>(size) * size * 4, std::byte{ 200 });
		writeKTX2(rgba8ToImage(pixels, size, size), path, false, Ktx2Compression::kNone);
	}

	// A token that is already signalled, as one is when the user cancels before the cook gets there.
	std::stop_source
	SignalledSource()
	{
		auto source = std::stop_source();
		source.request_stop();
		return source;
	}
}

TEST_CASE("a default CancelToken is never signalled", "[cancel]")
{
	// This is what every caller that does not offer cancellation relies on: leaving the argument out
	// must not mean "cancel immediately".
	REQUIRE_NOTHROW(throwIfCancelled(CancelToken()));
	REQUIRE_FALSE(CancelToken().stop_requested());
}

TEST_CASE("throwIfCancelled throws Cancelled once the token is signalled", "[cancel]")
{
	auto source = std::stop_source();
	REQUIRE_NOTHROW(throwIfCancelled(source.get_token()));

	source.request_stop();
	REQUIRE_THROWS_AS(throwIfCancelled(source.get_token()), Cancelled);
}

TEST_CASE("Cancelled is not a runtime_error", "[cancel]")
{
	// The whole point of the separate type: a caller that reports every std::runtime_error as a failure
	// must not report a cancel as one. It is still a std::exception, so it cannot escape a catch-all.
	try
	{
		throwIfCancelled(SignalledSource().get_token());
		FAIL("expected Cancelled");
	}
	catch (const std::runtime_error&)
	{
		FAIL("Cancelled must not be caught as a failure");
	}
	catch (const std::exception&)
	{
		SUCCEED();
	}
}

TEST_CASE("WriteTextures honours the cancel token", "[cancel][bmesh][io]")
{
	const ScratchDir dir("bernini_cancel_textures");
	const auto       mesh = ImportWithTextures(3);

	// Asked rather than spelled out: these images are unnamed, so what they are called is a hash of
	// their content and this test is about the cancel, not the naming.
	const std::vector<std::string> names = assetlib::importedTextureFileNames(mesh);
	REQUIRE(names.size() == 3);

	SECTION("a token signalled up front writes nothing at all")
	{
		REQUIRE_THROWS_AS(
			StoreAt(dir.path).WriteTextures(mesh, "textures", {}, SignalledSource().get_token()),
			Cancelled);

		REQUIRE_FALSE(std::filesystem::exists(dir.path / "textures" / names[0]));
	}

	SECTION("cancelling part-way stops before the next encode, keeping what was already written")
	{
		// The token is polled before each texture, so requesting the stop while the first one is being
		// reported means the first is written and the second is never begun.
		auto   source = std::stop_source();
		size_t calls  = 0;

		const auto onProgress = [&](size_t done, size_t) {
			++calls;
			if (done == 0)
				source.request_stop();
		};

		REQUIRE_THROWS_AS(
			StoreAt(dir.path).WriteTextures(mesh, "textures", onProgress, source.get_token()),
			Cancelled);

		REQUIRE(calls == 1);
		REQUIRE(std::filesystem::exists(dir.path / "textures" / names[0]));
		REQUIRE_FALSE(std::filesystem::exists(dir.path / "textures" / names[1]));
		REQUIRE_FALSE(std::filesystem::exists(dir.path / "textures" / names[2]));
	}

	SECTION("an unsignalled token writes every texture")
	{
		REQUIRE_NOTHROW(StoreAt(dir.path).WriteTextures(mesh, "textures"));

		REQUIRE(std::filesystem::exists(dir.path / "textures" / names[0]));
		REQUIRE(std::filesystem::exists(dir.path / "textures" / names[1]));
		REQUIRE(std::filesystem::exists(dir.path / "textures" / names[2]));
	}
}

TEST_CASE("loadFromGltf stops on a signalled token", "[cancel][gltf]")
{
	const std::filesystem::path glb = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(glb));

	REQUIRE_THROWS_AS(loadFromGltf(glb, { .cancel = SignalledSource().get_token() }), Cancelled);
	REQUIRE_NOTHROW(loadFromGltf(glb));
}

TEST_CASE("bakeMaterial stops on a signalled token and leaves the material alone", "[cancel][bake]")
{
	const ScratchDir dir("bernini_cancel_material");
	WriteSource(dir.path / "albedo.ktx2", 16);

	BMaterial mat;
	mat.pbr.routes[0] = { "albedo.ktx2", 0 };
	mat.pbr.routes[1] = { "albedo.ktx2", 1 };
	mat.pbr.routes[2] = { "albedo.ktx2", 2 };

	REQUIRE_THROWS_AS(
		StoreAt(dir.path).BakeMaterial(mat, SignalledSource().get_token()),
		Cancelled);

	// A half-updated material is worse than an unbaked one: it would name maps that are not there. So
	// a cancelled bake must not have touched it.
	REQUIRE(mat.pbr.baseColorTexture.empty());
	REQUIRE(mat.pbr.ormTexture.empty());
	REQUIRE(mat.pbr.normalTexture.empty());
}

TEST_CASE("an OS error naming a directory is reported, not swallowed", "[io][fs]")
{
	const ScratchDir dir("bernini_fs_error");

	// A file where the cook wants a directory: create_directories cannot win, and used to fail
	// silently, leaving the caller to die later inside an encoder blaming the texture instead.
	const auto blocker = dir.path / "occupied";
	{
		std::ofstream out(blocker);
		out << "not a directory";
	}

	REQUIRE_THROWS_AS(createDirectories(blocker / "textures"), std::runtime_error);

	// The message has to name the directory, or it tells the user nothing they can act on.
	try
	{
		createDirectories(blocker / "textures");
	}
	catch (const std::runtime_error& e)
	{
		const auto message = std::string(e.what());
		REQUIRE(message.find("occupied") != std::string::npos);
	}
}

TEST_CASE("a mesh that cannot be written reports why", "[io][fs]")
{
	const ScratchDir dir("bernini_save_error");

	// Saving onto a directory: the stream cannot open it, and the OS's reason is what the editor puts
	// in front of the user.
	const auto occupied = dir.path / "taken";
	std::filesystem::create_directories(occupied);

	auto mesh = BMesh();

	try
	{
		SaveAt(mesh, occupied);
		FAIL("expected save to throw");
	}
	catch (const std::runtime_error& e)
	{
		const auto message = std::string(e.what());
		INFO("message: " << message);

		// Not just "it failed": the path, then the OS's reason after it. That reason is the entire
		// point -- "permission denied" is actionable, "cannot open file for writing" is not.
		REQUIRE(message.find("taken': ") != std::string::npos);
		REQUIRE_FALSE(message.ends_with("'"));
	}
}

// The same guarantee as above, for the four containers that never had it pinned. They report through
// one writer now; before that each carried a private copy of it, and a copy that stopped naming the
// OS's reason would only ever be noticed by whoever could not save.
TEST_CASE("every container that cannot be written reports why", "[io][fs]")
{
	const ScratchDir dir("bernini_save_error_all");

	const auto occupied = dir.path / "taken";
	std::filesystem::create_directories(occupied);

	struct Container
	{
		const char*                                       name;
		std::function<void(const std::filesystem::path&)> write;
	};

	const Container containers[] = {
		{ "bmaterial", [](const std::filesystem::path& p) { SaveAt(BMaterial(), p); } },
		{ "benv", [](const std::filesystem::path& p) { SaveAt(BEnv(), p); } },
		{ "bsky", [](const std::filesystem::path& p) { SaveAt(BSky(), p); } },
		{ "benvl", [](const std::filesystem::path& p) { SaveAt(BEnvLighting(), p); } },
	};

	for (const Container& container : containers)
	{
		INFO("container: " << container.name);
		try
		{
			container.write(occupied);
			FAIL("expected the save to throw");
		}
		catch (const std::runtime_error& e)
		{
			const auto message = std::string(e.what());
			INFO("message: " << message);

			// Names itself, names the path, and ends in the OS's reason rather than in the path.
			REQUIRE(message.starts_with(std::string(container.name) + ": "));
			REQUIRE(message.find("taken': ") != std::string::npos);
			REQUIRE_FALSE(message.ends_with("'"));
		}
	}
}
