#include <algorithm>
#include <bgpu/SystemRequirements.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <stdexcept>
#include <string>
#include <vector>

// Both backends' checks are platform-neutral, so the D3D12 one is pinned on a Mac too. What no case
// here can prove is that each backend reads its facts off the device correctly: that is
// CreateGpuContext's, and a supported machine only ever shows the passing half of it.

namespace
{
	bgpu::AppleSystemFacts
	M1OnSonoma()
	{
		auto facts                 = bgpu::AppleSystemFacts();
		facts.appleSilicon         = true;
		facts.processor            = "Apple M1";
		facts.osMajor              = 14;
		facts.osMinor              = 4;
		facts.device               = true;
		facts.gpuName              = "Apple M1";
		facts.metal3               = true;
		facts.meshShaders          = true;
		facts.argumentBuffersTier2 = true;
		return facts;
	}

	bgpu::D3d12SystemFacts
	Rtx2060()
	{
		auto facts                = bgpu::D3d12SystemFacts();
		facts.device              = true;
		facts.gpuName             = "NVIDIA GeForce RTX 2060";
		facts.shaderModel         = 0x66;
		facts.meshShaderTier      = 10;
		facts.resourceBindingTier = 3;
		facts.enhancedBarriers    = true;
		return facts;
	}

	std::vector<bgpu::Requirement>
	Requirements(const std::vector<bgpu::UnmetRequirement>& unmet)
	{
		auto requirements = std::vector<bgpu::Requirement>();
		for (const bgpu::UnmetRequirement& each : unmet) requirements.push_back(each.requirement);
		return requirements;
	}
}

TEST_CASE("An Apple silicon Mac on macOS 13 or later meets every requirement", "[sysreq]")
{
	CHECK(bgpu::CheckSystemRequirements(M1OnSonoma()).empty());

	auto ventura    = M1OnSonoma();
	ventura.osMajor = 13;
	ventura.osMinor = 0;
	CHECK(bgpu::CheckSystemRequirements(ventura).empty());
}

TEST_CASE("An Intel Mac is refused even with a Metal 3 GPU", "[sysreq]")
{
	auto intel         = M1OnSonoma();
	intel.appleSilicon = false;
	intel.processor    = "Intel(R) Core(TM) i9-9980HK CPU @ 2.40GHz";
	intel.gpuName      = "AMD Radeon Pro 5500M";

	const std::vector<bgpu::UnmetRequirement> unmet = bgpu::CheckSystemRequirements(intel);
	REQUIRE(unmet.size() == 1);
	CHECK(unmet[0].requirement == bgpu::Requirement::kAppleSilicon);
	CHECK(unmet[0].found == intel.processor);
}

TEST_CASE("A Mac misses every requirement it falls short of, not only the first", "[sysreq]")
{
	auto old      = bgpu::AppleSystemFacts();
	old.processor = "Intel(R) Core(TM) i5-5257U CPU @ 2.70GHz";
	old.osMajor   = 12;
	old.osMinor   = 6;
	old.osPatch   = 1;
	old.device    = true;
	old.gpuName   = "Intel Iris Graphics 6100";

	const std::vector<bgpu::UnmetRequirement> unmet = bgpu::CheckSystemRequirements(old);
	CHECK(
		Requirements(unmet) == std::vector{ bgpu::Requirement::kAppleSilicon,
	                                        bgpu::Requirement::kMacOs13,
	                                        bgpu::Requirement::kMetal3,
	                                        bgpu::Requirement::kMetalMeshShaders,
	                                        bgpu::Requirement::kMetalArgumentBuffersTier2 });
	CHECK(unmet[1].found == "macOS 12.6.1");
}

TEST_CASE("A Mac with no Metal device is not told to replace its GPU", "[sysreq]")
{
	// A supported Mac whose device could not be created has a fault, which CreateGpuContext reports
	// as a runtime_error; only what is known without a device can be unmet.
	auto noDevice   = M1OnSonoma();
	noDevice.device = false;
	noDevice.metal3 = false;
	CHECK(bgpu::CheckSystemRequirements(noDevice).empty());

	noDevice.appleSilicon = false;
	CHECK(
		Requirements(bgpu::CheckSystemRequirements(noDevice)) ==
		std::vector{ bgpu::Requirement::kAppleSilicon });
}

