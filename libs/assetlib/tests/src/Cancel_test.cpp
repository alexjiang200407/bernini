#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/cancel.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>

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

	// An import carrying `count` tiny textures and nothing else -- enough for writeTextures, and small
	// enough that the Basis encode of each is quick.
	imp::BMeshImport
	importWithTextures(size_t count)
	{
		auto mesh = imp::BMeshImport();
		mesh.stringPool.push_back('\0');  // offset 0 == empty string

		const std::vector<std::byte> rgba(4 * 4 * 4, std::byte{ 128 });
		for (size_t i = 0; i < count; ++i) mesh.textures.push_back(rgba8ToImage(rgba, 4, 4));

		return mesh;
	}

	// Writes a `size` x `size` uncompressed RGBA8 .ktx2, for a material to route at.
	void
	writeSource(const std::filesystem::path& path, uint32_t size)
	{
		const std::vector<std::byte> pixels(static_cast<size_t>(size) * size * 4, std::byte{ 200 });
		writeKTX2(rgba8ToImage(pixels, size, size), path, false, Ktx2Compression::kNone);
	}

	// A token that is already signalled, as one is when the user cancels before the cook gets there.
	std::stop_source
	signalledSource()
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
		throwIfCancelled(signalledSource().get_token());
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

TEST_CASE("writeTextures honours the cancel token", "[cancel][bmesh][io]")
{
	const ScratchDir dir("bernini_cancel_textures");
	const auto       mesh = importWithTextures(3);

	SECTION("a token signalled up front writes nothing at all")
	{
		REQUIRE_THROWS_AS(
			writeTextures(mesh, dir.path, {}, signalledSource().get_token()),
			Cancelled);

		REQUIRE_FALSE(std::filesystem::exists(dir.path / "tex0.ktx2"));
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

		REQUIRE_THROWS_AS(writeTextures(mesh, dir.path, onProgress, source.get_token()), Cancelled);

		REQUIRE(calls == 1);
		REQUIRE(std::filesystem::exists(dir.path / "tex0.ktx2"));
		REQUIRE_FALSE(std::filesystem::exists(dir.path / "tex1.ktx2"));
		REQUIRE_FALSE(std::filesystem::exists(dir.path / "tex2.ktx2"));
	}

	SECTION("an unsignalled token writes every texture")
	{
		REQUIRE_NOTHROW(writeTextures(mesh, dir.path));

		REQUIRE(std::filesystem::exists(dir.path / "tex0.ktx2"));
		REQUIRE(std::filesystem::exists(dir.path / "tex1.ktx2"));
		REQUIRE(std::filesystem::exists(dir.path / "tex2.ktx2"));
	}
}

TEST_CASE("bake stops on a signalled token", "[cancel][bmesh][bake]")
{
	const ScratchDir dir("bernini_cancel_bake");
	const auto       mesh = importWithTextures(2);

	REQUIRE_THROWS_AS(bake(mesh, dir.path, "cancelled", signalledSource().get_token()), Cancelled);

	// The .bmesh is written last, so a cancelled bake never leaves a container behind that names
	// textures and materials it never got round to emitting.
	REQUIRE_FALSE(std::filesystem::exists(dir.path / "cancelled.bmesh"));
}

TEST_CASE("loadFromGltf stops on a signalled token", "[cancel][gltf]")
{
	const std::filesystem::path glb = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(glb));

	REQUIRE_THROWS_AS(loadFromGltf(glb, signalledSource().get_token()), Cancelled);
	REQUIRE_NOTHROW(loadFromGltf(glb));
}

TEST_CASE("bakeMaterial stops on a signalled token and leaves the material alone", "[cancel][bake]")
{
	const ScratchDir dir("bernini_cancel_material");
	writeSource(dir.path / "albedo.ktx2", 16);

	BMaterial mat;
	mat.mode      = MaterialMode::kLoose;
	mat.routes[0] = { "albedo.ktx2", 0 };
	mat.routes[1] = { "albedo.ktx2", 1 };
	mat.routes[2] = { "albedo.ktx2", 2 };

	REQUIRE_THROWS_AS(
		bakeMaterial(mat, MaterialBakeDesc{ dir.path }, signalledSource().get_token()),
		Cancelled);

	// A half-updated material is worse than an unbaked one: it would name maps that are not there and
	// claim to be baked. So a cancelled bake must not have touched it.
	REQUIRE(mat.mode == MaterialMode::kLoose);
	REQUIRE(mat.baseColorTexture.empty());
	REQUIRE(mat.ormTexture.empty());
	REQUIRE(mat.normalTexture.empty());
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
		save(mesh, occupied);
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
