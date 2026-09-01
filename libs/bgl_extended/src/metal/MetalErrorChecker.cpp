#include "MetalErrorChecker.h"

#include <bgl_common/gassert.h>

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
	operator>>(const void* object, const MetalErrorChecker& checker)
	{
		if (object == nullptr)
		{
			if (!checker.ReportError())
			{
				gfatal("Metal operation failed with no diagnostics available.");
			}
		}
	}

	bool
	MetalErrorChecker::ReportError() const
	{
		if (m_Error != nullptr)
		{
			gfatal("Metal operation failed with error: {}", GetErrorDescription(m_Error));
		}

		return false;
	}
}
