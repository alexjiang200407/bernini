#include "gfx/surface_registry.h"

#include "device/Device.h"
#include "slang/SlangSessions.h"
#include "util/util.h"
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl_common/SurfaceReflection.h>
#include <core/log/log.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		// The stem becomes an `import` in a module the engine writes, so it has to be spellable
		// there. A file the client can name and the shader cannot is the one refusal that would
		// otherwise surface as a compile error inside generated text nobody wrote.
		bool
		IsImportableName(const std::string& stem) noexcept
		{
			if (stem.empty() || (std::isdigit(static_cast<unsigned char>(stem.front())) != 0))
				return false;

			return std::ranges::all_of(stem, [](char c) {
				return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
			});
		}

		// What the slot's programs import. The tree ships a file of this name aliasing the null
		// surface; a module loaded from source under it shadows that file.
		std::string
		BindingModuleName(uint32_t slot)
		{
			return std::format("game.slot{}", slot);
		}

		std::string
		BindingModuleSource(uint32_t slot, const std::string& module, const std::string& sourceType)
		{
			return std::format(
				"import {};\npublic typealias Slot{}Surface = {};\n",
				module,
				slot,
				sourceType);
		}

		std::vector<std::filesystem::path>
		SurfaceFiles(const std::filesystem::path& dir)
		{
			std::vector<std::filesystem::path> files;
			for (const std::filesystem::directory_entry& entry :
			     std::filesystem::directory_iterator(dir))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".slang")
					files.emplace_back(entry.path());
			}

			// Filename order, so a slot is decided by the directory alone and a file added later
			// does not renumber the ones before it.
			std::ranges::sort(files);
			return files;
		}
	}

	std::vector<SurfaceType>
	RegisterSurfaces(IDevice& device, const std::filesystem::path& dir)
	{
		if (dir.empty())
			return {};

		if (!std::filesystem::is_directory(dir))
		{
			throw ApiError(
				std::format("surfaceShaderDir '{}' is not a directory", dir.generic_string()));
		}

		std::vector<SurfaceType> types;
		std::vector<std::string> sourceTypes;

		for (const std::filesystem::path& file : SurfaceFiles(dir))
		{
			const std::string stem = file.stem().string();

			// Nothing could name it, so nothing here can be a surface. The same directory is the
			// game's module search path, and what it calls its own files is its business.
			if (!IsImportableName(stem))
			{
				logger::debug("surface directory: skipping '{}', not an importable name", stem);
				continue;
			}

			std::optional<ReflectedSurface> reflected;
			try
			{
				reflected = device.ReflectSurfaceModule(stem, stem);
			}
			catch (const std::exception& e)
			{
				// The reflection answers to bgl_common, which cannot throw the API's error; this is
				// the seam where a bad module becomes one the client catches.
				throw ApiError(e.what());
			}

			// A module that never imported the contract is the game's own code sitting in its own
			// shader directory, not a surface that failed to be one.
			if (!reflected.has_value())
			{
				logger::debug("surface directory: '{}' declares no surface, skipping", stem);
				continue;
			}

			if (types.size() == cGameSlots)
			{
				throw ApiError(
					std::format(
						"surfaceShaderDir '{}' holds more surfaces than the {} slots the engine "
						"reserves; '{}' is past the last",
						dir.generic_string(),
						cGameSlots,
						stem));
			}

			reflected->type.kind = GameSlotKind(static_cast<uint32_t>(types.size()));
			types.emplace_back(std::move(reflected->type));
			sourceTypes.emplace_back(std::move(reflected->sourceTypeName));
		}

		// Only now: a binding drops every session, so reflecting after one would rebuild Slang's
		// core module for each surface after the first.
		for (uint32_t slot = 0; slot < types.size(); ++slot)
		{
			device.AddSourceModule(
				{ BindingModuleName(slot),
			      BindingModuleSource(slot, types[slot].name, sourceTypes[slot]) });

			logger::info("surface '{}' registered into game slot {}", types[slot].name, slot);
		}

		return types;
	}
}
