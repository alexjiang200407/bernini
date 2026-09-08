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
	 * **Reflect on a scalar-layout target**, which today means a DXIL one whatever backend will
	 * draw the surface. The offsets have to be the ones `RawBuffer.Load<MaterialParams>` reads a
	 * record at, and a raw load reconstructs its type from scalar loads on every backend. A Metal
	 * target reflects a structured-buffer element under MSL's rules instead -- a float3 aligned to
	 * 16 rather than packed at 4 -- which is a true layout for a different accessor
	 * (`EntryBuffer<T>`) and not the one a record is read with. Reflecting it moves every field
	 * after the first vector, and the shader reads a texture's slot index out of the bytes of the
	 * value before it.
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
