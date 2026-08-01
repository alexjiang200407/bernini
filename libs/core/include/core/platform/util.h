#pragma once

namespace core
{
	std::string
	get_executable_name() noexcept;

	// This process's id. The standard library has none; this is getpid/GetCurrentProcessId.
	[[nodiscard]] uint32_t
	process_id() noexcept;
}
