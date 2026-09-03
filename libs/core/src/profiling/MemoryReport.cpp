#include <core/platform/memory.h>
#include <core/profiling/MemoryReport.h>
#include <core/str/str.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace core::profiling
{
	MemorySnapshot
	memory_snapshot() noexcept
	{
		MemorySnapshot snapshot{};

		for (std::size_t tag = 0; tag < c_MemoryTagCount; ++tag)
		{
			const auto kind = static_cast<MemoryTag>(tag);
			snapshot.tags[tag] =
				MemoryReportEntry{ .name = tag_name(kind), .totals = tag_totals(kind) };
		}

		snapshot.tagged = memory_totals();

		const ProcessMemory process = process_memory();
		snapshot.footprint          = process.footprint;
		snapshot.footprintPeak      = process.peak;
		snapshot.untagged =
			process.footprint > snapshot.tagged.live ? process.footprint - snapshot.tagged.live : 0;

		return snapshot;
	}

	std::string
	format_memory_report(const MemorySnapshot& snapshot)
	{
		auto lines = std::vector<std::string>();
		lines.emplace_back("memory report (peak / live, allocations)");

		auto ordered = std::vector<const MemoryReportEntry*>();
		for (const MemoryReportEntry& entry : snapshot.tags) ordered.emplace_back(&entry);

		// Largest peak first: the report is read to find a hog, and a hog is what the top line
		// should be. Ties keep tag order, so a run's table does not reshuffle between reports.
		std::ranges::stable_sort(
			ordered,
			std::ranges::greater{},
			[](const MemoryReportEntry* entry) { return entry->totals.peak; });

		for (const MemoryReportEntry* entry : ordered)
		{
			if (entry->totals.peak == 0)
				continue;

			lines.emplace_back(
				std::format(
					"  {:<16} {:>12} peak  {:>12} live  {} allocations",
					entry->name,
					str::format_bytes(entry->totals.peak),
					str::format_bytes(entry->totals.live),
					entry->totals.allocations));
		}

		lines.emplace_back(
			std::format(
				"  {:<16} {:>12} peak  {:>12} live",
				"tagged",
				str::format_bytes(snapshot.tagged.peak),
				str::format_bytes(snapshot.tagged.live)));

		if (snapshot.footprint == 0)
		{
			lines.emplace_back(
				"  the platform reports no process footprint, so nothing is untagged");
		}
		else
		{
			lines.emplace_back(
				std::format(
					"  {:<16} {:>12} peak  {:>12} live",
					"process",
					str::format_bytes(snapshot.footprintPeak),
					str::format_bytes(snapshot.footprint)));
			lines.emplace_back(
				std::format(
					"  {:<16} {:>12}       (footprint the tags do not account for)",
					"untagged",
					str::format_bytes(snapshot.untagged)));
		}

		std::string report;
		for (const std::string& line : lines)
		{
			if (!report.empty())
				report += '\n';
			report += line;
		}
		return report;
	}

	bool
	write_memory_report(const MemorySnapshot& snapshot, const std::filesystem::path& path) noexcept
	{
		try
		{
			auto tags = nlohmann::json::array();
			for (const MemoryReportEntry& entry : snapshot.tags)
			{
				tags.push_back(
					{
						{ "name", entry.name },
						{ "liveBytes", entry.totals.live },
						{ "peakBytes", entry.totals.peak },
						{ "allocations", entry.totals.allocations },
					});
			}

			const nlohmann::json document = {
				{ "tags", tags },
				{ "taggedLiveBytes", snapshot.tagged.live },
				{ "taggedPeakBytes", snapshot.tagged.peak },
				{ "footprintBytes", snapshot.footprint },
				{ "footprintPeakBytes", snapshot.footprintPeak },
				{ "untaggedBytes", snapshot.untagged },
			};

			std::ofstream out(path);
			if (!out)
				return false;

			out << document.dump(2) << '\n';
			return out.good();
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	MemoryReport::MemoryReport(std::filesystem::path jsonPath) noexcept :
		m_JsonPath(std::move(jsonPath))
	{}

	MemoryReport::~MemoryReport() noexcept
	{
		try
		{
			const MemorySnapshot snapshot = memory_snapshot();

			spdlog::info("{}", format_memory_report(snapshot));

			if (!m_JsonPath.empty() && !write_memory_report(snapshot, m_JsonPath))
				spdlog::warn("could not write the memory report to {}", m_JsonPath.string());
		}
		catch (const std::exception&)
		{
			// A destructor at the end of main, whose whole job is to say what a run cost. Throwing
			// out of it would terminate a process that had already finished its work.
		}
	}
}
