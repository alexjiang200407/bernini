#pragma once
#include "uniforms/UniformLayoutEntry.h"

namespace bgl
{
	/**
	 * The WGSL binding a resource leaf was assigned. WGSL has no bindless, so every plainly-bound
	 * buffer becomes an explicit (group, binding) slot; readWrite picks storage vs. read-only storage.
	 */
	struct BindGroupSlot
	{
		uint32_t group;
		uint32_t binding;
		bool     readWrite;
	};

	/**
	 * Composes `module` with each named entry point, links the program, and keeps it alive on
	 * `owner` so the returned layout pointer stays valid. Whole-program WGSL is read from `owner`
	 * afterwards (getTargetCode for multi-stage, getEntryPointCode for a lone entry).
	 *
	 * @return the linked program layout, owned by `owner`.
	 * @throws GraphicsError if a named entry point is not found in the module.
	 */
	slang::ProgramLayout*
	LinkWgslProgram(
		slang::ISession*                      session,
		slang::IModule*                       module,
		std::span<const std::string>          entryPointNames,
		Slang::ComPtr<slang::IComponentType>& owner);

	/**
	 * Walks the program's constant buffers into `entries` -- each resource leaf becomes an 8-byte
	 * kDescriptorHandle at a struct-relative offset -- and appends every leaf's (group, binding) to
	 * `slots`. Offsets are struct-relative because Uniforms::Traverse and the dispatch/draw path both
	 * accumulate down the tree.
	 */
	void
	ReflectWgslBindings(
		slang::ProgramLayout*       layout,
		UniformLayoutMap&           entries,
		std::vector<BindGroupSlot>& slots);

	/**
	 * Builds a bind group layout binding every slot at `visibility`. Only bind group 0 is supported.
	 */
	wgpu::BindGroupLayout
	MakeWgslBindGroupLayout(
		const wgpu::Device&            device,
		std::span<const BindGroupSlot> slots,
		wgpu::ShaderStage              visibility);
}
