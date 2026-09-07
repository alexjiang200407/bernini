#pragma once
#include <bgl/SurfaceType.h>

#include <slang.h>
#include <string_view>

namespace bgl
{
	/**
	 * Reads the one surface out of a game's compiled module: the struct conforming to
	 * `ISurfaceSource`, its `Params` fields at the offsets a record holds them at, the slot fields
	 * among them by their declared type, and the defaults on the rest.
	 *
	 * The layout is the one this session's target reconstructs, and the backends disagree: MSL
	 * aligns a float3 to 16 where the scalar rules leave it at 4. So a surface is reflected once
	 * per device rather than once per build, and what a record holds is whatever
	 * `RawBuffer.Load<Params>` reads back on the backend that will draw it.
	 *
	 * `SurfaceType::slot` is left at zero; registration assigns it.
	 *
	 * @param module A module already loaded into the session that will compile the surface.
	 * @param name What a material names to reach this surface, normally the module file's stem.
	 * @param targetIndex Which of the session's targets the layout is read for.
	 * @return The reflected surface.
	 * @throws ApiError if the module does not import the contract, holds no conforming struct or
	 *         more than one, declares more slots than a record carries, declares a parameter of a
	 *         type the engine cannot pack, or does not reflect at all.
	 */
	SurfaceType
	ReflectSurface(slang::IModule* module, std::string_view name, SlangInt targetIndex = 0);
}
