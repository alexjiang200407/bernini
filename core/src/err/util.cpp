#include <core/err/util.h>
#include <core/win/util.h>
#include <cpptrace/cpptrace.hpp>

namespace core
{
	void
	crash_signal_handle(int signal)
	{
		std::string exe_name     = get_executable_name();
		std::string log_filename = "./" + exe_name + "_crash.log";

		std::ofstream log_file(log_filename, std::ios::out | std::ios::trunc);

		if (log_file.is_open())
		{
			log_file << "--- CRASH DETECTED (Signal " << signal << ") ---\n";

			cpptrace::generate_trace().print(log_file);

			log_file.close();
		}
		std::exit(signal);
	}
}
