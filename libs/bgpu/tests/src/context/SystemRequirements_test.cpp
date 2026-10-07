#include <algorithm>
#include <bgpu/SystemRequirements.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Every backend's checks are platform-neutral, so the D3D12 one is pinned on a Mac too. What no case
// here can prove is that each backend reads its facts off the device correctly: that is
// CreateGpuContext's, and a supported machine only ever shows the passing half of it.

namespace
{
	// VK_MAKE_API_VERSION(0, 1, 3, 0) and (0, 1, 2, 0), spelled out: the suite includes no Vulkan header.
	constexpr uint32_t c_Vulkan13 = (1U << 22U) | (3U << 12U);
	constexpr uint32_t c_Vulkan12 = (1U << 22U) | (2U << 12U);

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

	bgpu::VulkanSystemFacts
	Rtx2060OnVulkan()
	{
		auto facts               = bgpu::VulkanSystemFacts();
		facts.device             = true;
		facts.gpuName            = "NVIDIA GeForce RTX 2060";
		facts.apiVersion         = c_Vulkan13 | 280U;
		facts.meshShaders        = true;
		facts.descriptorIndexing = true;
		facts.scalarBlockLayout  = true;
		facts.synchronization    = true;
		facts.mutableDescriptors = true;
		facts.graphics           = true;
		facts.presentation       = true;
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
	"A Vulkan 1.3 GPU with mesh shaders and descriptor indexing meets every requirement",
	"[sysreq]")
{
	CHECK(bgpu::CheckSystemRequirements(Rtx2060OnVulkan()).empty());

	auto exactly13       = Rtx2060OnVulkan();
	exactly13.apiVersion = c_Vulkan13;
	CHECK(bgpu::CheckSystemRequirements(exactly13).empty());
}

TEST_CASE("A machine with no Vulkan device misses only that", "[sysreq]")
{
	const std::vector<bgpu::UnmetRequirement> unmet =
		bgpu::CheckSystemRequirements(bgpu::VulkanSystemFacts());
	REQUIRE(unmet.size() == 1);
	CHECK(unmet[0].requirement == bgpu::Requirement::kVulkanDevice);
	CHECK(unmet[0].found.empty());
}

// A driver below 1.3 cannot report the features the bar names, so nothing but its version is held
// against the machine: the GPU may be fine, and the driver is the thing a player can change.
TEST_CASE("A Vulkan driver below 1.3 is told to update, and to do nothing else", "[sysreq]")
{
	auto old       = bgpu::VulkanSystemFacts();
	old.device     = true;
	old.gpuName    = "NVIDIA GeForce RTX 2060";
	old.apiVersion = c_Vulkan12 | 198U;

	const std::vector<bgpu::UnmetRequirement> unmet = bgpu::CheckSystemRequirements(old);
	REQUIRE(unmet.size() == 1);
	CHECK(unmet[0].requirement == bgpu::Requirement::kVulkan13);
	CHECK(unmet[0].found == "NVIDIA GeForce RTX 2060 with Vulkan 1.2.198");
}

TEST_CASE("A Vulkan GPU without mesh shaders or bindless is refused", "[sysreq]")
{
	auto rdna1        = Rtx2060OnVulkan();
	rdna1.gpuName     = "AMD Radeon RX 5700 XT";
	rdna1.meshShaders = false;
	CHECK(
		Requirements(bgpu::CheckSystemRequirements(rdna1)) ==
		std::vector{ bgpu::Requirement::kVulkanMeshShaders });

	auto everything               = Rtx2060OnVulkan();
	everything.meshShaders        = false;
	everything.descriptorIndexing = false;
	everything.scalarBlockLayout  = false;
	everything.synchronization    = false;
	everything.mutableDescriptors = false;
	everything.graphics           = false;
	CHECK(
		Requirements(bgpu::CheckSystemRequirements(everything)) ==
		std::vector{ bgpu::Requirement::kVulkanMeshShaders,
	                 bgpu::Requirement::kVulkanDescriptorIndexing,
	                 bgpu::Requirement::kVulkanScalarBlockLayout,
	                 bgpu::Requirement::kVulkanSynchronization,
	                 bgpu::Requirement::kVulkanMutableDescriptors,
	                 bgpu::Requirement::kVulkanGraphics });
}

// Every mesh-shading GPU has had the extension for years, so a driver without it is the thing to
// change; the graphics features are the GPU's own, and one without them is refused as the mesh stage
// is.
TEST_CASE(
	"A Vulkan driver without mutable descriptors is told to update; a GPU without the graphics "
	"features is refused",
	"[sysreq]")
{
	using Catch::Matchers::ContainsSubstring;

	auto oldDriver                                  = Rtx2060OnVulkan();
	oldDriver.mutableDescriptors                    = false;
	const std::vector<bgpu::UnmetRequirement> stale = bgpu::CheckSystemRequirements(oldDriver);
	CHECK(Requirements(stale) == std::vector{ bgpu::Requirement::kVulkanMutableDescriptors });
	CHECK_THAT(bgpu::DescribeUnmetRequirements(stale), ContainsSubstring("graphics driver"));
	CHECK_THAT(
		bgpu::DescribeUnmetRequirements(stale),
		!ContainsSubstring("graphics card with mesh shaders"));

	auto noGraphics                                   = Rtx2060OnVulkan();
	noGraphics.graphics                               = false;
	const std::vector<bgpu::UnmetRequirement> lacking = bgpu::CheckSystemRequirements(noGraphics);
	CHECK(Requirements(lacking) == std::vector{ bgpu::Requirement::kVulkanGraphics });
	CHECK_THAT(
		bgpu::DescribeUnmetRequirements(lacking),
		ContainsSubstring("graphics card with mesh shaders"));

	auto noPresentation         = Rtx2060OnVulkan();
	noPresentation.presentation = false;
	CHECK(
		Requirements(bgpu::CheckSystemRequirements(noPresentation)) ==
		std::vector{ bgpu::Requirement::kVulkanGraphics });
}

// A 1.3 driver that hides a feature 1.3 makes mandatory is out of date or broken: the player is told
// to update the driver, not to replace the card.
TEST_CASE(
	"A Vulkan driver without timeline semaphores or synchronization2 is told to update",
	"[sysreq]")
{
	auto broken            = Rtx2060OnVulkan();
	broken.synchronization = false;

	const std::vector<bgpu::UnmetRequirement> unmet = bgpu::CheckSystemRequirements(broken);
	CHECK(Requirements(unmet) == std::vector{ bgpu::Requirement::kVulkanSynchronization });
	CHECK_THAT(
		bgpu::DescribeUnmetRequirements(unmet),
		Catch::Matchers::ContainsSubstring("graphics driver"));
	CHECK_THAT(
		bgpu::DescribeUnmetRequirements(unmet),
		!Catch::Matchers::ContainsSubstring("graphics card with mesh shaders"));
}

TEST_CASE("A Vulkan machine's message is the one a D3D12 machine's is", "[sysreq]")
{
	using Catch::Matchers::ContainsSubstring;

	auto lacking               = Rtx2060OnVulkan();
	lacking.gpuName            = "Intel(R) UHD Graphics 630";
	lacking.meshShaders        = false;
	lacking.descriptorIndexing = false;
	lacking.scalarBlockLayout  = false;

	const std::string message =
		bgpu::DescribeUnmetRequirements(bgpu::CheckSystemRequirements(lacking));

	CHECK_THAT(message, ContainsSubstring("graphics card with mesh shaders"));
	CHECK_THAT(message, ContainsSubstring("graphics driver"));
	CHECK_THAT(message, ContainsSubstring("This computer has: Intel(R) UHD Graphics 630."));

	// The mesh stage and bindless read as one line, the layout the driver lacks as another.
	CHECK(std::ranges::count(message, '\n') == 3);
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
