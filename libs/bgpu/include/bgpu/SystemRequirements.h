#pragma once
#include <bgpu/api.h>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bgpu
{
	/**
	 * One thing the engine needs of the machine, below which it cannot draw: bindless resource
	 * access and a mesh stage, and what each backend needs to offer them (docs/bgpu.md, "Minimum
	 * system requirements"). CreateGpuContext checks every one that applies to its backend.
	 */
	enum class Requirement : uint8_t
	{
		kAppleSilicon,
		kMacOs13,
		kMetal3,
		kMetalMeshShaders,
		kMetalArgumentBuffersTier2,

		kD3d12Device,
		kD3d12MeshShaderTier1,
		kD3d12ResourceBindingTier3,
		kD3d12ShaderModel66,
		kD3d12EnhancedBarriers,

		kVulkanDevice,
		kVulkan13,
		kVulkanMeshShaders,
		kVulkanDescriptorIndexing,
		kVulkanScalarBlockLayout,
		kVulkanSynchronization,
		kVulkanMutableDescriptors,
		kVulkanGraphics,
	};

	struct UnmetRequirement
	{
		Requirement requirement = Requirement::kAppleSilicon;

		// What the machine has instead, as a player would recognise it -- the processor, the OS
		// version, the GPU's name. Empty when there is nothing to name.
		std::string found;

		bool
		operator==(const UnmetRequirement&) const = default;
	};

	/** What the Metal context reads of the machine, before it builds anything on the device. */
	struct AppleSystemFacts
	{
		bool        appleSilicon = false;
		std::string processor;

		uint32_t osMajor = 0;
		uint32_t osMinor = 0;
		uint32_t osPatch = 0;

		// A Metal device exists. Without one the GPU's requirements are unknown rather than unmet: the
		// machine may be fine and the fault elsewhere, so they are not checked.
		bool        device = false;
		std::string gpuName;
		bool        metal3 = false;

		// MTLGPUFamilyApple7, the first family with a mesh stage on Apple silicon.
		bool meshShaders          = false;
		bool argumentBuffersTier2 = false;
	};

	/**
	 * What the D3D12 context reads of the machine. The tiers and the shader model are the D3D12
	 * enumerators' values, so the checks compile on every backend.
	 */
	struct D3d12SystemFacts
	{
		// A device was created at feature level 12_0. Without one nothing else could be read.
		bool        device = false;
		std::string gpuName;

		uint32_t shaderModel         = 0;  // D3D_SHADER_MODEL: 0x66 is 6.6
		uint32_t meshShaderTier      = 0;  // D3D12_MESH_SHADER_TIER: 10 is tier 1
		uint32_t resourceBindingTier = 0;  // D3D12_RESOURCE_BINDING_TIER: 3 is tier 3
		bool     enhancedBarriers    = false;
	};

	/**
	 * What the Vulkan context reads of the machine's first physical device. The version is Vulkan's
	 * packed one, so the check compiles on every backend.
	 */
	struct VulkanSystemFacts
	{
		// A physical device was enumerated. Without one nothing else could be read.
		bool        device = false;
		std::string gpuName;

		uint32_t apiVersion = 0;  // VkPhysicalDeviceProperties::apiVersion: 0x00403000 is 1.3

		// Read only from a 1.3 device. Below it they are unknown rather than unmet, so they are not
		// checked: an old driver hides what the GPU can do.

		// VK_EXT_mesh_shader with its meshShader and taskShader features: the engine's mesh programs
		// have an amplification stage, which Vulkan calls the task stage.
		bool meshShaders = false;

		// Bindless: descriptor arrays of runtime size, partially bound, updated after bind and
		// indexed non-uniformly, for sampled images, storage images and storage buffers.
		bool descriptorIndexing = false;

		// What a ScalarDataLayout buffer compiles to in SPIR-V.
		bool scalarBlockLayout = false;

		// Timeline semaphores and synchronization2: the RHI's fences and barriers, D3D12's fence
		// values and enhanced barriers. A conformant 1.3 driver has both.
		bool synchronization = false;

		// VK_EXT_mutable_descriptor_type: one bindless array holding buffers and textures alike, at
		// the one binding Slang lowers both handles to, as D3D12's one CBV/SRV/UAV heap holds them.
		bool mutableDescriptors = false;

		// What the graphics RHI draws with and its states can ask for: dynamic rendering, a count
		// buffer for indirect dispatches, and the core features behind D3D12's blend, raster and
		// sampler states (independent and dual-source blend, wireframe, depth clamp, several
		// viewports, anisotropic, min/max and mirror-once sampling, BC textures), and buffer writes
		// from a pixel shader.
		bool graphics = false;

		// VK_KHR_swapchain: a renderer presents to a window. Checked with `graphics`, as part of
		// Requirement::kVulkanGraphics.
		bool presentation = false;
	};

	/** Every requirement `facts` fall short of, in the enumeration's order. Empty when they meet all. */
	[[nodiscard]] BGPU_API std::vector<UnmetRequirement>
						   CheckSystemRequirements(const AppleSystemFacts& facts);

	[[nodiscard]] BGPU_API std::vector<UnmetRequirement>
						   CheckSystemRequirements(const D3d12SystemFacts& facts);

	[[nodiscard]] BGPU_API std::vector<UnmetRequirement>
						   CheckSystemRequirements(const VulkanSystemFacts& facts);

	/**
	 * A message a player can act on: that this computer cannot run the game, and every requirement
	 * it misses with what it has instead. English; a client that wants its own words reads
	 * UnsupportedSystem::Unmet and writes them.
	 */
	[[nodiscard]] BGPU_API std::string
						   DescribeUnmetRequirements(std::span<const UnmetRequirement> unmet);

	/**
	 * Thrown by CreateGpuContext when the machine is below the engine's bar, rather than letting the
	 * first mesh pipeline fail on it. Distinct from every other failure there, which is a fault
	 * rather than a machine the engine does not support. `what()` is DescribeUnmetRequirements.
	 *
	 * Defined in the header, so a catch in any module names one type without bgpu exporting a class
	 * that holds a std::vector.
	 */
	class UnsupportedSystem : public std::runtime_error
	{
	public:
		explicit UnsupportedSystem(std::vector<UnmetRequirement> unmet) :
			std::runtime_error(DescribeUnmetRequirements(unmet)),
			m_Unmet(std::make_shared<const std::vector<UnmetRequirement>>(std::move(unmet)))
		{}

		[[nodiscard]] std::span<const UnmetRequirement>
		Unmet() const noexcept
		{
			return *m_Unmet;
		}

	private:
		// Shared, so copying the exception cannot throw.
		std::shared_ptr<const std::vector<UnmetRequirement>> m_Unmet;
	};
}
