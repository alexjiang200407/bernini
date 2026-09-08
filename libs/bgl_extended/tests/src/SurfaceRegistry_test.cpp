#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl/glm.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

using namespace bgl;

namespace
{
	bgl::GraphicsOptions
	SurfaceOptions(const std::filesystem::path& dir)
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		opts.surfaceShaderDir         = dir;
		return opts;
	}

	// A directory of this case's own, so a refusal is written by the test rather than by whatever
	// else happens to be staged.
	std::filesystem::path
	FreshDir(std::string_view name)
	{
		const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		return dir;
	}

	void
	WriteSurface(const std::filesystem::path& path, std::string_view body)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << "import bgl.MaterialReader;\nimport bgl.PbrSurface;\nimport bgl.SurfaceSource;\n"
			<< body;
	}

	// A surface that reflects and binds, for the cases that only care how many there are.
	constexpr std::string_view c_Trivial = R"(struct FillerParams { float unused; };

struct FillerSurface : ISurfaceSource
{
    typealias MaterialParams = FillerParams;
    static float Coverage<R : IMaterialReader>(R reader, FillerParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, FillerParams params) { return PbrSurface(); }
};
)";
}

// The whole registration path, and the only one that proves the binding compiles: every reserved
// slot's pipelines are built by the constructor this returns from, and slot 0's now instantiate the
// engine's record on RimSurface rather than on the null surface the tree ships.
TEST_CASE("A surface directory fills the reserved slots in filename order", "[surface][registry]")
{
	auto gfx = bgl::CreateGraphics(SurfaceOptions("./shaders/tests/surfaces"));
	REQUIRE(gfx != nullptr);

	const std::span<const SurfaceType> types = gfx->GetSurfaceTypes();
	REQUIRE(types.size() == 3u);

	// Filename order, so the directory alone decides which slot a surface lands in.
	CHECK(types[0].name == "PbrLike");
	CHECK(types[0].kind == MaterialType::kGameStart);
	CHECK(types[1].name == "Rim");
	CHECK(types[2].name == "Tint");
	CHECK(
		types[2].kind ==
		static_cast<MaterialType>(static_cast<uint32_t>(MaterialType::kGameStart) + 2u));

	const SurfaceParams& rim = types[1].params;
	REQUIRE(rim.values.size() == 2u);
	CHECK(rim.values[0].name == "rimColor");
	CHECK(rim.values[0].type == SurfaceValueType::kFloat3);
	CHECK(rim.values[0].defaultValue == glm::vec4(0.2f, 0.6f, 1.0f, 0.0f));
	CHECK(rim.values[1].name == "rimPower");
	CHECK(rim.values[1].defaultValue.x == 3.0f);

	REQUIRE(rim.textures.size() == 1u);
	CHECK(rim.textures[0].name == "baseColor");
	CHECK(rim.textures[0].kind == SurfaceTextureKind::kColor);
	CHECK(rim.textures[0].index == 0u);

	// A surface with no texture at all still registers; the record's handles simply go unread.
	CHECK(types[2].params.textures.empty());
	REQUIRE(types[2].params.values.size() == 1u);
	CHECK(types[2].params.values[0].name == "tint");
}

// Naming no directory is not an error -- it is what every client that has no surfaces does, which
// is all of them until one is written.
TEST_CASE("No surface directory registers nothing", "[surface][registry]")
{
	auto gfx = bgl::CreateGraphics(SurfaceOptions({}));
	REQUIRE(gfx != nullptr);
	CHECK(gfx->GetSurfaceTypes().empty());
}

// Each refusal is the client's mistake, so each is an ApiError naming the file rather than an
// assertion inside the compiler.
TEST_CASE("A surface directory the engine cannot register is refused", "[surface][registry]")
{
	using Catch::Matchers::ContainsSubstring;
	using Catch::Matchers::MessageMatches;

	SECTION("a directory that is not there")
	{
		CHECK_THROWS_MATCHES(
			bgl::CreateGraphics(SurfaceOptions("./shaders/tests/no_such_surfaces")),
			ApiError,
			MessageMatches(ContainsSubstring("is not a directory")));
	}

	SECTION("a fifth surface")
	{
		const std::filesystem::path dir = FreshDir("bernini_surfaces_five");
		for (const char* name : { "A", "B", "C", "D", "E" })
			WriteSurface(dir / (std::string(name) + ".slang"), c_Trivial);

		CHECK_THROWS_MATCHES(
			bgl::CreateGraphics(SurfaceOptions(dir)),
			ApiError,
			MessageMatches(ContainsSubstring("'E' is past the last")));
	}

	SECTION("a file that means to be a surface and is not one")
	{
		const std::filesystem::path dir = FreshDir("bernini_surfaces_hollow");
		WriteSurface(dir / "Hollow.slang", "struct Lonely { float value; };\n");

		CHECK_THROWS_MATCHES(
			bgl::CreateGraphics(SurfaceOptions(dir)),
			ApiError,
			MessageMatches(
				ContainsSubstring("no struct in the module conforms to ISurfaceSource")));
	}

	// The likeliest mistake of all, and the one that must not take the process with it: every other
	// path that compiles a shader here reports a diagnostic by aborting, because everywhere else the
	// source is the engine's own and a diagnostic is a bug.
	SECTION("a file that does not compile")
	{
		const std::filesystem::path dir = FreshDir("bernini_surfaces_broken");
		WriteSurface(dir / "Broken.slang", "struct Half { float value\n");

		CHECK_THROWS_MATCHES(
			bgl::CreateGraphics(SurfaceOptions(dir)),
			ApiError,
			MessageMatches(ContainsSubstring("surface 'Broken': its module did not compile")));
	}
}

// The directory is the game's own module search path as well as where its surfaces live, so most of
// what sits in it is the game's code and none of the engine's business. Only a module that imports
// the contract is a surface, and only that one is held to it -- which is what lets a game keep a
// shared header beside the surfaces that import it.
TEST_CASE("A module beside the surfaces is not one of them", "[surface][registry]")
{
	const std::filesystem::path dir = FreshDir("bernini_surfaces_mixed");

	// Not a surface: it never imports the contract, so nothing in it could conform.
	{
		std::ofstream out(dir / "Palette.slang", std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << "public static const float3 cWarm = float3(1.0, 0.8, 0.6);\n";
	}

	// A name no shader could import, which is therefore no surface either.
	WriteSurface(dir / "not-a-name.slang", c_Trivial);

	WriteSurface(dir / "Only.slang", c_Trivial);

	auto gfx = bgl::CreateGraphics(SurfaceOptions(dir));
	REQUIRE(gfx != nullptr);

	const std::span<const SurfaceType> types = gfx->GetSurfaceTypes();
	REQUIRE(types.size() == 1u);
	CHECK(types[0].name == "Only");
	CHECK(types[0].kind == MaterialType::kGameStart);
}
