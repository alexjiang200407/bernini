#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl_common/SurfaceReflection.h>

#include <bgl/glm.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <optional>
#include <slang-com-ptr.h>
#include <slang.h>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace bgl;

namespace
{
	// The contract is staged beside the suite, so a surface here imports bgl.* exactly as a game's
	// does at runtime -- and the target is a GPU one, because the parameter layout under test is
	// the one a shader reads back and the host target has no structured buffer to lay out.
	class Session
	{
	public:
		explicit Session(SlangCompileTarget format = SLANG_DXIL)
		{
			REQUIRE(SLANG_SUCCEEDED(slang::createGlobalSession(m_Global.writeRef())));

			slang::TargetDesc target{};
			target.format  = format;
			target.profile = m_Global->findProfile("sm_6_6");

			const char* searchPath = "./shaders/src";

			slang::SessionDesc desc{};
			desc.targets         = &target;
			desc.targetCount     = 1;
			desc.searchPaths     = &searchPath;
			desc.searchPathCount = 1;

			REQUIRE(SLANG_SUCCEEDED(m_Global->createSession(desc, m_Session.writeRef())));
		}

		slang::IModule*
		Load(std::string_view name, std::string_view source)
		{
			const std::string moduleName(name);
			const std::string path = moduleName + ".slang";
			const std::string text(source);

			Slang::ComPtr<slang::IBlob> diagnostics;
			slang::IModule*             module = m_Session->loadModuleFromSourceString(
				moduleName.c_str(),
				path.c_str(),
				text.c_str(),
				diagnostics.writeRef());

			const std::string reported =
				diagnostics != nullptr ?
					std::string(static_cast<const char*>(diagnostics->getBufferPointer())) :
					std::string("no diagnostic");
			INFO(reported);
			REQUIRE(module != nullptr);
			return module;
		}

	private:
		Slang::ComPtr<slang::IGlobalSession> m_Global;
		Slang::ComPtr<slang::ISession>       m_Session;
	};

	constexpr std::string_view c_Contract = R"(import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;
)";

	// A surface whose fields are ordered to tell the layout rules apart: a float before a float3
	// packs them into one 16-byte span under the scalar rules a raw load reconstructs, and pushes
	// the float3 to 16 under any rule that aligns a vector to its size.
	constexpr std::string_view c_Gate = R"(struct GateParams
{
    [Default(2.0)]
    float power;

    [Default(0.2, 0.6, 1.0)]
    float3 tint;

    ColorSlot base;
    NormalSlot bumps;

    [Default(1.0, 2.0, 3.0, 4.0)]
    float4 quad;

    float unset;
};

struct GateSurface : ISurfaceSource
{
    typealias MaterialParams = GateParams;

    static float Coverage<R : IMaterialReader>(R reader, GateParams params) { return 1.0; }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, GateParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.emissive = params.tint * params.power;
        surface.baseColor = reader.Sample(params.base, reader.Uv()) * params.quad + params.unset;
        surface.normalXY = reader.Sample(params.bumps, reader.Uv()).xy;
        return surface;
    }
};
)";

	std::string
	Module(std::string_view body)
	{
		return std::string(c_Contract) + std::string(body);
	}

	// Where GateParams' six fields land, in declaration order.
	struct Offsets
	{
		uint32_t power;
		uint32_t tint;
		uint32_t base;
		uint32_t bumps;
		uint32_t quad;
		uint32_t unset;
	};
}

