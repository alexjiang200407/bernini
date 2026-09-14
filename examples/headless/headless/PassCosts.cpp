#include <algorithm>
#include <bgl/PassHistory.h>
#include <cstddef>
#include <format>
#include <headless/PassCosts.h>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace headless
{
	namespace
	{
		/** `samples` must not be empty. */
		[[nodiscard]] PassCost
		Summarise(std::string name, std::vector<double> samples)
		{
			std::ranges::sort(samples);
			return PassCost{ .name   = std::move(name),
				             .median = samples[samples.size() / 2],
				             .max    = samples.back(),
				             .frames = samples.size() };
		}
	}

	PassCosts
	SummarisePasses(const bgl::PassHistory& history)
	{
		auto costs = PassCosts();

		for (std::size_t pass = 0; pass < history.Passes().size(); ++pass)
		{
			std::vector<double> samples;
			for (std::size_t sample = 0; sample < history.SampleCount(); ++sample)
			{
				if (const std::optional<double> cell = history.At(sample, pass))
					samples.push_back(*cell);
			}

			if (!samples.empty())
				costs.passes.push_back(
					Summarise(std::string(history.Passes()[pass]), std::move(samples)));
		}

		std::ranges::sort(costs.passes, [](const PassCost& a, const PassCost& b) {
			return a.median > b.median;
		});

		std::vector<double> totals;
		for (std::size_t sample = 0; sample < history.SampleCount(); ++sample)
		{
			totals.push_back(history.TotalAt(sample));
		}
		if (!totals.empty())
			costs.frame = Summarise("frame", std::move(totals));

		return costs;
	}

	void
	PrintPassCosts(std::ostream& out, const PassCosts& costs)
	{
		std::size_t width = 5;
		for (const PassCost& cost : costs.passes)
		{
			width = std::max(width, cost.name.size());
		}

		const auto row = [&](const PassCost& cost) {
			out << std::format(
				"{:<{}}  {:>10.3f}  {:>10.3f}  {:>7}\n",
				cost.name,
				width,
				cost.median,
				cost.max,
				cost.frames);
		};

		out << std::format(
			"{:<{}}  {:>10}  {:>10}  {:>7}\n",
			"pass",
			width,
			"median ms",
			"max ms",
			"frames");
		out << std::string(width + 33, '-') << '\n';

		for (const PassCost& cost : costs.passes)
		{
			row(cost);
		}

		if (costs.frame)
		{
			out << std::string(width + 33, '-') << '\n';
			row(*costs.frame);
		}
	}
}
