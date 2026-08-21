#pragma once

#include "VersionControl/IVersionControl.h"
#include "VersionControl/git_cli.h"

namespace editor
{
	/** IVersionControl over the `git` command line, which is the only implementation there is. */
	class GitVersionControl : public IVersionControl
	{
	public:
		/**
		 * @param repositoryRoot what the backend runs in.
		 * @param dataDirectory the project's assets, which ADR-10's reference check is asked about.
		 */
		GitVersionControl(
			std::filesystem::path repositoryRoot,
			std::filesystem::path dataDirectory) noexcept;

		[[nodiscard]] std::vector<PendingChange>
		ListChanges() const override;

		[[nodiscard]] VersionControlOutcome
		Submit(const std::vector<QString>& assets, const QString& message) override;

		[[nodiscard]] std::vector<QString>
		ListIncoming() override;

		[[nodiscard]] VersionControlOutcome
		GetLatest() override;

		[[nodiscard]] VersionControlOutcome
		Revert(const std::vector<QString>& assets) override;

		[[nodiscard]] std::vector<Submission>
		ListHistory(int limit) const override;

		[[nodiscard]] VersionControlOutcome
		UndoSubmission(const QString& id) override;

		[[nodiscard]] const std::filesystem::path&
		GetRoot() const noexcept override
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

		/**
		 * Publishes what was just recorded, unrecording it again when the shared project turns out to
		 * have moved in between -- so a recorded change that cannot be published never survives.
		 *
		 * @param recordedOver what HEAD was before the recording, to go back to.
		 */
		[[nodiscard]] VersionControlOutcome
		PublishOrUnrecord(const QString& recordedOver);

		/** Absolute paths a submission added, which undoing it would take away again. */
		[[nodiscard]] std::vector<std::filesystem::path>
		AdditionsOf(const QString& id) const;

		/**
		 * ADR-10 over `deleted`: kAssetsStillInUse naming both ends of every reference that would
		 * break, or kDone when nothing left behind would point at a hole.
		 */
		[[nodiscard]] VersionControlOutcome
		GuardDeletions(const std::vector<std::filesystem::path>& deleted) const;

		/** Absolute paths the shared project's work would remove. */
		[[nodiscard]] std::vector<std::filesystem::path>
		IncomingDeletions() const;

		std::filesystem::path m_RepositoryRoot;
		std::filesystem::path m_DataDirectory;
	};
}