// The whole reflection in one pass: the fields in declaration order, at the offsets the target puts
// them at, with the textures taken out of the value list and numbered as the record's handles are.
//
// Both targets are pinned because they disagree, and the disagreement is a trap rather than a
// choice. A record is read with RawBuffer.Load<T>, which reconstructs its type from scalar loads on
// every backend, so the scalar column is the one a caller must reflect under. The MSL column is the
// layout of a *structured buffer's element* on that target -- true for EntryBuffer<T> and wrong
// here, and wrong quietly: every field after the first vector moves.
TEST_CASE("A surface's parameters are reflected at their target's offsets", "[surface][reflection]")
{
	uint32_t           paramsSize = 0;
	Offsets            at{};
	SlangCompileTarget format = SLANG_DXIL;

	SECTION("the scalar rules, which is what a raw load reads on every backend")
	{
		format     = SLANG_DXIL;
		paramsSize = 44u;
		at         = { 0u, 4u, 16u, 20u, 24u, 40u };
	}

	SECTION("MSL's rules for a structured buffer, which no record is read under")
	{
		format     = SLANG_METAL;
		paramsSize = 80u;
		at         = { 0u, 16u, 32u, 36u, 48u, 64u };
	}

	Session                               session(format);
	const std::optional<ReflectedSurface> reflected =
		ReflectSurface(session.Load("Gate", Module(c_Gate)), "Gate");
	REQUIRE(reflected.has_value());
	const SurfaceType& surface = reflected->type;

	// The binding module writes a typealias to this, so it is the struct and not the file.
	CHECK(reflected->sourceTypeName == "GateSurface");

	CHECK(surface.name == "Gate");
	// Registration's to assign, not reflection's.
	CHECK(surface.kind == MaterialType::kInvalid);
	CHECK(surface.params.byteSize == paramsSize);

	REQUIRE(surface.params.values.size() == 4u);

	CHECK(surface.params.values[0].name == "power");
	CHECK(surface.params.values[0].type == SurfaceValueType::kFloat);
	CHECK(surface.params.values[0].byteOffset == at.power);
	CHECK(surface.params.values[0].defaultValue.x == 2.0f);

	CHECK(surface.params.values[1].name == "tint");
	CHECK(surface.params.values[1].type == SurfaceValueType::kFloat3);
	CHECK(surface.params.values[1].byteOffset == at.tint);
	CHECK(surface.params.values[1].defaultValue.x == 0.2f);
	CHECK(surface.params.values[1].defaultValue.y == 0.6f);
	CHECK(surface.params.values[1].defaultValue.z == 1.0f);
	// A component past the parameter's own is zero, not whatever the attribute's field defaulted to.
	CHECK(surface.params.values[1].defaultValue.w == 0.0f);

	CHECK(surface.params.values[2].name == "quad");
	CHECK(surface.params.values[2].type == SurfaceValueType::kFloat4);
	CHECK(surface.params.values[2].byteOffset == at.quad);
	CHECK(surface.params.values[2].defaultValue == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));

	// No attribute is zero, which is also what a material that sets nothing writes.
	CHECK(surface.params.values[3].name == "unset");
	CHECK(surface.params.values[3].byteOffset == at.unset);
	CHECK(surface.params.values[3].defaultValue == glm::vec4(0.0f));

	REQUIRE(surface.params.textures.size() == 2u);

	CHECK(surface.params.textures[0].name == "base");
	CHECK(surface.params.textures[0].kind == SurfaceTextureKind::kColor);
	CHECK(surface.params.textures[0].index == 0u);
	CHECK(surface.params.textures[0].byteOffset == at.base);

	CHECK(surface.params.textures[1].name == "bumps");
	CHECK(surface.params.textures[1].kind == SurfaceTextureKind::kNormal);
	CHECK(surface.params.textures[1].index == 1u);
	CHECK(surface.params.textures[1].byteOffset == at.bumps);
}

