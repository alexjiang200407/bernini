#include "Windows/VersionControl/version_control_rules.h"

namespace
{
	namespace fs = std::filesystem;

	const fs::path c_Root = fs::temp_directory_path() / "bernini_rules_project";

	// What the backend calls things, and what none of these sentences may.
	constexpr std::array c_BackendWords = { "git",    "commit", "push",    "pull",
		                                    "rebase", "HEAD",   "upstream" };

	editor::PendingChange
	Change(const char* path, editor::ChangeKind kind)
	{
		return { .path = QString::fromUtf8(path), .renamedFrom = std::nullopt, .kind = kind };
	}
}

TEST_CASE("An asset a window still holds open is named among what would change", "[vcs]")
{
	const std::vector<QString> changing{ "Data/Materials/coyote.bmaterial",
		                                 "Data/Meshes/coyote.bmesh" };

	SECTION("what no window holds is not held open")
	{
		CHECK(editor::HeldOpenAmong(c_Root, changing, {}).empty());
	}

	SECTION("a window holding one of them names that one")
	{
		const QStringList open{ QString::fromStdWString(
			(c_Root / "Data/Materials/coyote.bmaterial").generic_wstring()) };

		CHECK(
			editor::HeldOpenAmong(c_Root, changing, open) ==
			std::vector<QString>{ "Data/Materials/coyote.bmaterial" });
	}

	SECTION("a window holding something else entirely holds nothing back")
	{
		const QStringList open{ QString::fromStdWString(
			(c_Root / "Data/Materials/squirrel.bmaterial").generic_wstring()) };

		CHECK(editor::HeldOpenAmong(c_Root, changing, open).empty());
	}

	// A window outside the project cannot be holding a project asset, and must not throw off the
	// comparison by being un-relativisable.
	SECTION("a path outside the project is ignored rather than matched")
	{
		const QStringList open{ QString::fromStdWString(
			(c_Root.parent_path() / "elsewhere/coyote.bmaterial").generic_wstring()) };

		CHECK(editor::HeldOpenAmong(c_Root, changing, open).empty());
	}
}

// ADR-2: every sentence the window shows is written here, so replacing the backend changes none of
// them -- and none of them may name one.
TEST_CASE("Every refusal has words of its own, and none of them names a backend", "[vcs]")
{
	using editor::ChangeKind;
	using editor::VersionControlStatus;

	static constexpr std::array c_Refusals = {
		VersionControlStatus::kNoIdentity,          VersionControlStatus::kNothingToDo,
		VersionControlStatus::kCouldNotReachShared, VersionControlStatus::kWorkHasMovedOn,
		VersionControlStatus::kWouldNotFastForward, VersionControlStatus::kAssetsInTheWay,
		VersionControlStatus::kAssetsStillInUse,    VersionControlStatus::kAssetsChangedSince,
	};

	std::vector<QString> said;
	for (const VersionControlStatus status : c_Refusals)
	{
		const auto words = editor::DescribeOutcome(
			{ .status = status, .assets = { "Data/Materials/coyote.bmaterial" } });

		CAPTURE(static_cast<int>(status));
		CHECK_FALSE(words.isEmpty());

		for (const char* backend : c_BackendWords)
		{
			CHECK_FALSE(words.contains(QString::fromUtf8(backend), Qt::CaseInsensitive));
		}

		// Two refusals that read the same would leave the user unable to tell them apart.
		CHECK(std::ranges::find(said, words) == said.end());
		said.push_back(words);
	}

	CHECK(editor::DescribeOutcome({ .status = VersionControlStatus::kDone }).isEmpty());

	// Every other sentence the user can be shown, held to the same rule.
	for (const QString& words : { editor::DescribeUnavailable(),
	                              editor::DescribeHeldOpen({ "Data/Materials/coyote.bmaterial" }),
	                              editor::DescribeRevert(3),
	                              editor::DescribeChange(ChangeKind::kAdded),
	                              editor::DescribeChange(ChangeKind::kModified),
	                              editor::DescribeChange(ChangeKind::kDeleted),
	                              editor::DescribeChange(ChangeKind::kRenamed) })
	{
		CAPTURE(words.toStdString());
		CHECK_FALSE(words.isEmpty());
		for (const char* backend : c_BackendWords)
		{
			CHECK_FALSE(words.contains(QString::fromUtf8(backend), Qt::CaseInsensitive));
		}
	}
}

