#pragma once
#include <bgl/SurfaceType.h>

#include <filesystem>
#include <vector>

namespace bgl
{
	class IDevice;

	/**
	 * Reads every surface in `dir` and binds each to one of the reserved game slots.
	 *
	 * Each `.slang` directly in the directory is one surface: its name is the file's stem, and its
	 * slot is its position in filename order, so nothing outside the directory names a file. Each is
	 * reflected first and bound afterwards, because binding drops every live Slang session.
	 *
	 * Runs once, before any pipeline is built. Nothing rebuilds a pipeline afterwards, so a surface
	 * added to the directory later is seen at the next launch and not before.
	 *
	 * @param device The device whose compiler reads the modules and whose slots are bound.
	 * @param dir The client's surface directory. Empty registers nothing.
	 * @return One entry per surface, in slot order, each carrying the material kind it was given.
	 * @throws ApiError if the directory does not exist, holds more surfaces than there are slots, or
	 *         holds a file whose stem is not a name a shader can import; and for anything the
	 *         reflection itself refuses.
	 */
	[[nodiscard]] std::vector<SurfaceType>
	RegisterSurfaces(IDevice& device, const std::filesystem::path& dir);
}
