#pragma once

namespace core
{
	template <typename K, typename Key, typename Hash, typename KeyEqual>
	concept TransparentKeyCompatible =
		requires(const Hash& h, const KeyEqual& eq, const K& k, const Key& key) {
			typename Hash::is_transparent;
			typename KeyEqual::is_transparent;
			{ h(k) } -> std::convertible_to<std::size_t>;
			{ eq(k, key) } -> std::convertible_to<bool>;
			{ eq(key, k) } -> std::convertible_to<bool>;
		};

	template <
		typename Key,
		typename T,
		typename Hash     = std::hash<Key>,
		typename KeyEqual = std::equal_to<Key>>
	class ordered_map
	{
	public:
		using key_type    = Key;
		using mapped_type = T;
		using value_type  = T;
		using size_type   = std::size_t;
		using hasher      = Hash;
		using key_equal   = KeyEqual;

	private:
		std::vector<T>                                     m_Nodes;
		std::unordered_map<Key, size_type, Hash, KeyEqual> m_Index;

	public:
		ordered_map() = default;

		explicit ordered_map(size_type reserveCount) { reserve(reserveCount); }

		void
		reserve(size_type count)
		{
			m_Nodes.reserve(count);
			m_Index.reserve(count);
		}

		[[nodiscard]]
		bool
		contains(const Key& key) const noexcept
		{
			return m_Index.find(key) != m_Index.end();
		}

		template <typename K>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual>
		[[nodiscard]]
		bool
		contains(const K& key) const noexcept
		{
			return m_Index.find(key) != m_Index.end();
		}

		[[nodiscard]]
		T*
		find(const Key& key) noexcept
		{
			auto it = m_Index.find(key);
			return it == m_Index.end() ? nullptr : &m_Nodes[it->second];
		}

		[[nodiscard]]
		const T*
		find(const Key& key) const noexcept
		{
			auto it = m_Index.find(key);
			return it == m_Index.end() ? nullptr : &m_Nodes[it->second];
		}

		template <typename K>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual>
		[[nodiscard]]
		T*
		find(const K& key) noexcept
		{
			auto it = m_Index.find(key);
			return it == m_Index.end() ? nullptr : &m_Nodes[it->second];
		}

		template <typename K>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual>
		[[nodiscard]]
		const T*
		find(const K& key) const noexcept
		{
			auto it = m_Index.find(key);
			return it == m_Index.end() ? nullptr : &m_Nodes[it->second];
		}

		// at with exact Key type
		T&
		at(const Key& key)
		{
			auto* ptr = find(key);
			if (!ptr)
				throw std::out_of_range("ordered_map::at - key not found");
			return *ptr;
		}

		const T&
		at(const Key& key) const
		{
			auto* ptr = find(key);
			if (!ptr)
				throw std::out_of_range("ordered_map::at - key not found");
			return *ptr;
		}

		template <typename K>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual>
		T&
		at(const K& key)
		{
			auto* ptr = find(key);
			if (!ptr)
				throw std::out_of_range("ordered_map::at - key not found");
			return *ptr;
		}

		template <typename K>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual>
		const T&
		at(const K& key) const
		{
			auto* ptr = find(key);
			if (!ptr)
				throw std::out_of_range("ordered_map::at - key not found");
			return *ptr;
		}

		T&
		operator[](const Key& key)
			requires std::is_default_constructible_v<T>
		{
			auto it = m_Index.find(key);
			if (it != m_Index.end())
				return m_Nodes[it->second];

			size_type idx = m_Nodes.size();
			m_Index.emplace(key, idx);
			m_Nodes.emplace_back();
			return m_Nodes.back();
		}

		T&
		operator[](Key&& key)
			requires std::is_default_constructible_v<T>
		{
			auto it = m_Index.find(key);
			if (it != m_Index.end())
				return m_Nodes[it->second];

			size_type idx = m_Nodes.size();
			m_Index.emplace(std::move(key), idx);
			m_Nodes.emplace_back();
			return m_Nodes.back();
		}

		template <typename... Args>
		T&
		emplace(const Key& key, Args&&... args)
		{
			auto it = m_Index.find(key);
			if (it != m_Index.end())
				throw std::runtime_error("ordered_map::emplace - duplicate key");

			size_type idx = m_Nodes.size();
			m_Index.emplace(key, idx);
			m_Nodes.emplace_back(std::forward<Args>(args)...);
			return m_Nodes.back();
		}

		template <typename... Args>
		T&
		emplace(Key&& key, Args&&... args)
		{
			auto it = m_Index.find(key);
			if (it != m_Index.end())
				throw std::runtime_error("ordered_map::emplace - duplicate key");

			size_type idx = m_Nodes.size();
			m_Index.emplace(std::move(key), idx);
			m_Nodes.emplace_back(std::forward<Args>(args)...);
			return m_Nodes.back();
		}

		template <typename K, typename... Args>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual> &&
		             std::constructible_from<Key, K>
		T&
		emplace(K&& key, Args&&... args)
		{
			auto it = m_Index.find(key);
			if (it != m_Index.end())
				throw std::runtime_error("ordered_map::emplace - duplicate key");

			size_type idx = m_Nodes.size();
			m_Index.emplace(Key{ std::forward<K>(key) }, idx);
			m_Nodes.emplace_back(std::forward<Args>(args)...);
			return m_Nodes.back();
		}

		[[nodiscard]]
		size_type
		size() const noexcept
		{
			return m_Nodes.size();
		}

		[[nodiscard]]
		bool
		empty() const noexcept
		{
			return m_Nodes.empty();
		}

		void
		clear() noexcept
		{
			m_Nodes.clear();
			m_Index.clear();
		}

		auto
		begin() noexcept
		{
			return m_Nodes.begin();
		}
		auto
		end() noexcept
		{
			return m_Nodes.end();
		}
		auto
		begin() const noexcept
		{
			return m_Nodes.begin();
		}
		auto
		end() const noexcept
		{
			return m_Nodes.end();
		}
		auto
		cbegin() const noexcept
		{
			return m_Nodes.cbegin();
		}
		auto
		cend() const noexcept
		{
			return m_Nodes.cend();
		}
	};
}
