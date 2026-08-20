#pragma once

#include "Revisions/IRevisionControl.h"

namespace editor
{
	/** IRevisionControl over the `git` command line, which is the only implementation there is. */
	class GitRevisionControl : public IRevisionControl
	{
	public:
		explicit GitRevisionControl(std::filesystem::path repositoryRoot) noexcept;

		[[nodiscard]] std::vector<PendingChange>
		ListChanges() const override;

		[[nodiscard]] const std::filesystem::path&
		GetRepositoryRoot() const noexcept
		{
			return m_RepositoryRoot;
		}

	private:
		std::filesystem::path m_RepositoryRoot;
	};
}
