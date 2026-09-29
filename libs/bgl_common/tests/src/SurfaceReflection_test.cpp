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

	constexpr std::string_view c_LitContract = R"(import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.LitSurfaceSource;
)";

	// A surface whose fields are ordered to tell the layout rules apart: a float before a float3
	// packs them into one 16-byte span under the scalar rules a raw load reconstructs, and pushes
	// the float3 to 16 under any rule that aligns a vector to its size.
	constexpr std::string_view c_Gate = R"(struct GateParams
{
    [Default(2.0)]
    float power;

    [Color]
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

	std::string
	LitModule(std::string_view body)
	{
		return std::string(c_LitContract) + std::string(body);
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
	CHECK(surface.shading == SurfaceShading::kPbrSurface);
	CHECK(surface.params.byteSize == paramsSize);

	REQUIRE(surface.params.values.size() == 4u);

	CHECK(surface.params.values[0].name == "power");
	CHECK(surface.params.values[0].type == SurfaceValueType::kFloat);
	CHECK(surface.params.values[0].byteOffset == at.power);
	CHECK(surface.params.values[0].defaultValue.x == 2.0f);
	CHECK_FALSE(surface.params.values[0].isColor);

	CHECK(surface.params.values[1].name == "tint");
	CHECK(surface.params.values[1].type == SurfaceValueType::kFloat3);
	CHECK(surface.params.values[1].byteOffset == at.tint);
	CHECK(surface.params.values[1].isColor);
	CHECK(surface.params.values[1].defaultValue.x == 0.2f);
	CHECK(surface.params.values[1].defaultValue.y == 0.6f);
	CHECK(surface.params.values[1].defaultValue.z == 1.0f);
	// A component past the parameter's own is zero, not whatever the attribute's field defaulted to.
	CHECK(surface.params.values[1].defaultValue.w == 0.0f);

	CHECK(surface.params.values[2].name == "quad");
	CHECK(surface.params.values[2].type == SurfaceValueType::kFloat4);
	CHECK(surface.params.values[2].byteOffset == at.quad);
	CHECK(surface.params.values[2].defaultValue == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
	CHECK_FALSE(surface.params.values[2].isColor);

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

// The lit contract reflects under exactly the rules the PBR-surface one does -- same parameter
// walk, same slots -- with the discriminator saying which contract the struct conforms to. The
// module also exercises ISurfaceLight: Shade reads the sun and both environment lookups, so the
// contract's methods are held compilable by this compile.
TEST_CASE("A lit surface reflects with its own contract", "[surface][reflection]")
{
	constexpr std::string_view c_Toon = R"(struct ToonParams
{
    [Color]
    [Default(1.0, 0.5, 0.25)]
    float3 shadowTint;

    [Default(3.0)]
    float bands;

    ColorSlot base;
};

struct ToonSurface : ILitSurfaceSource
{
    typealias MaterialParams = ToonParams;

    static float Coverage<R : IMaterialReader>(R reader, ToonParams params) { return 1.0; }

    static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, ToonParams params)
    {
        let towardSun = -light.SunDirection();
        let facing = max(dot(reader.WorldNormal(), towardSun), 0.0);
        let band = floor(facing * params.bands) / params.bands;
        let ambient = light.Irradiance(reader.WorldNormal())
            + light.Reflection(reader.WorldNormal(), 1.0);
        let base = reader.Sample(params.base, reader.Uv());
        let lit = lerp(params.shadowTint, light.SunRadiance(), band);
        return float4(base.rgb * (lit + ambient), base.a);
    }
};
)";

	Session                               session;
	const std::optional<ReflectedSurface> reflected =
		ReflectSurface(session.Load("Toon", LitModule(c_Toon)), "Toon");
	REQUIRE(reflected.has_value());
	const SurfaceType& surface = reflected->type;

	CHECK(reflected->sourceTypeName == "ToonSurface");
	CHECK(surface.name == "Toon");
	CHECK(surface.kind == MaterialType::kInvalid);
	CHECK(surface.shading == SurfaceShading::kLit);

	REQUIRE(surface.params.values.size() == 2u);
	CHECK(surface.params.values[0].name == "shadowTint");
	CHECK(surface.params.values[0].type == SurfaceValueType::kFloat3);
	CHECK(surface.params.values[0].isColor);
	CHECK(surface.params.values[1].name == "bands");
	CHECK(surface.params.values[1].defaultValue.x == 3.0f);

	REQUIRE(surface.params.textures.size() == 1u);
	CHECK(surface.params.textures[0].name == "base");
	CHECK(surface.params.textures[0].kind == SurfaceTextureKind::kColor);
	CHECK(surface.params.textures[0].index == 0u);
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

	SECTION("nothing conforms to the lit contract it imported")
	{
		CHECK_THROWS_MATCHES(
			ReflectSurface(
				session.Load("LoneLit", LitModule("struct LoneLit { float value; };\n")),
				"LoneLit"),
			std::runtime_error,
			Catch::Matchers::Message(
				"surface 'LoneLit': no struct in the module conforms to ILitSurfaceSource"));
	}

	SECTION("nothing conforms to either contract it imported")
	{
		const std::string body = std::string(c_LitContract) + "struct Neither { float value; };\n";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Neither", Module(body)), "Neither"),
			std::runtime_error,
			Catch::Matchers::Message(
				"surface 'Neither': no struct in the module conforms to "
				"ISurfaceSource or ILitSurfaceSource"));
	}

	SECTION("one struct conforms to each contract")
	{
		const std::string body =
			std::string(c_LitContract) + R"(struct SplitParams { float value; };

struct PbrHalf : ISurfaceSource
{
    typealias MaterialParams = SplitParams;
    static float Coverage<R : IMaterialReader>(R reader, SplitParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, SplitParams params) { return PbrSurface(); }
};

struct LitHalf : ILitSurfaceSource
{
    typealias MaterialParams = SplitParams;
    static float Coverage<R : IMaterialReader>(R reader, SplitParams params) { return 1.0; }
    static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, SplitParams params) { return float4(0.0); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Split", Module(body)), "Split"),
			std::runtime_error,
			Catch::Matchers::MessageMatches(
				ContainsSubstring("both conform to a surface contract")));
	}

	SECTION("one struct conforms to both contracts")
	{
		const std::string body =
			std::string(c_LitContract) + R"(struct GreedyParams { float value; };

struct GreedySurface : ISurfaceSource, ILitSurfaceSource
{
    typealias MaterialParams = GreedyParams;
    static float Coverage<R : IMaterialReader>(R reader, GreedyParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, GreedyParams params) { return PbrSurface(); }
    static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, GreedyParams params) { return float4(0.0); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Greedy", Module(body)), "Greedy"),
			std::runtime_error,
			Catch::Matchers::MessageMatches(ContainsSubstring(
				"conforms to ISurfaceSource and ILitSurfaceSource; a surface owns one contract")));
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

	SECTION("a colour narrower than a colour")
	{
		constexpr std::string_view c_Body = R"(struct NarrowParams
{
    [Color]
    float2 pair;
};

struct NarrowSurface : ISurfaceSource
{
    typealias MaterialParams = NarrowParams;
    static float Coverage<R : IMaterialReader>(R reader, NarrowParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, NarrowParams params) { return PbrSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Narrow", Module(c_Body)), "Narrow"),
			std::runtime_error,
			Catch::Matchers::MessageMatches(
				ContainsSubstring("'pair' is [Color] but has 2 component(s)")));
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

namespace
{
	// Every contract, imported beside the surface, so a lookup of the ones it does not conform to
	// resolves -- and so the toon modules are shown not to clash with the others.
	constexpr std::string_view c_AllContracts = R"(import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;
import bgl.SurfaceLight;
import bgl.LitSurfaceSource;
import bgl.ToonCharacterSurface;
import bgl.ToonEnvironmentSurface;
)";

	// A game's character and environment surfaces, as the test project writes them: base colour
	// through a colour slot, each on its own toon contract.
	constexpr std::string_view c_ToonCharacter = R"(

struct FlatParams
{
    [Color]
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 baseColorFactor;

    ColorSlot baseColor;
};

struct FlatCharacter : IToonCharacterSurfaceSource
{
    typealias MaterialParams = FlatParams;

    static float Coverage<R : IMaterialReader>(R reader, FlatParams params)
    {
        return params.baseColorFactor.a * reader.Sample(params.baseColor, reader.Uv()).a;
    }

    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, FlatParams params)
    {
        ToonCharacterSurface surface = ToonCharacterSurface();
        surface.baseColor = params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());
        return surface;
    }
};
)";

	constexpr std::string_view c_ToonEnvironment = R"(

struct FlatParams
{
    [Color]
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 baseColorFactor;

    ColorSlot baseColor;
};

struct FlatEnvironment : IToonEnvironmentSurfaceSource
{
    typealias MaterialParams = FlatParams;

    static float Coverage<R : IMaterialReader>(R reader, FlatParams params)
    {
        return params.baseColorFactor.a * reader.Sample(params.baseColor, reader.Uv()).a;
    }

    static ToonEnvironmentSurface Evaluate<R : IMaterialReader>(R reader, FlatParams params)
    {
        ToonEnvironmentSurface surface = ToonEnvironmentSurface();
        surface.baseColor = params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());
        return surface;
    }
};
)";

	bool
	Conforms(slang::IModule* module, const char* structName, const char* interfaceName)
	{
		slang::ProgramLayout*  layout = module->getLayout();
		slang::TypeReflection* type   = layout->findTypeByName(structName);
		slang::TypeReflection* iface  = layout->findTypeByName(interfaceName);
		REQUIRE(type != nullptr);
		REQUIRE(iface != nullptr);
		return layout->isSubType(type, iface);
	}
}

