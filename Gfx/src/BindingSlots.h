#pragma once

namespace gfx
{
#ifdef RENDERER_DX11
	constexpr uint32_t BINDING_SPACE_STRIDE = 8;

	constexpr uint32_t
	makeBindingID(uint16_t space, uint16_t slot) noexcept
	{
		return space * BINDING_SPACE_STRIDE + slot;
	}

	constexpr uint32_t
	getBindingSpace(uint32_t bindingID) noexcept
	{
		return bindingID / BINDING_SPACE_STRIDE;
	}

	constexpr uint32_t
	getBindingSlot(uint32_t bindingID) noexcept
	{
		return bindingID % BINDING_SPACE_STRIDE;
	}

#else
	constexpr uint32_t
	makeBindingID(uint16_t space, uint16_t slot) noexcept
	{
		return space << 16 | slot;
	}

	constexpr uint32_t
	getBindingSpace(uint32_t bindingID) noexcept
	{
		return bindingID >> 16;
	}

	constexpr uint32_t
	getBindingSlot(uint32_t bindingID) noexcept
	{
		return bindingID & 0xffff;
	}

#endif

	namespace BindingSlots
	{
		constexpr uint32_t PerFrameSpace    = 0;
		constexpr uint32_t PerObjectSpace   = 1;
		constexpr uint32_t PerMaterialSpace = 2;
	}

	namespace BINDINGS
	{
		// Vertex Shader Bindings
		constexpr auto CAMERA_VCB = makeBindingID(BindingSlots::PerFrameSpace, 0);

		constexpr auto OBJECT_TRANSFORM_VCB = makeBindingID(BindingSlots::PerObjectSpace, 0);

		// Pixel Shader Bindings
		constexpr auto LIGHTING_PCB = makeBindingID(BindingSlots::PerFrameSpace, 0);

		constexpr auto DIFFUSE_TEXTURE  = makeBindingID(BindingSlots::PerMaterialSpace, 0);
		constexpr auto NORMAL_TEXTURE   = makeBindingID(BindingSlots::PerMaterialSpace, 1);
		constexpr auto SPECULAR_TEXTURE = makeBindingID(BindingSlots::PerMaterialSpace, 2);
	}

	constexpr uint32_t TEXTURE_COUNT = 3;
}
