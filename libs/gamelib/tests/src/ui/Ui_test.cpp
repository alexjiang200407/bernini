#include <gamelib/ui/UiRuntime.h>

#include "ui/UiStubRenderer.h"
#include "ui/UiTree.h"

#include <RmlUi/Core.h>
#include <RmlUi/Lua.h>
#include <assetlib/pak.h>
#include <lua.h>

#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

namespace
{
	namespace fs = std::filesystem;

	constexpr std::string_view c_Document = "Authored/UI/menu.rml";
	constexpr std::string_view c_Font     = "Authored/Fonts/Lato-Regular.ttf";

	// The repo's fixture tree, resolved from the runtime output directory like every other test
	// asset; copy_assets stages it there.
	fs::path
	AssetRoot()
	{
		return fs::path("assets") / "Data";
	}

	// A scratch data root holding a copy of the UI fixtures, so a case may also pack it.
	struct Sandbox
	{
		fs::path path;

		explicit Sandbox(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path / "Authored/UI");
			fs::create_directories(path / "Authored/Fonts");

			for (const std::string_view key :
			     { c_Document, std::string_view("Authored/UI/menu.rcss"), c_Font })
			{
				fs::copy_file(AssetRoot() / key, path / key);
			}
		}

		~Sandbox() { fs::remove_all(path); }

		Sandbox(const Sandbox&) = delete;
		Sandbox(Sandbox&&)      = delete;
		Sandbox&
		operator=(const Sandbox&) = delete;
		Sandbox&
		operator=(Sandbox&&) noexcept = delete;
	};

	/**
	 * A runtime over one store, with the stub renderer and the fixture font already loaded.
	 *
	 * One per case and never two at once: RmlUi's lifetime is process-global, which UiRuntime
	 * asserts.
	 */
	struct Fixture
	{
		game::test::UiStubRenderer renderer;
		game::UiRuntime            runtime;
		game::UiContextPtr         context;

		// What the fixture document binds against. Created before any document is loaded, which is
		// when RmlUi resolves a `data-model` attribute.
		int                  pressed = 0;
		float                height  = 8.0f;
		Rml::DataModelHandle model;

		Fixture(const Fixture&) = delete;
		Fixture(Fixture&&)      = delete;
		Fixture&
		operator=(const Fixture&) = delete;
		Fixture&
		operator=(Fixture&&) noexcept = delete;

		explicit Fixture(const assetlib::AssetStore& store) : runtime(store, renderer)
		{
			runtime.LoadFontFace(c_Font);
			context = runtime.CreateContext("test", 800, 600);

			Rml::DataModelConstructor constructor = context->Get().CreateDataModel("menu");
			REQUIRE(constructor);
			REQUIRE(constructor.Bind("pressed", &pressed));
			REQUIRE(constructor.Bind("height", &height));
			model = constructor.GetModelHandle();
		}
	};

	Rml::Element*
	ById(game::UiContext& context, const char* id)
	{
		Rml::Element* root = context.Get().GetRootElement();
		return root != nullptr ? root->GetElementById(id) : nullptr;
	}

	// The centre of an element's border box, which is where a click lands on it.
	Rml::Vector2i
	CentreOf(Rml::Element& element)
	{
		const Rml::Vector2f offset = element.GetAbsoluteOffset(Rml::BoxArea::Border);
		const Rml::Vector2f size   = element.GetBox().GetSize(Rml::BoxArea::Border);

		return Rml::Vector2i(
			static_cast<int>(offset.x + size.x * 0.5f),
			static_cast<int>(offset.y + size.y * 0.5f));
	}
}

