#include <bgl_common/PassScheduler.h>

namespace bgl
{
	size_t
	PassScheduler::AddPass(Pass pass)
	{
		m_Passes.emplace_back(std::move(pass));
		return m_Passes.size() - 1;
	}

	void
	PassScheduler::Compile()
	{
		// Keyed on views into the stored names: nothing is added to m_Passes while this runs, so
		// they outlive the map.
		std::unordered_map<std::string_view, int32_t> producer;

		for (size_t p = 0; p < m_Passes.size(); ++p)
		{
			Node& node = m_Passes[p];
			node.deps.clear();
			node.kept = false;

			for (const Access& a : node.pass.accesses)
			{
				// Taken for a write too: the graph cannot tell an overwrite from an accumulation,
				// so the earlier writer stays live.
				if (const auto it = producer.find(a.name); it != producer.end())
				{
					node.deps.push_back(it->second);
				}
				if (a.isWrite)
				{
					producer[a.name] = static_cast<int32_t>(p);
				}
			}
		}

		std::stack<size_t> live;
		for (size_t p = 0; p < m_Passes.size(); ++p)
		{
			if (m_Passes[p].pass.pinned)
			{
				m_Passes[p].kept = true;
				live.push(p);
			}
		}

		while (!live.empty())
		{
			const size_t p = live.top();
			live.pop();
			for (const int32_t d : m_Passes[p].deps)
			{
				if (!m_Passes[static_cast<size_t>(d)].kept)
				{
					m_Passes[static_cast<size_t>(d)].kept = true;
					live.push(static_cast<size_t>(d));
				}
			}
		}

		m_Order.clear();
		for (size_t p = 0; p < m_Passes.size(); ++p)
		{
			if (m_Passes[p].kept)
			{
				m_Order.push_back(p);
			}
		}
	}
}
