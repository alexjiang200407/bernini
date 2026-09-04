#include "json_doc.h"

#include <charconv>
#include <core/err/util.h>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

	std::vector<std::byte>
	toBytes(const nlohmann::json& json)
	{
		const std::string text = canonicalDump(json);
		const auto        data = std::as_bytes(std::span(text.data(), text.size()));
		return { data.begin(), data.end() };
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
