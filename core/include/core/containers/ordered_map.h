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
		std::vector<T>                                     nodes;
		std::unordered_map<Key, size_type, Hash, KeyEqual> index;

	public:
		ordered_map() = default;

		explicit ordered_map(size_type reserveCount) { reserve(reserveCount); }

		void
		reserve(size_type count)
		{
			nodes.reserve(count);
			index.reserve(count);
		}

		[[nodiscard]]
		bool
		contains(const Key& key) const noexcept
		{
			return index.find(key) != index.end();
		}

		template <typename K>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual>
		[[nodiscard]]
		bool
		contains(const K& key) const noexcept
		{
			return index.find(key) != index.end();
		}

		[[nodiscard]]
		T*
		find(const Key& key) noexcept
		{
			auto it = index.find(key);
			return it == index.end() ? nullptr : &nodes[it->second];
		}

		[[nodiscard]]
		const T*
		find(const Key& key) const noexcept
		{
			auto it = index.find(key);
			return it == index.end() ? nullptr : &nodes[it->second];
		}

		template <typename K>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual>
		[[nodiscard]]
		T*
		find(const K& key) noexcept
		{
			auto it = index.find(key);
			return it == index.end() ? nullptr : &nodes[it->second];
		}

		template <typename K>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual>
		[[nodiscard]]
		const T*
		find(const K& key) const noexcept
		{
			auto it = index.find(key);
			return it == index.end() ? nullptr : &nodes[it->second];
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
			auto it = index.find(key);
			if (it != index.end())
				return nodes[it->second];

			size_type idx = nodes.size();
			index.emplace(key, idx);
			nodes.emplace_back();
			return nodes.back();
		}

		T&
		operator[](Key&& key)
			requires std::is_default_constructible_v<T>
		{
			auto it = index.find(key);
			if (it != index.end())
				return nodes[it->second];

			size_type idx = nodes.size();
			index.emplace(std::move(key), idx);
			nodes.emplace_back();
			return nodes.back();
		}

		template <typename... Args>
		T&
		emplace(const Key& key, Args&&... args)
		{
			auto it = index.find(key);
			if (it != index.end())
				throw std::runtime_error("ordered_map::emplace - duplicate key");

			size_type idx = nodes.size();
			index.emplace(key, idx);
			nodes.emplace_back(std::forward<Args>(args)...);
			return nodes.back();
		}

		template <typename... Args>
		T&
		emplace(Key&& key, Args&&... args)
		{
			auto it = index.find(key);
			if (it != index.end())
				throw std::runtime_error("ordered_map::emplace - duplicate key");

			size_type idx = nodes.size();
			index.emplace(std::move(key), idx);
			nodes.emplace_back(std::forward<Args>(args)...);
			return nodes.back();
		}

		template <typename K, typename... Args>
			requires TransparentKeyCompatible<K, Key, Hash, KeyEqual> &&
		             std::constructible_from<Key, K>
		T&
		emplace(K&& key, Args&&... args)
		{
			auto it = index.find(key);
			if (it != index.end())
				throw std::runtime_error("ordered_map::emplace - duplicate key");

			size_type idx = nodes.size();
			index.emplace(Key{ std::forward<K>(key) }, idx);
			nodes.emplace_back(std::forward<Args>(args)...);
			return nodes.back();
		}

		[[nodiscard]]
		size_type
		size() const noexcept
		{
			return nodes.size();
		}

		[[nodiscard]]
		bool
		empty() const noexcept
		{
			return nodes.empty();
		}

		void
		clear() noexcept
		{
			nodes.clear();
			index.clear();
		}

		auto
		begin() noexcept
		{
			return nodes.begin();
		}
		auto
		end() noexcept
		{
			return nodes.end();
		}
		auto
		begin() const noexcept
		{
			return nodes.begin();
		}
		auto
		end() const noexcept
		{
			return nodes.end();
		}
		auto
		cbegin() const noexcept
		{
			return nodes.cbegin();
		}
		auto
		cend() const noexcept
		{
			return nodes.cend();
		}
	};
}
