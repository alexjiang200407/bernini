#pragma once

#include "VersionControl/IVersionControl.h"
#include "VersionControl/git_cli.h"

namespace editor
{
	/** IVersionControl over the `git` command line, which is the only implementation there is. */
	class GitVersionControl : public IVersionControl
	{
	public:
		explicit GitVersionControl(std::filesystem::path repositoryRoot) noexcept;

		[[nodiscard]] std::vector<PendingChange>
		ListChanges() const override;

		[[nodiscard]] VersionControlOutcome
		Submit(const std::vector<QString>& assets, const QString& message) override;

		[[nodiscard]] VersionControlOutcome
		GetLatest() override;

		[[nodiscard]] VersionControlOutcome
		Revert(const std::vector<QString>& assets) override;

		[[nodiscard]] const std::filesystem::path&
		GetRepositoryRoot() const noexcept
		{
			return m_RepositoryRoot;
		}

	private:
		/** Whether this machine has been told who is submitting; git will not record a change without it. */
		[[nodiscard]] bool
		HasIdentity() const;

		/**
		 * How many submissions `range` spans, or nothing when there is no shared project to compare
		 * against. `HEAD..@{upstream}` is what the shared project has that this one does not.
		 */
		[[nodiscard]] std::optional<int>
		CountSubmissions(const QString& range) const;

		/** Every asset that differs between two points, which is what an update would change. */
		[[nodiscard]] std::vector<QString>
		ChangedBetween(const QString& from, const QString& to) const;

		std::filesystem::path m_RepositoryRoot;
	};
}