// The toon contracts compile as a game writes against them, and are two contracts rather than one
// under two names: a character surface is not an environment surface, and neither is a PBR or a lit
// one. What this cannot show is registration: which shading ReflectSurface reports a toon surface
// under, and that it draws.
TEST_CASE(
	"A toon surface conforms to its own model's contract alone",
	"[surface][reflection][toon]")
{
	Session session;

	SECTION("character")
	{
		slang::IModule* module = session.Load(
			"ToonCharacterFlat",
			std::string(c_AllContracts) + std::string(c_ToonCharacter));
		CHECK(Conforms(module, "FlatCharacter", "IToonCharacterSurfaceSource"));
		CHECK_FALSE(Conforms(module, "FlatCharacter", "IToonEnvironmentSurfaceSource"));
		CHECK_FALSE(Conforms(module, "FlatCharacter", "ISurfaceSource"));
		CHECK_FALSE(Conforms(module, "FlatCharacter", "ILitSurfaceSource"));
	}

	SECTION("environment")
	{
		slang::IModule* module = session.Load(
			"ToonEnvironmentFlat",
			std::string(c_AllContracts) + std::string(c_ToonEnvironment));
		CHECK(Conforms(module, "FlatEnvironment", "IToonEnvironmentSurfaceSource"));
		CHECK_FALSE(Conforms(module, "FlatEnvironment", "IToonCharacterSurfaceSource"));
		CHECK_FALSE(Conforms(module, "FlatEnvironment", "ISurfaceSource"));
		CHECK_FALSE(Conforms(module, "FlatEnvironment", "ILitSurfaceSource"));
	}
}