// Each of the contract's four slot types is its own kind, and nothing but the declared type says
// so.
TEST_CASE("A texture's kind is its declared type", "[surface][reflection]")
{
	constexpr std::string_view c_Kinds = R"(struct KindParams
{
    CoverageSlot cutout;
    DataSlot orm;
    NormalSlot bumps;
    ColorSlot albedo;
};

struct KindSurface : ISurfaceSource
{
    typealias MaterialParams = KindParams;

    static float Coverage<R : IMaterialReader>(R reader, KindParams params)
    {
        return reader.Sample(params.cutout, reader.Uv()).r;
    }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, KindParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor = reader.Sample(params.albedo, reader.Uv());
        surface.orm = reader.Sample(params.orm, reader.Uv()).rgb;
        surface.normalXY = reader.Sample(params.bumps, reader.Uv()).xy;
        return surface;
    }
};
)";

	Session                               session;
	const std::optional<ReflectedSurface> reflected =
		ReflectSurface(session.Load("Kinds", Module(c_Kinds)), "Kinds");
	REQUIRE(reflected.has_value());
	const SurfaceType& surface = reflected->type;

	CHECK(surface.params.values.empty());
	REQUIRE(surface.params.textures.size() == 4u);
	CHECK(surface.params.textures[0].kind == SurfaceTextureKind::kCoverage);
	CHECK(surface.params.textures[1].kind == SurfaceTextureKind::kData);
	CHECK(surface.params.textures[2].kind == SurfaceTextureKind::kNormal);
	CHECK(surface.params.textures[3].kind == SurfaceTextureKind::kColor);
}

// A module that never imported the contract is not a failed surface, it is the game's own code:
// the same directory is its module search path, so most of what sits there is nothing the engine
// has an opinion about. Anything that does import the contract is held to it, below.
TEST_CASE("A module that is not a surface reflects to nothing", "[surface][reflection]")
{
	Session session;
	CHECK_FALSE(ReflectSurface(session.Load("Bare", "struct Bare { float value; };\n"), "Bare")
	                .has_value());
}

// Every refusal is a named throw, because each one is a mistake in a file the engine does not own
// and the message is all its author gets.
TEST_CASE("A module the engine cannot draw from is refused by name", "[surface][reflection]")
{
	using Catch::Matchers::ContainsSubstring;

	Session session;

	SECTION("no struct conforms")
	{
		constexpr std::string_view c_Body = R"(struct Lonely
{
    float value;
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Lonely", Module(c_Body)), "Lonely"),
			std::runtime_error,
			Catch::Matchers::Message(
				"surface 'Lonely': no struct in the module conforms to ISurfaceSource"));
	}

	SECTION("two structs conform")
	{
		constexpr std::string_view c_Body = R"(struct TwoParams { float value; };

struct FirstSurface : ISurfaceSource
{
    typealias MaterialParams = TwoParams;
    static float Coverage<R : IMaterialReader>(R reader, TwoParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, TwoParams params) { return PbrSurface(); }
};

struct SecondSurface : ISurfaceSource
{
    typealias MaterialParams = TwoParams;
    static float Coverage<R : IMaterialReader>(R reader, TwoParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, TwoParams params) { return PbrSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Two", Module(c_Body)), "Two"),
			std::runtime_error,
			Catch::Matchers::MessageMatches(ContainsSubstring("both conform to ISurfaceSource")));
	}

	SECTION("a ninth texture")
	{
		constexpr std::string_view c_Body = R"(struct NineParams
{
    ColorSlot a; ColorSlot b; ColorSlot c; ColorSlot d;
    ColorSlot e; ColorSlot f; ColorSlot g; ColorSlot h;
    ColorSlot ninth;
};

struct NineSurface : ISurfaceSource
{
    typealias MaterialParams = NineParams;
    static float Coverage<R : IMaterialReader>(R reader, NineParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, NineParams params) { return PbrSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Nine", Module(c_Body)), "Nine"),
			std::runtime_error,
			Catch::Matchers::MessageMatches(
				ContainsSubstring("texture 'ninth' is past the 8 a record carries")));
	}

	SECTION("a parameter the engine cannot pack")
	{
		constexpr std::string_view c_Body = R"(struct IntParams
{
    uint level;
};

struct IntSurface : ISurfaceSource
{
    typealias MaterialParams = IntParams;
    static float Coverage<R : IMaterialReader>(R reader, IntParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, IntParams params) { return PbrSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Ints", Module(c_Body)), "Ints"),
			std::runtime_error,
			Catch::Matchers::MessageMatches(
				ContainsSubstring("'level' is not a float or a float vector")));
	}
}