TEST_CASE("A UI document loads from the mount, loose and packed", "[ui]")
{
	const Sandbox sandbox("bernini_ui_mount");

	// The archive must not change what a document is: a shipped game reads the same tree the
	// editor did, through a `.bpak` rather than a directory.
	const bool packed = GENERATE(false, true);
	INFO("packed: " << packed);

	if (packed)
	{
		static_cast<void>(assetlib::AssetStore(sandbox.path)
		                      .Pack(assetlib::PackDesc{ sandbox.path / "Data.bpak" }));
	}

	const assetlib::AssetStore store =
		packed ? assetlib::AssetStore(
					 sandbox.path,
					 std::make_shared<assetlib::PakFile>(sandbox.path / "Data.bpak")) :
				 assetlib::AssetStore(sandbox.path);

	Fixture fx(store);

	Rml::ElementDocument* document = fx.context->LoadDocument(c_Document);
	REQUIRE(document != nullptr);
	document->Show();
	fx.context->Get().Update();

	// The stylesheet was reached through the document's own <link>, which is the half a packed read
	// could break on its own.
	Rml::Element* panel = ById(*fx.context, "panel");
	REQUIRE(panel != nullptr);
	CHECK(panel->GetBox().GetSize(Rml::BoxArea::Border).x == Catch::Approx(240.0f));

	INFO(game::test::DumpTree(fx.context->Get()));
	CHECK_THAT(
		game::test::DumpTree(fx.context->Get()),
		// (80,80), not (48,48): the button's absolute position resolves against the panel, which is
		// the nearest positioned ancestor -- the tree is what says so at a glance.
		Catch::Matchers::ContainsSubstring("div #play .button  (80,80 120x32)"));
}

TEST_CASE("A missing document is an error rather than a null nobody checks", "[ui]")
{
	const Sandbox              sandbox("bernini_ui_missing");
	const assetlib::AssetStore store(sandbox.path);

	Fixture fx(store);

	CHECK_THROWS_AS(fx.context->LoadDocument("Authored/UI/absent.rml"), std::runtime_error);
	CHECK_THROWS_AS(fx.runtime.LoadFontFace("Authored/Fonts/absent.ttf"), std::runtime_error);
}

TEST_CASE("A data-model write moves the element bound to it", "[ui]")
{
	const Sandbox              sandbox("bernini_ui_model");
	const assetlib::AssetStore store(sandbox.path);

	Fixture fx(store);

	Rml::ElementDocument* document = fx.context->LoadDocument(c_Document);
	REQUIRE(document != nullptr);
	document->Show();
	fx.context->Get().Update();

	Rml::Element* readout = ById(*fx.context, "readout");
	REQUIRE(readout != nullptr);
	CHECK(readout->GetBox().GetSize(Rml::BoxArea::Border).y == Catch::Approx(8.0f));

	// The write alone changes nothing: Update is what re-evaluates the binding and re-lays out.
	fx.height = 40.0f;
	fx.model.DirtyVariable("height");
	fx.context->Get().Update();

	CHECK(readout->GetBox().GetSize(Rml::BoxArea::Border).y == Catch::Approx(40.0f));
}

TEST_CASE("A click dispatched on an element by id changes the model", "[ui]")
{
	const Sandbox              sandbox("bernini_ui_dispatch");
	const assetlib::AssetStore store(sandbox.path);

	Fixture fx(store);

	Rml::ElementDocument* document = fx.context->LoadDocument(c_Document);
	REQUIRE(document != nullptr);
	document->Show();
	fx.context->Get().Update();

	Rml::Element* play = ById(*fx.context, "play");
	REQUIRE(play != nullptr);

	// No pointer involved: the element is reached by id and the event dispatched onto it, which is
	// how a test drives a control whose position it does not care about.
	play->DispatchEvent(Rml::EventId::Click, Rml::Dictionary());
	fx.context->Get().Update();

	CHECK(fx.pressed == 1);
}

TEST_CASE("A click through the input door fires the document's binding", "[ui]")
{
	const Sandbox              sandbox("bernini_ui_click");
	const assetlib::AssetStore store(sandbox.path);

	Fixture fx(store);

	Rml::ElementDocument* document = fx.context->LoadDocument(c_Document);
	REQUIRE(document != nullptr);
	document->Show();
	fx.context->Get().Update();

	Rml::Element* play = ById(*fx.context, "play");
	REQUIRE(play != nullptr);

	// RmlUi's own vocabulary, called by the client: gamelib declares no event type of its own
	// (ADR-10), and this is the whole input door the example's SDL translation drives.
	const Rml::Vector2i centre = CentreOf(*play);
	fx.context->Get().ProcessMouseMove(centre.x, centre.y, 0);

	const bool overButton = fx.context->Get().ProcessMouseButtonDown(0, 0);
	fx.context->Get().ProcessMouseButtonUp(0, 0);
	fx.context->Get().Update();

	CHECK(fx.pressed == 1);

	// False means the UI consumed it: what tells a game a click landed on a control rather than
	// on the world.
	CHECK_FALSE(overButton);

	// Away from every element, the click is the game's.
	fx.context->Get().ProcessMouseMove(700, 500, 0);
	const bool overNothing = fx.context->Get().ProcessMouseButtonDown(0, 0);
	fx.context->Get().ProcessMouseButtonUp(0, 0);
	fx.context->Get().Update();

	CHECK(overNothing);
	CHECK(fx.pressed == 1);
}