TEST_CASE("A D3D12 GPU with mesh shaders and bindless meets every requirement", "[sysreq]")
{
	CHECK(bgpu::CheckSystemRequirements(Rtx2060()).empty());
}

TEST_CASE("A D3D12 machine with no 12_0 device misses only that", "[sysreq]")
{
	auto none    = bgpu::D3d12SystemFacts();
	none.gpuName = "Microsoft Basic Render Driver";

	const std::vector<bgpu::UnmetRequirement> unmet = bgpu::CheckSystemRequirements(none);
	REQUIRE(unmet.size() == 1);
	CHECK(unmet[0].requirement == bgpu::Requirement::kD3d12Device);
	CHECK(unmet[0].found == none.gpuName);
}

TEST_CASE("A D3D12 GPU without mesh shaders or an old driver is refused", "[sysreq]")
{
	auto rdna1           = Rtx2060();
	rdna1.gpuName        = "AMD Radeon RX 5700 XT";
	rdna1.meshShaderTier = 0;
	CHECK(
		Requirements(bgpu::CheckSystemRequirements(rdna1)) ==
		std::vector{ bgpu::Requirement::kD3d12MeshShaderTier1 });

	auto oldDriver             = Rtx2060();
	oldDriver.shaderModel      = 0x65;
	oldDriver.enhancedBarriers = false;
	CHECK(
		Requirements(bgpu::CheckSystemRequirements(oldDriver)) ==
		std::vector{ bgpu::Requirement::kD3d12ShaderModel66,
	                 bgpu::Requirement::kD3d12EnhancedBarriers });

	auto tier2                = Rtx2060();
	tier2.resourceBindingTier = 2;
	CHECK(
		Requirements(bgpu::CheckSystemRequirements(tier2)) ==
		std::vector{ bgpu::Requirement::kD3d12ResourceBindingTier3 });
}

TEST_CASE(
	"The message names each thing to replace or update once, with what the machine has",
	"[sysreq]")
{
	using Catch::Matchers::ContainsSubstring;

	const auto unmet = std::vector<bgpu::UnmetRequirement>{
		{ bgpu::Requirement::kD3d12MeshShaderTier1, "AMD Radeon RX 5700 XT" },
		{ bgpu::Requirement::kD3d12ResourceBindingTier3, "AMD Radeon RX 5700 XT" },
		{ bgpu::Requirement::kD3d12ShaderModel66, "AMD Radeon RX 5700 XT" },
	};
	const std::string message = bgpu::DescribeUnmetRequirements(unmet);

	CHECK_THAT(message, ContainsSubstring("does not meet the minimum system requirements"));
	CHECK_THAT(message, ContainsSubstring("graphics card with mesh shaders"));
	CHECK_THAT(message, ContainsSubstring("graphics driver"));
	CHECK_THAT(message, ContainsSubstring("This computer has: AMD Radeon RX 5700 XT."));

	// The two GPU requirements read as one line, the driver as another.
	CHECK(
		message.find("graphics card with mesh shaders") ==
		message.rfind("graphics card with mesh shaders"));
	CHECK(std::ranges::count(message, '\n') == 3);
}

TEST_CASE("UnsupportedSystem carries what was unmet and says it in what()", "[sysreq]")
{
	const auto unmet = std::vector<bgpu::UnmetRequirement>{
		{ bgpu::Requirement::kAppleSilicon, "Intel(R) Core(TM) i7" },
		{ bgpu::Requirement::kMacOs13, "macOS 12.7.0" },
	};

	try
	{
		throw bgpu::UnsupportedSystem(unmet);
	}
	catch (const std::runtime_error& error)
	{
		const auto* unsupported = dynamic_cast<const bgpu::UnsupportedSystem*>(&error);
		REQUIRE(unsupported != nullptr);
		CHECK(std::vector(unsupported->Unmet().begin(), unsupported->Unmet().end()) == unmet);
		CHECK(std::string(error.what()) == bgpu::DescribeUnmetRequirements(unmet));

		const bgpu::UnsupportedSystem copy = *unsupported;
		CHECK(copy.Unmet().size() == 2);
	}
}
