#pragma once
#include <bgl/SurfaceType.h>

#include <optional>
#include <slang.h>
#include <string>
#include <string_view>

namespace bgl
{
	/**
	 * What the engine learned about a game's surface. `type` is what a client sees; `sourceTypeName`
	 * is the struct inside the module that conforms to `ISurfaceSource`, which only the binding
	 * module needs -- it is the name that module writes its `typealias` to.
	 */
	struct ReflectedSurface
	{
		SurfaceType type;
		std::string sourceTypeName;
	};

	/**
	 * Reads the one surface out of a game's compiled module: the struct conforming to
	 * `ISurfaceSource`, its `MaterialParams` fields at the offsets a record holds them at, the texture
	 * fields among them by their declared type, and the defaults on the values that are left.
	 *
	 * The layout is the one this session's target reconstructs, and the backends disagree: MSL
	 * aligns a float3 to 16 where the scalar rules leave it at 4. So a surface is reflected once
	 * per device rather than once per build, and what a record holds is whatever
	 * `RawBuffer.Load<MaterialParams>` reads back on the backend that will draw it.
	 *
	 * `SurfaceType::kind` is left invalid; registration assigns it.
	 *
	 * **A module that does not import the contract is not a surface**, and comes back empty rather
	 * than refused: the same directory is the game's module search path, so most of what is in it is
	 * the game's own code and none of the engine's business. A module that does import it is held to
	 * it.
	 *
	 * @param slangModule A module already loaded into the session that will compile the surface.
	 * @param surfaceName What a material names to reach this surface, normally the module file's
	 *        stem. Not a material's own name: a material names the surface it draws through.
	 * @param targetIndex Which of the session's targets the layout is read for.
	 * @return The reflected surface and the name of the struct it was read from, or nothing when the
	 *         module does not import the contract.
	 * @throws std::runtime_error if the module imports the contract but holds no conforming struct
	 *         or more than one, declares more textures than a record carries, declares a parameter
	 *         of a type the engine cannot pack, or does not reflect at all. Not
	 *         `bgl::ApiError`: that type is the renderer's to throw, and registration is the seam
	 *         where a bad module becomes one.
	 */
	std::optional<ReflectedSurface>
	ReflectSurface(
		slang::IModule*  slangModule,
		std::string_view surfaceName,
		SlangInt         targetIndex = 0);
}
