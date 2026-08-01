#include "MetalErrorChecker.h"

#include "error/gassert.h"

namespace bgl
{
	std::string
	GetErrorDescription(const NS::Error* error)
	{
		if (error == nullptr)
			return "no reason given";

		// Every field here is itself optional, so each is read through its own guard rather than
		// chained: a partially-populated NSError is the normal shape, not a malformed one.
		const NS::String* description = error->localizedDescription();
		const NS::String* domain      = error->domain();

		auto out =
			std::string(description != nullptr ? description->utf8String() : "no description");

		if (domain != nullptr)
			out += std::format(" [{} {}]", domain->utf8String(), error->code());

		return out;
	}

	void
	ReportMetalFailure(const std::string_view what, const NS::Error* error)
	{
		gfatal("Metal Error: {} failed: {}", what, GetErrorDescription(error));
	}
}
