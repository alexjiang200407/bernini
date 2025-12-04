#pragma once

namespace gfx
{
	class GfxBase
	{
	public:
		virtual ~GfxBase() = default;
	};

	bool
	isGfxInitialized() noexcept;
}
