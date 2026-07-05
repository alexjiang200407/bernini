#include <CLI/CLI.hpp>
#include <assetlib/assetlib.h>

int
main(int argc, char** argv)
{
	CLI::App app{ "Bernini asset pipeline CLI" };
	app.set_version_flag("--version", assetlib::Version());

	CLI11_PARSE(app, argc, argv);
	return 0;
}
