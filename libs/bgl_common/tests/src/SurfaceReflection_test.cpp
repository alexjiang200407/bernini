#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl_common/SurfaceReflection.h>

#include <bgl/glm.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <slang-com-ptr.h>
#include <slang.h>
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
    typealias Params = GateParams;

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

// The whole reflection in one pass: the fields in declaration order, at the offsets the target
// puts them at, with the slots taken out of the parameter list and numbered as the record's handles
// are. Both targets are pinned because they disagree, which is why a surface is reflected once per
// device: a record packed under one backend's offsets is read as noise by the other.
TEST_CASE("A surface's parameters are reflected at their target's offsets", "[surface][reflection]")
{
	uint32_t           paramsSize = 0;
	Offsets            at{};
	SlangCompileTarget format = SLANG_DXIL;

	SECTION("the scalar rules, which is what DXIL reads")
	{
		format     = SLANG_DXIL;
		paramsSize = 44;
		at         = { 0, 4, 16, 20, 24, 40 };
	}

	SECTION("MSL's rules, which align a vector to its own width")
	{
		format     = SLANG_METAL;
		paramsSize = 80;
		at         = { 0, 16, 32, 36, 48, 64 };
	}

	Session     session(format);
	SurfaceType surface = ReflectSurface(session.Load("Gate", Module(c_Gate)), "Gate");

	CHECK(surface.name == "Gate");
	// Registration's to assign, not reflection's.
	CHECK(surface.slot == 0);
	CHECK(surface.paramsSize == paramsSize);

	REQUIRE(surface.parameters.size() == 4);

	CHECK(surface.parameters[0].name == "power");
	CHECK(surface.parameters[0].type == SurfaceParamType::kFloat);
	CHECK(surface.parameters[0].offset == at.power);
	CHECK(surface.parameters[0].defaultValue.x == 2.0f);

	CHECK(surface.parameters[1].name == "tint");
	CHECK(surface.parameters[1].type == SurfaceParamType::kFloat3);
	CHECK(surface.parameters[1].offset == at.tint);
	CHECK(surface.parameters[1].defaultValue.x == 0.2f);
	CHECK(surface.parameters[1].defaultValue.y == 0.6f);
	CHECK(surface.parameters[1].defaultValue.z == 1.0f);
	// A component past the parameter's own is zero, not whatever the attribute's field defaulted to.
	CHECK(surface.parameters[1].defaultValue.w == 0.0f);

	CHECK(surface.parameters[2].name == "quad");
	CHECK(surface.parameters[2].type == SurfaceParamType::kFloat4);
	CHECK(surface.parameters[2].offset == at.quad);
	CHECK(surface.parameters[2].defaultValue == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));

	// No attribute is zero, which is also what a material that sets nothing writes.
	CHECK(surface.parameters[3].name == "unset");
	CHECK(surface.parameters[3].offset == at.unset);
	CHECK(surface.parameters[3].defaultValue == glm::vec4(0.0f));

	REQUIRE(surface.slots.size() == 2);

	CHECK(surface.slots[0].name == "base");
	CHECK(surface.slots[0].kind == SurfaceSlotKind::kColor);
	CHECK(surface.slots[0].index == 0);
	CHECK(surface.slots[0].offset == at.base);

	CHECK(surface.slots[1].name == "bumps");
	CHECK(surface.slots[1].kind == SurfaceSlotKind::kNormal);
	CHECK(surface.slots[1].index == 1);
	CHECK(surface.slots[1].offset == at.bumps);
}

// Each slot type is its own kind, and nothing but the declared type says so.
TEST_CASE("A slot's kind is its declared type", "[surface][reflection]")
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
    typealias Params = KindParams;

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

	Session     session;
	SurfaceType surface = ReflectSurface(session.Load("Kinds", Module(c_Kinds)), "Kinds");

	CHECK(surface.parameters.empty());
	REQUIRE(surface.slots.size() == 4);
	CHECK(surface.slots[0].kind == SurfaceSlotKind::kCoverage);
	CHECK(surface.slots[1].kind == SurfaceSlotKind::kData);
	CHECK(surface.slots[2].kind == SurfaceSlotKind::kNormal);
	CHECK(surface.slots[3].kind == SurfaceSlotKind::kColor);
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
			ApiError,
			Catch::Matchers::Message(
				"surface 'Lonely': no struct in the module conforms to ISurfaceSource"));
	}

	SECTION("the contract is never imported")
	{
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Bare", "struct Bare { float value; };\n"), "Bare"),
			ApiError,
			Catch::Matchers::MessageMatches(
				ContainsSubstring("does not import bgl.SurfaceSource")));
	}

	SECTION("two structs conform")
	{
		constexpr std::string_view c_Body = R"(struct TwoParams { float value; };

struct FirstSurface : ISurfaceSource
{
    typealias Params = TwoParams;
    static float Coverage<R : IMaterialReader>(R reader, TwoParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, TwoParams params) { return PbrSurface(); }
};

struct SecondSurface : ISurfaceSource
{
    typealias Params = TwoParams;
    static float Coverage<R : IMaterialReader>(R reader, TwoParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, TwoParams params) { return PbrSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Two", Module(c_Body)), "Two"),
			ApiError,
			Catch::Matchers::MessageMatches(ContainsSubstring("both conform to ISurfaceSource")));
	}

	SECTION("a ninth slot")
	{
		constexpr std::string_view c_Body = R"(struct NineParams
{
    ColorSlot a; ColorSlot b; ColorSlot c; ColorSlot d;
    ColorSlot e; ColorSlot f; ColorSlot g; ColorSlot h;
    ColorSlot ninth;
};

struct NineSurface : ISurfaceSource
{
    typealias Params = NineParams;
    static float Coverage<R : IMaterialReader>(R reader, NineParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, NineParams params) { return PbrSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Nine", Module(c_Body)), "Nine"),
			ApiError,
			Catch::Matchers::MessageMatches(
				ContainsSubstring("slot 'ninth' is past the 8 a record carries")));
	}

	SECTION("a parameter the engine cannot pack")
	{
		constexpr std::string_view c_Body = R"(struct IntParams
{
    uint level;
};

struct IntSurface : ISurfaceSource
{
    typealias Params = IntParams;
    static float Coverage<R : IMaterialReader>(R reader, IntParams params) { return 1.0; }
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, IntParams params) { return PbrSurface(); }
};
)";
		CHECK_THROWS_MATCHES(
			ReflectSurface(session.Load("Ints", Module(c_Body)), "Ints"),
			ApiError,
			Catch::Matchers::MessageMatches(
				ContainsSubstring("parameter 'level' is not a float or a float vector")));
	}
}
