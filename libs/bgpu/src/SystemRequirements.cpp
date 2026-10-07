#include <algorithm>
#include <bgpu/SystemRequirements.h>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bgpu
{
	namespace
	{
		constexpr uint32_t c_ShaderModel66        = 0x66;
		constexpr uint32_t c_MeshShaderTier1      = 10;
		constexpr uint32_t c_ResourceBindingTier3 = 3;
		constexpr uint32_t c_MinimumMacOsMajor    = 13;
		constexpr uint32_t c_Vulkan13             = (1U << 22U) | (3U << 12U);
		constexpr auto     c_NeedsAMetal3Gpu =
			std::string_view("A graphics processor that supports Metal 3");
		constexpr auto c_NeedsAMeshShadingGpu = std::string_view(
			"A graphics card with mesh shaders: NVIDIA GeForce GTX 1660, RTX 2060 or newer, AMD "
			"Radeon RX 6000 series or newer, or Intel Arc");
		constexpr auto c_NeedsACurrentDriver = std::string_view(
			"An up-to-date graphics driver -- install the latest one from your graphics card's "
			"maker");

		// Requirements met by the same part of the machine share a sentence, so a player reads each
		// thing to replace or update once.
		[[nodiscard]] std::string_view
		Need(const Requirement requirement) noexcept
		{
			switch (requirement)
			{
			case Requirement::kAppleSilicon:
				return "A Mac with Apple silicon (M1 or later)";
			case Requirement::kMacOs13:
				return "macOS 13 Ventura or later";
			case Requirement::kMetal3:
			case Requirement::kMetalMeshShaders:
			case Requirement::kMetalArgumentBuffersTier2:
				return c_NeedsAMetal3Gpu;
			case Requirement::kD3d12Device:
			case Requirement::kD3d12MeshShaderTier1:
			case Requirement::kD3d12ResourceBindingTier3:
			case Requirement::kVulkanDevice:
			case Requirement::kVulkanMeshShaders:
			case Requirement::kVulkanDescriptorIndexing:
				return c_NeedsAMeshShadingGpu;
			case Requirement::kD3d12ShaderModel66:
			case Requirement::kD3d12EnhancedBarriers:
			case Requirement::kVulkan13:
			case Requirement::kVulkanScalarBlockLayout:
				return c_NeedsACurrentDriver;
			}
			return "";
		}

		// Vulkan packs a version as variant.major.minor.patch in 3, 7, 10 and 12 bits.
		[[nodiscard]] std::string
		VulkanVersion(const uint32_t packed)
		{
			return std::format(
				"Vulkan {}.{}.{}",
				(packed >> 22U) & 0x7FU,
				(packed >> 12U) & 0x3FFU,
				packed & 0xFFFU);
		}
	}

	std::vector<UnmetRequirement>
	CheckSystemRequirements(const AppleSystemFacts& facts)
	{
		auto unmet = std::vector<UnmetRequirement>();
		if (!facts.appleSilicon)
			unmet.push_back({ Requirement::kAppleSilicon, facts.processor });
		if (facts.osMajor < c_MinimumMacOsMajor)
		{
			unmet.push_back(
				{ Requirement::kMacOs13,
			      std::format("macOS {}.{}.{}", facts.osMajor, facts.osMinor, facts.osPatch) });
		}
		if (!facts.device)
			return unmet;
		if (!facts.metal3)
			unmet.push_back({ Requirement::kMetal3, facts.gpuName });
		if (!facts.meshShaders)
			unmet.push_back({ Requirement::kMetalMeshShaders, facts.gpuName });
		if (!facts.argumentBuffersTier2)
			unmet.push_back({ Requirement::kMetalArgumentBuffersTier2, facts.gpuName });
		return unmet;
	}

	std::vector<UnmetRequirement>
	CheckSystemRequirements(const D3d12SystemFacts& facts)
	{
		if (!facts.device)
			return { { Requirement::kD3d12Device, facts.gpuName } };

		auto unmet = std::vector<UnmetRequirement>();
		if (facts.meshShaderTier < c_MeshShaderTier1)
			unmet.push_back({ Requirement::kD3d12MeshShaderTier1, facts.gpuName });
		if (facts.resourceBindingTier < c_ResourceBindingTier3)
			unmet.push_back({ Requirement::kD3d12ResourceBindingTier3, facts.gpuName });
		if (facts.shaderModel < c_ShaderModel66)
			unmet.push_back({ Requirement::kD3d12ShaderModel66, facts.gpuName });
		if (!facts.enhancedBarriers)
			unmet.push_back({ Requirement::kD3d12EnhancedBarriers, facts.gpuName });
		return unmet;
	}

	std::vector<UnmetRequirement>
	CheckSystemRequirements(const VulkanSystemFacts& facts)
	{
		if (!facts.device)
			return { { Requirement::kVulkanDevice, facts.gpuName } };

		if (facts.apiVersion < c_Vulkan13)
		{
			return {
				{ Requirement::kVulkan13,
				  std::format("{} with {}", facts.gpuName, VulkanVersion(facts.apiVersion)) }
			};
		}

		auto unmet = std::vector<UnmetRequirement>();
		if (!facts.meshShaders)
			unmet.push_back({ Requirement::kVulkanMeshShaders, facts.gpuName });
		if (!facts.descriptorIndexing)
			unmet.push_back({ Requirement::kVulkanDescriptorIndexing, facts.gpuName });
		if (!facts.scalarBlockLayout)
			unmet.push_back({ Requirement::kVulkanScalarBlockLayout, facts.gpuName });
		return unmet;
	}

	std::string
	DescribeUnmetRequirements(const std::span<const UnmetRequirement> unmet)
	{
		auto message =
			std::string("This computer does not meet the minimum system requirements.\n");

		auto said = std::vector<std::string_view>();
		for (const UnmetRequirement& each : unmet)
		{
			const std::string_view need = Need(each.requirement);
			if (std::ranges::find(said, need) != said.end())
				continue;
			said.push_back(need);

			message += std::format("\n- {}.", need);
			if (!each.found.empty())
				message += std::format(" This computer has: {}.", each.found);
		}
		return message;
	}
}
