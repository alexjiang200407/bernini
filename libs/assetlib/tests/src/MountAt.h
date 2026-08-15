#pragma once
#include <core/file/LooseFileSystem.h>

/**
 * A loose mount over `root`, for the predicates that resolve through one.
 *
 * Returned by value despite `LooseFileSystem` being immovable: `return T(args)` initializes the
 * caller's object directly, so a test can write the mount inline at the assertion that asks about
 * that directory rather than naming a local for each.
 */
[[nodiscard]] inline core::file::LooseFileSystem
MountAt(const std::filesystem::path& root)
{
	return core::file::LooseFileSystem(root);
}