TEST_CASE("JoinPath resolves a reference into a mount key, and refuses an escape", "[ui]")
{
	const Sandbox              sandbox("bernini_ui_joinpath");
	const assetlib::AssetStore store(sandbox.path);

	Fixture fx(store);

	Rml::SystemInterface* system = Rml::GetSystemInterface();
	REQUIRE(system != nullptr);

	const auto join = [&](const char* document, const char* reference) {
		Rml::String out;
		system->JoinPath(out, document, reference);
		return std::string(out);
	};

	CHECK(join("Authored/UI/menu.rml", "menu.rcss") == "Authored/UI/menu.rcss");
	CHECK(
		join("Authored/UI/menu.rml", "../Fonts/Lato-Regular.ttf") ==
		"Authored/Fonts/Lato-Regular.ttf");

	// Out of the data root, and an absolute host path: both empty, which fails the open that
	// follows rather than reading something outside the project (ADR-8).
	CHECK(join("Authored/UI/menu.rml", "../../../etc/passwd").empty());
	CHECK(join("Authored/UI/menu.rml", "/etc/passwd").empty());

	// A scheme names something the game registered rather than a file, so it passes through for
	// the renderer to resolve (ADR-14).
	CHECK(join("Authored/UI/menu.rml", "target://preview") == "target://preview");
}

TEST_CASE("The UI clock is the one the client advances", "[ui]")
{
	const Sandbox              sandbox("bernini_ui_clock");
	const assetlib::AssetStore store(sandbox.path);

	Fixture fx(store);

	CHECK(fx.runtime.GetElapsedTime() == Catch::Approx(0.0));

	fx.runtime.AdvanceTime(1.5);
	CHECK(fx.runtime.GetElapsedTime() == Catch::Approx(1.5));

	// Through RmlUi's own accessor: what a transition reads is the interface that was installed,
	// not a second clock beside it (ADR-9). Nothing here consults a wall clock, so this is exact.
	Rml::SystemInterface* system = Rml::GetSystemInterface();
	REQUIRE(system != nullptr);
	CHECK(system->GetElapsedTime() == Catch::Approx(1.5));
}

TEST_CASE("The UI runtime is one per process", "[ui]")
{
	const Sandbox              sandbox("bernini_ui_single");
	const assetlib::AssetStore store(sandbox.path);

	Fixture fx(store);

	game::test::UiStubRenderer second;
	CHECK_THROWS_AS(game::UiRuntime(store, second), std::runtime_error);

	// And the refusal did not tear the live one down.
	CHECK(fx.context->LoadDocument(c_Document) != nullptr);
}

// A document that scripts itself: the plan's Lua non-goal, amended in this task's PR. What is
// deferred is the engine-wide VM, not the plugin.
TEST_CASE("A document scripts itself when scripting is on", "[ui][lua]")
{
	const Sandbox sandbox("bernini_ui_lua");
	fs::copy_file(
		AssetRoot() / "Authored/UI/scripted.rml",
		sandbox.path / "Authored/UI/scripted.rml");

	const assetlib::AssetStore store(sandbox.path);

	game::test::UiStubRenderer renderer;
	auto                       options = game::UiRuntimeOptions();
	options.scripting                  = true;

	game::UiRuntime runtime(store, renderer, options);
	REQUIRE(runtime.ScriptingEnabled());
	runtime.LoadFontFace(c_Font);

	// The plugin is up and holds a state, which is what "scripting on" means underneath.
	CHECK(Rml::Lua::Interpreter::GetLuaState() != nullptr);

	game::UiContextPtr context = runtime.CreateContext("lua", 800, 600);

	Rml::ElementDocument* document = context->LoadDocument("Authored/UI/scripted.rml");
	REQUIRE(document != nullptr);
	document->Show();
	context->Get().Update();

	Rml::Element* readout = context->Get().GetRootElement()->GetElementById("readout");
	REQUIRE(readout != nullptr);
	CHECK(readout->GetBox().GetSize(Rml::BoxArea::Border).y == Catch::Approx(8.0f));

	// The inline onclick calls a function the document's own <script> defined, which then reaches
	// back into the document -- none of it known to this binary.
	Rml::Element* play = context->Get().GetRootElement()->GetElementById("play");
	REQUIRE(play != nullptr);
	play->DispatchEvent(Rml::EventId::Click, Rml::Dictionary());
	context->Get().Update();

	CHECK(readout->GetBox().GetSize(Rml::BoxArea::Border).y == Catch::Approx(24.0f));

	// And the script's state persists across events, which is what makes it a document's scope
	// rather than a handler.
	play->DispatchEvent(Rml::EventId::Click, Rml::Dictionary());
	context->Get().Update();

	CHECK(readout->GetBox().GetSize(Rml::BoxArea::Border).y == Catch::Approx(40.0f));
}

