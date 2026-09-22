#include "util/held_open_assets.h"

#include <QObject>
#include <editor_plugin_api/EditorPanel.h>

#include <catch2/catch_test_macros.hpp>
#include <qcontainerfwd.h>
#include <qwidget.h>
#include <string>
#include <utility>
#include <vector>

namespace
{
	// No Q_OBJECT: AUTOMOC is off for this target and nothing here needs a signal. Being a QObject
	// at all is the whole qualification -- that is what puts it in the tree the walk reads.
	class Holder : public QObject, public editor::IHoldsAssets
	{
	public:
		Holder(QObject* parent, QStringList held) : QObject(parent), m_Held(std::move(held)) {}

		[[nodiscard]] QStringList
		GetHeldOpenPaths() const override
		{
			return m_Held;
		}

	private:
		QStringList m_Held;
	};

	class PluginPanel final : public editor::EditorPanel
	{
	public:
		using EditorPanel::EditorPanel;

		std::vector<std::string>
		GetHeldAssets() const override
		{
			return { "Authored/AI/behavior.bexample" };
		}

		bool
		CanClose() override
		{
			return true;
		}
		void
		SetActive(bool) override
		{}
	};
}

TEST_CASE("A plugin panel participates in held asset protection", "[heldopen][plugins]")
{
	QWidget root;
	static_cast<void>(new PluginPanel(&root));

	CHECK(editor::GetAssetsHeldOpen(&root) == QStringList{ "Authored/AI/behavior.bexample" });
}

// The bug this closes: MainWindow named two of the six panels that hold assets, and the four it
// missed were the ones lighting a viewport from a `.benv` -- which nothing on disk holds, so the
// file could be deleted while four views were still drawing it. Nothing here registers a holder,
// at any depth, which is the property that makes the seventh panel free.
TEST_CASE("A holder nobody wired up is still asked what it has open", "[heldopen]")
{
	QObject root;
	auto*   branch = new QObject(&root);

	static_cast<void>(new Holder(branch, { "/Data/Authored/Environments/studio.benv" }));
	static_cast<void>(new Holder(&root, { "/Data/Authored/Materials/skin.bmaterial" }));

	const QStringList held = editor::GetAssetsHeldOpen(&root);

	CHECK(held.size() == 2);
	CHECK(held.contains("/Data/Authored/Environments/studio.benv"));
	CHECK(held.contains("/Data/Authored/Materials/skin.bmaterial"));
}

TEST_CASE("A root that is itself a holder declares its own", "[heldopen]")
{
	const Holder root(nullptr, { "/Data/Derived/Meshes/tree.bmesh" });

	CHECK(editor::GetAssetsHeldOpen(&root) == QStringList{ "/Data/Derived/Meshes/tree.bmesh" });
}

// The guard must fail open. A window with no panel holding anything -- and a caller with no window
// at all -- has to leave every deletion to be judged on what is on disk.
TEST_CASE("A tree with no holder in it holds nothing", "[heldopen]")
{
	QObject root;
	static_cast<void>(new QObject(&root));

	CHECK(editor::GetAssetsHeldOpen(&root).isEmpty());
	CHECK(editor::GetAssetsHeldOpen(nullptr).isEmpty());
}
