"""Every string the editor shows goes through the resolver, and its English is the CSV's.

The two tree checks at the bottom are the gate; the rest pin what the scanner in
`util.editor_localization` does and does not count, so a change to it that silently stops finding
things fails here rather than passing the gate by finding nothing.
"""

import util.editor_localization as el


def test_a_literal_handed_to_a_text_call_is_found():
    found = el.raw_literals('label->setText("Save");\nnew QLabel(QStringLiteral("Name"), this);\n')
    assert [(line, call) for line, call, _ in found] == [(1, "setText"), (2, "QLabel")]


def test_a_call_spanning_lines_is_read_whole():
    source = 'QMessageBox::warning(\n\tthis,\n\ttitle,\n\t"Could not save");\n'
    assert [literal for _, _, literal in el.raw_literals(source)] == ['"Could not save"']


def test_a_localized_triple_and_a_letterless_literal_pass():
    source = (
        'setText(Localize("editor.main.save", { name, f(x) }, "Save {0}"));\n'
        'setText(Localize(m_Host.GetLanguageResolver(), "bernini.material.save", "Save"));\n'
        'setText(QStringLiteral("%1 / %2").arg(a, b));\n'
        'setText(name);\n')
    assert el.raw_literals(source) == []


def test_a_similar_name_and_a_comment_are_not_text_calls():
    source = (
        'SetTitle({ "editor.main", "title", "Title" });\n'
        'setObjectName("sample.changedAsset");\n'
        '// label->setText("Save");\n')
    assert el.raw_literals(source) == []


def test_a_localize_call_is_read_with_or_without_a_resolver():
    source = (
        'Localize("editor.main.title", "Bernini Editor");\n'
        'Localize(host.GetLanguageResolver(), "sample.editor.overview", { n }, "Project {0}");\n'
        'setText("Raw", Localize("editor.main.title", "Bernini Editor"));\n')
    assert el.triples(source) == [
        (1, "editor.main", "title", "Bernini Editor"),
        (2, "sample.editor", "overview", "Project {0}"),
        (3, "editor.main", "title", "Bernini Editor")]
    assert [literal for _, _, literal in el.raw_literals(source)] == ['"Raw"']


def test_a_lettered_literal_passed_as_an_argument_is_still_raw():
    source = 'setText(Localize("editor.main.save", { "Untitled" }, "Save {0}"));\n'
    assert [literal for _, _, literal in el.raw_literals(source)] == ['"Untitled"']


def test_a_localized_error_is_read_like_a_localize_call():
    source = 'throw editor::LocalizedError("editor.plugins.missing", { path }, "Missing: {0}");\n'
    assert el.triples(source) == [(1, "editor.plugins", "missing", "Missing: {0}")]


def test_a_split_fallback_is_concatenated_and_unescaped():
    source = '{ "editor.main",\n  "could_not_start",\n  "The editor could not start:\\n\\n%1"\n  " See log." }'
    assert el.triples(source) == [
        (1, "editor.main", "could_not_start", "The editor could not start:\n\n%1 See log.")]


def test_a_fallback_the_csv_disagrees_with_is_reported(tmp_path):
    (tmp_path / "src").mkdir()
    (tmp_path / "loc").mkdir()
    (tmp_path / "src" / "a.cpp").write_text(
        'X({ "fixture.editor", "save", "Save" });\n'
        'X({ "fixture.editor", "open", "Open" });\n'
        'X({ "missing.editor", "close", "Close" });\n', encoding="utf-8")
    (tmp_path / "loc" / "fixture.editor.csv").write_text(
        "key,en\nsave,Save file\nunused,Nobody\n", encoding="utf-8")
    findings = el.catalog_findings(["src"], ["loc"], str(tmp_path))
    assert len(findings) == 4
    assert "falls back to 'Save'" in findings[0]
    assert "open has no row" in findings[1]
    assert "missing.editor has no localization" in findings[2]
    assert "unused is used by no source" in findings[3]


def test_the_covered_editor_sources_hand_qt_no_raw_literal():
    findings = el.raw_literal_findings()
    assert findings == [], "route these through the resolver:\n" + "\n".join(findings)


def test_every_fallback_matches_its_catalog():
    findings = el.catalog_findings()
    assert findings == [], "\n".join(findings)