// Registration's half of the toon contracts: each reflects under its own shading, with its
// parameters laid out exactly as a PBR or lit surface's are, since the record is the same record.
TEST_CASE("A toon surface reflects under its own model", "[surface][reflection][toon]")
{
	Session session;

	SECTION("character")
	{
		const std::optional<ReflectedSurface> reflected = ReflectSurface(
			session.Load(
				"ToonCharacterFlat",
				std::string(c_AllContracts) + std::string(c_ToonCharacter)),
			"ToonCharacterFlat");
		REQUIRE(reflected.has_value());
		CHECK(reflected->sourceTypeName == "FlatCharacter");
		CHECK(reflected->type.shading == SurfaceShading::kToonCharacter);
		REQUIRE(reflected->type.params.values.size() == 1u);
		CHECK(reflected->type.params.values[0].name == "baseColorFactor");
		CHECK(reflected->type.params.values[0].isColor);
		REQUIRE(reflected->type.params.textures.size() == 1u);
		CHECK(reflected->type.params.textures[0].name == "baseColor");
	}

	SECTION("environment")
	{
		const std::optional<ReflectedSurface> reflected = ReflectSurface(
			session.Load(
				"ToonEnvironmentFlat",
				std::string(c_AllContracts) + std::string(c_ToonEnvironment)),
			"ToonEnvironmentFlat");
		REQUIRE(reflected.has_value());
		CHECK(reflected->sourceTypeName == "FlatEnvironment");
		CHECK(reflected->type.shading == SurfaceShading::kToonEnvironment);
	}
}

TEST_CASE("A toon surface is held to one contract", "[surface][reflection][toon]")
{
	using Catch::Matchers::ContainsSubstring;

	Session session;

	// Two toon contracts cannot meet on one struct -- their Evaluates differ by return type alone,
	// which Slang refuses -- but a toon and a lit one can, having no member in common but Coverage.
	SECTION("one struct conforms to a toon model and to the lit contract")
	{
		const std::string body =
			std::string(c_AllContracts) + R"(struct BothParams { float value; };

struct Both : ILitSurfaceSource, IToonCharacterSurfaceSource
{
    typealias MaterialParams = BothParams;
    static float Coverage<R : IMaterialReader>(R reader, BothParams params) { return 1.0; }
    static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, BothParams params) { return float4(0.0); }
    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, BothParams params) { return ToonCharacterSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Both", body), "Both"),
			std::runtime_error,
			Catch::Matchers::MessageMatches(ContainsSubstring(
				"conforms to ILitSurfaceSource and IToonCharacterSurfaceSource; a surface owns one "
				"contract")));
	}

	SECTION("a character surface and an environment surface in one file")
	{
		std::string body = std::string(c_AllContracts) + std::string(c_ToonCharacter);
		body += R"(
struct AlsoEnvironment : IToonEnvironmentSurfaceSource
{
    typealias MaterialParams = FlatParams;
    static float Coverage<R : IMaterialReader>(R reader, FlatParams params) { return 1.0; }
    static ToonEnvironmentSurface Evaluate<R : IMaterialReader>(R reader, FlatParams params) { return ToonEnvironmentSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Pair", body), "Pair"),
			std::runtime_error,
			Catch::Matchers::MessageMatches(
				ContainsSubstring("both conform to a surface contract")));
	}

	SECTION("nothing conforms to any contract it imported")
	{
		const std::string body = std::string(c_AllContracts) + "struct Nothing { float value; };\n";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Nothing", body), "Nothing"),
			std::runtime_error,
			Catch::Matchers::Message(
				"surface 'Nothing': no struct in the module conforms to ISurfaceSource, "
				"ILitSurfaceSource, IToonCharacterSurfaceSource or IToonEnvironmentSurfaceSource"));
	}
}