TEST_CASE("A refusal names the assets it is about, and stops naming them at three", "[vcs]")
{
	using editor::VersionControlStatus;

	const auto one = editor::DescribeOutcome(
		{ .status = VersionControlStatus::kAssetsStillInUse,
	      .assets = { "Data/Materials/coyote.bmaterial" } });
	CHECK(one.contains(QString("coyote.bmaterial")));

	// The path is not the name: an artist knows the asset, not where the repository keeps it.
	CHECK_FALSE(one.contains(QString("Data/Materials")));

	const auto many = editor::DescribeOutcome(
		{ .status = VersionControlStatus::kAssetsStillInUse,
	      .assets = { "a.bmaterial",
	                  "b.bmaterial",
	                  "c.bmaterial",
	                  "d.bmaterial",
	                  "e.bmaterial" } });

	CHECK(many.contains(QString("a.bmaterial")));
	CHECK(many.contains(QString("2 more")));
	CHECK_FALSE(many.contains(QString("e.bmaterial")));
}

// The first person to read this message asked "which other assets?", which is the half it was
// leaving out.
TEST_CASE("A blocked removal says what still uses it", "[vcs]")
{
	const auto said = editor::DescribeOutcome(
		{ .status   = editor::VersionControlStatus::kAssetsStillInUse,
	      .assets   = { "Data/Skeletons/Squirrel/Squirrel.bskel" },
	      .neededBy = { "Data/Animations/Squirrel/Squirrel.banim" } });

	CHECK(said.contains(QString("Squirrel.bskel (used by Squirrel.banim)")));

	// Missing holders must not lose the assets they belong to, whatever produced the outcome.
	const auto lopsided = editor::DescribeOutcome(
		{ .status = editor::VersionControlStatus::kAssetsStillInUse,
	      .assets = { "Data/Skeletons/Dog/Dog.bskel" } });

	CHECK(lopsided.contains(QString("Dog.bskel")));
	CHECK_FALSE(lopsided.contains(QString("used by")));
}

TEST_CASE("A submission starts with a message describing what was chosen", "[vcs]")
{
	CHECK(editor::DefaultSubmitMessage({}).isEmpty());

	CHECK(
		editor::DefaultSubmitMessage(
			{ Change("Data/Meshes/coyote.bmesh", editor::ChangeKind::kAdded) }) ==
		QString("New coyote.bmesh"));

	CHECK(
		editor::DefaultSubmitMessage(
			{ Change("Data/Meshes/coyote.bmesh", editor::ChangeKind::kModified),
	          Change("Data/Meshes/squirrel.bmesh", editor::ChangeKind::kDeleted) }) ==
		QString("Updated 2 assets"));
}

TEST_CASE("A change is described in the words the menu uses", "[vcs]")
{
	CHECK(editor::DescribeChange(editor::ChangeKind::kAdded) == QString("New"));
	CHECK(editor::DescribeChange(editor::ChangeKind::kModified) == QString("Changed"));
	CHECK(editor::DescribeChange(editor::ChangeKind::kDeleted) == QString("Removed"));
	CHECK(editor::DescribeChange(editor::ChangeKind::kRenamed) == QString("Moved"));
}

// Nowhere to get it back from, so the question has to say so rather than just asking twice.
TEST_CASE("Throwing unsubmitted work away says how much, and that it is gone", "[vcs]")
{
	const auto asked = editor::DescribeRevert(3);

	CHECK(asked.contains(QString("3")));
	CHECK(asked.contains(QString("nowhere to get it back")));
}

TEST_CASE("A submission reads as when, who and what they said", "[vcs]")
{
	editor::Submission submission;
	submission.author  = "Bernini Test";
	submission.message = "bob adds a squirrel";
	submission.when    = QDateTime::fromString("2026-08-22T09:30:00+00:00", Qt::ISODate);

	const auto described = editor::DescribeSubmission(submission);

	CHECK(described.contains(QString("Bernini Test")));
	CHECK(described.contains(QString("bob adds a squirrel")));
	CHECK_FALSE(described.startsWith(QString("Bernini")));

	// A submission made at a terminal carries a whole body, and this is one row.
	submission.message = "Bake the posed culling boxes\n\nassetlib_cli bakebounds, from #421.";
	const auto oneLine = editor::DescribeSubmission(submission);

	CHECK(oneLine.contains(QString("Bake the posed culling boxes")));
	CHECK_FALSE(oneLine.contains(QString("bakebounds")));
	CHECK_FALSE(oneLine.contains(QChar('\n')));
}