TEST_CASE("Scripting is off unless it is asked for", "[ui][lua]")
{
	const Sandbox sandbox("bernini_ui_nolua");
	fs::copy_file(
		AssetRoot() / "Authored/UI/scripted.rml",
		sandbox.path / "Authored/UI/scripted.rml");

	const assetlib::AssetStore store(sandbox.path);

	Fixture fx(store);
	CHECK_FALSE(fx.runtime.ScriptingEnabled());

	// No plugin, so no state: the absence is observed rather than inferred from the flag.
	CHECK(Rml::Lua::Interpreter::GetLuaState() == nullptr);

	// The document still loads -- its script is inert markup, not a parse error -- and the inline
	// handler does nothing, so a game that binds from C++ never creates a VM.
	Rml::ElementDocument* document = fx.context->LoadDocument("Authored/UI/scripted.rml");
	REQUIRE(document != nullptr);
	document->Show();
	fx.context->Get().Update();

	Rml::Element* readout = ById(*fx.context, "readout");
	REQUIRE(readout != nullptr);

	Rml::Element* play = ById(*fx.context, "play");
	REQUIRE(play != nullptr);
	play->DispatchEvent(Rml::EventId::Click, Rml::Dictionary());
	fx.context->Get().Update();

	CHECK(readout->GetBox().GetSize(Rml::BoxArea::Border).y == Catch::Approx(8.0f));
}

// The seam the engine-wide VM arrives through: RmlUi adds its bindings to a state the caller owns
// and must not close it. Until that VM exists this is the only thing holding the promise.
TEST_CASE("A caller's Lua state is used and not closed", "[ui][lua]")
{
	const Sandbox sandbox("bernini_ui_lua_state");
	fs::copy_file(
		AssetRoot() / "Authored/UI/scripted.rml",
		sandbox.path / "Authored/UI/scripted.rml");

	const assetlib::AssetStore store(sandbox.path);

	lua_State* state = luaL_newstate();
	REQUIRE(state != nullptr);

	// The plugin opens the standard libraries only for a state it made itself, so a caller's state
	// arrives bare and a document's first string concatenation would panic the process.
	luaL_openlibs(state);

	{
		game::test::UiStubRenderer renderer;
		auto                       options = game::UiRuntimeOptions();
		options.scripting                  = true;
		options.luaState                   = state;

		game::UiRuntime runtime(store, renderer, options);
		runtime.LoadFontFace(c_Font);

		// Ours, not one the plugin made beside it.
		CHECK(Rml::Lua::Interpreter::GetLuaState() == state);

		game::UiContextPtr    context  = runtime.CreateContext("lua_state", 800, 600);
		Rml::ElementDocument* document = context->LoadDocument("Authored/UI/scripted.rml");
		REQUIRE(document != nullptr);
		document->Show();
		context->Get().Update();
	}

	// The runtime is gone and the state is not: still usable, and ours to close. A plugin that
	// closed it would make this a use-after-free rather than a failed check.
	CHECK(Rml::Lua::Interpreter::GetLuaState() == nullptr);
	lua_pushinteger(state, 42);
	CHECK(lua_tointeger(state, -1) == 42);

	lua_close(state);
}
