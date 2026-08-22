#include "json_doc.h"

#include <core/err/util.h>

namespace assetlib::doc
{
	nlohmann::json
	parseObject(std::string_view text, std::string_view what)
	{
		auto json = nlohmann::json::parse(text, nullptr, false);
		core::throw_runtime_error_if(
			json.is_discarded() || !json.is_object(),
			"{} is not a JSON object",
			what);
		return json;
	}

	std::string
	canonicalDump(const nlohmann::json& json)
	{
		return json.dump(1, '\t') + '\n';
	}

	double
	plainFloat(float value)
	{
		const std::string text = std::format("{}", value);

		double out = 0.0;
		std::from_chars(text.data(), text.data() + text.size(), out);
		return out;
	}
}
