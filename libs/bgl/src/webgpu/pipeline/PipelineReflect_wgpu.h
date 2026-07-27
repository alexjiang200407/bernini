#pragma once
#include "uniforms/UniformLayoutEntry.h"

namespace bgl
{
	/**
	 * A WGSL binding the reflection assigned. WGSL has no bindless, so every plainly-bound buffer
	 * becomes an explicit (group, binding) slot, and a constant buffer's plain-data members are
	 * gathered into one further Uniform slot at the constant buffer's own binding.
	 */
	struct BindGroupSlot
	{
		uint32_t                group;
		uint32_t                binding;
		wgpu::BufferBindingType type;
	};

	/** One entry point to link: its module and its name. Two entries may share a module. */
	struct WgslEntryPoint
	{
		slang::IModule* module;
		std::string     name;
	};

	/**
	 * Composes the entry points -- each found in its own module, so a vertex and pixel stage may come
	 * from different modules -- links the program, and keeps it alive on `owner` so the returned
	 * layout pointer stays valid. Whole-program WGSL is read from `owner` afterwards (getTargetCode
	 * for multi-stage, getEntryPointCode for a lone entry).
	 *
	 * @return the linked program layout, owned by `owner`.
	 * @throws GraphicsError if an entry point is not found in its module.
	 */
	slang::ProgramLayout*
	LinkWgslProgram(
		slang::ISession*                      session,
		std::span<const WgslEntryPoint>       entryPoints,
		Slang::ComPtr<slang::IComponentType>& owner);

	/**
	 * Walks the program's constant buffers into `entries` and appends every binding to `slots`.
	 *
	 * A constant buffer's Uniforms bytes are laid out in two regions. Plain-data leaves keep the
	 * std140 offsets Slang assigned them, so `[0, uniformBlockSize)` is a byte-exact image of the
	 * WGSL uniform buffer and uploads with one memcpy. Resource leaves become 8-byte
	 * kDescriptorHandles packed after that block, since only their slot index is ever read back.
	 *
	 * Offsets are struct-relative because Uniforms::Traverse and the dispatch/draw path both
	 * accumulate down the tree, so a field owning resources is placed where the handle allocation
	 * has reached and its subtree hands out offsets relative to that. Nesting is therefore fine --
	 * the buffer wrappers do keep their handle a level down -- but a struct below the top level may
	 * not mix plain data with resources, having a single offset to place both regions at.
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
