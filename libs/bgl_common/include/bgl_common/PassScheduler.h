#pragma once

namespace bgl
{
	/**
	 * Dependency derivation, dead-pass culling and execution order over passes that declare what
	 * they read and write. A pass is addressed by index, and the renderer keeps everything else
	 * about it under that index.
	 *
	 * Ordering is submission order. Compile builds last-writer edges to drive culling; it does not
	 * reschedule.
	 */
	class PassScheduler
	{
	public:
		struct Access
		{
			/**
			 * Already resolved by the renderer. How a name is scoped is that renderer's rule, so
			 * two accesses mean one resource here exactly when their resolved names are equal --
			 * which is also what makes an unresolvable name a resource of its own.
			 */
			std::string name;
			bool        isWrite;
		};

		/**
		 * What the scheduling reads, which is deliberately not what a renderer records: no exec
		 * callback, no queue, no attachment, not even a name. Each of those is spelled in one
		 * renderer's vocabulary -- an exec takes that renderer's pass context, holding its command
		 * list and its handle types -- so none of them can be named in this tier. Nor would an exec
		 * be called from here in any case: this orders passes, and the renderer runs them.
		 */
		struct Pass
		{
			/**
			 * Keeps the pass through culling whatever becomes of its outputs. The renderer folds
			 * every reason it has into this one flag -- a side-effect pin, an attachment, a write
			 * to a resource it imported -- because culling treats them all identically and none of
			 * them can be named in this tier.
			 */
			bool pinned = false;

			std::vector<Access> accesses;
		};

		// Returns the pass's index, which is how it is addressed from here on.
		size_t
		AddPass(Pass pass);

		/**
		 * Derives the edges, then keeps every pinned pass and everything reachable backward from
		 * one. Idempotent: a second call recomputes from the passes as they now stand.
		 */
		void
		Compile();

		// The surviving passes, in submission order. Empty until Compile.
		[[nodiscard]] const std::vector<size_t>&
		Order() const noexcept
		{
			return m_Order;
		}

		[[nodiscard]] bool
		WasCulled(size_t index) const noexcept
		{
			return index < m_Passes.size() && !m_Passes[index].kept;
		}

		[[nodiscard]] size_t
		Count() const noexcept
		{
			return m_Passes.size();
		}

		void
		Clear() noexcept
		{
			m_Passes.clear();
			m_Order.clear();
		}

	private:
		struct Node
		{
			Pass                 pass;
			std::vector<int32_t> deps;
			bool                 kept = false;
		};

		std::vector<Node>   m_Passes;
		std::vector<size_t> m_Order;
	};
}
