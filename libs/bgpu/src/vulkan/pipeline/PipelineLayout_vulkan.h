#pragma once
#include "volk_vulkan.h"
#include <bgpu/uniforms/UniformLayoutEntry.h>
#include <core/str/str.h>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace bgpu
{
	class IShader;
	class ShaderCache;

	namespace pipeline_util
	{
		/**
		 * A pipeline's layout and code: its constant buffers as uniform-buffer descriptors in set 0,
		 * the bindless table's layout in set 1, and SPIR-V per entry point. Owns the two Vulkan
		 * layouts and destroys them with itself.
		 */
		class PipelineLayout final
		{
		public:
			PipelineLayout() = default;
			~PipelineLayout() noexcept;

			PipelineLayout(const PipelineLayout&) = delete;
			PipelineLayout(PipelineLayout&& other) noexcept;
			PipelineLayout&
			operator=(const PipelineLayout&) = delete;
			PipelineLayout&
			operator=(PipelineLayout&& other) noexcept;

			VkDevice              device          = VK_NULL_HANDLE;
			VkDescriptorSetLayout constantsLayout = VK_NULL_HANDLE;
			VkDescriptorSetLayout bindlessLayout  = VK_NULL_HANDLE;
			VkPipelineLayout      layout          = VK_NULL_HANDLE;

			UniformLayoutMap uniformLayoutEntries;

			// The binding of each constant buffer in set 0, by its rootParamIndex.
			std::vector<uint32_t> cbufferBindings;

			core::str::unordered_str_map<std::vector<std::byte>> entryPointCode;

		private:
			void
			Destroy() noexcept;
		};

		/**
		 * Compiles the shaders as one linked program, or loads it from `cache`, and makes the
		 * layout every one of them binds through. Takes no Slang session: on a cache hit nothing
		 * here touches Slang.
		 *
		 * @param stages the stages the constant buffers are visible to.
		 */
		[[nodiscard]] PipelineLayout
		BuildPipelineLayout(
			VkDevice                        device,
			const ShaderCache*              cache,
			std::initializer_list<IShader*> shaders,
			VkShaderStageFlags              stages);
	}
}
