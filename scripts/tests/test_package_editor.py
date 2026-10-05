"""What the editor package carries out of a runtime directory, and the config it ships with.

The runtime directory is a developer's as well as the editor's: their log, their config, the build
tools staged beside the editor. Each case pins one file a tester must or must not receive.
"""

import json
import os

import package_editor


def touch(root, rel, executable=False):
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("x")
    if executable:
        os.chmod(path, 0o755)
    return path


def test_the_developers_config_and_log_stay_behind(tmp_path):
    assert not package_editor.ships(touch(tmp_path, "config.json"), tmp_path)
    assert not package_editor.ships(touch(tmp_path, "editor.log"), tmp_path)
    assert not package_editor.ships(touch(tmp_path, "shadercache/pipelines.bin"), tmp_path)


def test_a_build_tool_beside_the_editor_stays_behind(tmp_path):
    assert not package_editor.ships(touch(tmp_path, "bgpu_idlgen.exe"), tmp_path)
    assert package_editor.ships(touch(tmp_path, "editor.exe"), tmp_path)


def test_the_translation_catalogs_ship_although_a_csv_elsewhere_does_not(tmp_path):
    assert package_editor.ships(touch(tmp_path, "localization/editor.main.csv"), tmp_path)
    assert package_editor.ships(touch(tmp_path, "plugins/bernini.default/localization/a.csv"), tmp_path)
    assert not package_editor.ships(touch(tmp_path, "gpu_timings.csv"), tmp_path)


def test_the_shipped_config_opens_a_device_on_a_machine_without_the_debug_layer(tmp_path):
    package_editor.write_config(tmp_path)
    config = json.loads((tmp_path / "config.json").read_text())
    assert config["graphics"]["enableDebugLayer"] is False
    assert config["graphics"]["enablePixDebug"] is False
    assert config["startupProject"] == ""
