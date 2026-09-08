"""Tests for the one-time migration off the shared legacy config folder.

The thing being protected here is a real user's settings, so the cases that
matter most are the ones where the copy must NOT happen or must NOT be trusted:
an already-migrated folder, an unreadable file, a config the other product wrote.

Run: python test_app_paths.py
"""

import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import app_paths


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f)


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="smalloled-paths-")
        self.new = os.path.join(self.root, "SmallOLED-Companion")
        self.old = os.path.join(self.root, "PCStatsMonitor")
        os.makedirs(self.new, exist_ok=True)

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def new_cfg(self):
        with open(os.path.join(self.new, app_paths.CONFIG_NAME), encoding="utf-8") as f:
            return json.load(f)

    # -- nothing to do ------------------------------------------------------
    def test_fresh_install_copies_nothing(self):
        self.assertEqual(app_paths.migrate_legacy_config(self.new, self.old), "")
        self.assertFalse(os.path.exists(os.path.join(self.new, app_paths.CONFIG_NAME)))

    def test_existing_new_config_is_never_overwritten(self):
        write_json(os.path.join(self.new, app_paths.CONFIG_NAME), {"esp32_ip": "10.0.0.1"})
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME), {"esp32_ip": "10.0.0.2"})
        self.assertEqual(app_paths.migrate_legacy_config(self.new, self.old), "")
        self.assertEqual(self.new_cfg()["esp32_ip"], "10.0.0.1")

    def test_same_dir_is_a_noop(self):
        write_json(os.path.join(self.new, app_paths.CONFIG_NAME), {"esp32_ip": "10.0.0.1"})
        self.assertEqual(app_paths.migrate_legacy_config(self.new, self.new), "")

    def test_missing_legacy_dir_is_a_noop(self):
        self.assertEqual(
            app_paths.migrate_legacy_config(self.new, os.path.join(self.root, "nope")), "")

    # -- the happy path -----------------------------------------------------
    def test_copies_and_stamps_and_leaves_original(self):
        original = {"esp32_ip": "192.168.0.12", "sensor_source": "auto",
                    "metrics": [{"id": 1}]}
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME), original)

        note = app_paths.migrate_legacy_config(self.new, self.old)

        self.assertIn("copied", note.lower())
        migrated = self.new_cfg()
        self.assertEqual(migrated["esp32_ip"], "192.168.0.12")
        self.assertEqual(migrated["metrics"], [{"id": 1}])
        self.assertEqual(migrated["app"], app_paths.APP_TAG)
        # The legacy file is untouched, including its lack of a stamp.
        with open(os.path.join(self.old, app_paths.CONFIG_NAME), encoding="utf-8") as f:
            self.assertNotIn("app", json.load(f))
        self.assertTrue(os.path.exists(os.path.join(self.new, app_paths.BREADCRUMB_NAME)))

    def test_second_run_does_not_recopy(self):
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME), {"esp32_ip": "1.2.3.4"})
        self.assertNotEqual(app_paths.migrate_legacy_config(self.new, self.old), "")
        # A later edit in the new folder must survive the next launch.
        cfg = self.new_cfg()
        cfg["esp32_ip"] = "5.6.7.8"
        write_json(os.path.join(self.new, app_paths.CONFIG_NAME), cfg)
        self.assertEqual(app_paths.migrate_legacy_config(self.new, self.old), "")
        self.assertEqual(self.new_cfg()["esp32_ip"], "5.6.7.8")

    def test_backups_come_along(self):
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME), {"esp32_ip": "1.2.3.4"})
        write_json(os.path.join(self.old, "monitor_config_v2.0_backup.json"), {"old": True})
        note = app_paths.migrate_legacy_config(self.new, self.old)
        self.assertIn("backup", note)
        self.assertTrue(os.path.exists(
            os.path.join(self.new, "monitor_config_v2.0_backup.json")))

    # -- ownership ----------------------------------------------------------
    def test_other_products_config_is_adopted_but_flagged(self):
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME),
                   {"esp32_ip": "192.168.0.14", "send_pc_stats": True})
        note = app_paths.migrate_legacy_config(self.new, self.old)
        self.assertIn("NOTE:", note)
        self.assertEqual(self.new_cfg()["esp32_ip"], "192.168.0.14")

    def test_config_written_by_both_products_is_flagged(self):
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME),
                   {"esp32_ip": "192.168.0.12", "send_pc_stats": True,
                    "sensor_source": "auto"})
        note = app_paths.migrate_legacy_config(self.new, self.old)
        self.assertIn("NOTE:", note)
        self.assertIn("both", note)

    def test_our_own_config_migrates_quietly(self):
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME),
                   {"esp32_ip": "192.168.0.12", "sensor_source": "auto"})
        note = app_paths.migrate_legacy_config(self.new, self.old)
        self.assertNotIn("NOTE:", note)

    def test_owner_detection(self):
        self.assertEqual(app_paths.config_owner({"app": "smalloled"}), "ours")
        self.assertEqual(app_paths.config_owner({"app": "pixelclock"}), "theirs")
        self.assertEqual(app_paths.config_owner({"sensor_source": "auto"}), "ours")
        self.assertEqual(app_paths.config_owner({"send_pc_stats": True}), "theirs")
        self.assertEqual(app_paths.config_owner(
            {"send_pc_stats": True, "sensor_source": "auto"}), "mixed")
        self.assertEqual(app_paths.config_owner({"esp32_ip": "1.2.3.4"}), "unknown")
        self.assertEqual(app_paths.config_owner(None), "unknown")

    # -- damaged input ------------------------------------------------------
    def test_corrupt_legacy_config_is_kept_aside_not_adopted(self):
        os.makedirs(self.old, exist_ok=True)
        with open(os.path.join(self.old, app_paths.CONFIG_NAME), "w", encoding="utf-8") as f:
            f.write("{ this is not json")
        note = app_paths.migrate_legacy_config(self.new, self.old)
        self.assertIn("not valid JSON", note)
        self.assertFalse(os.path.exists(os.path.join(self.new, app_paths.CONFIG_NAME)))
        self.assertTrue(os.path.exists(
            os.path.join(self.new, app_paths.CONFIG_NAME + ".unreadable")))

    def test_json_that_is_not_an_object_is_rejected(self):
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME), ["a", "list"])
        note = app_paths.migrate_legacy_config(self.new, self.old)
        self.assertIn("not valid JSON", note)
        self.assertFalse(os.path.exists(os.path.join(self.new, app_paths.CONFIG_NAME)))

    def test_unwritable_target_reports_and_does_not_crash(self):
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME), {"esp32_ip": "1.2.3.4"})
        # A directory where the config file should go: the write must fail.
        os.makedirs(os.path.join(self.new, app_paths.CONFIG_NAME), exist_ok=True)
        note = app_paths.migrate_legacy_config(self.new, self.old)
        self.assertIn("Could not", note)

    def test_non_ascii_paths(self):
        root = tempfile.mkdtemp(prefix="smalloled-żółw-")
        try:
            new = os.path.join(root, "SmallOLED-Companion")
            old = os.path.join(root, "PCStatsMonitor")
            os.makedirs(new, exist_ok=True)
            write_json(os.path.join(old, app_paths.CONFIG_NAME),
                       {"esp32_ip": "1.2.3.4", "label": "żółw"})
            self.assertNotEqual(app_paths.migrate_legacy_config(new, old), "")
            with open(os.path.join(new, app_paths.CONFIG_NAME), encoding="utf-8") as f:
                self.assertEqual(json.load(f)["label"], "żółw")
        finally:
            shutil.rmtree(root, ignore_errors=True)

    def test_no_temp_file_left_behind(self):
        write_json(os.path.join(self.old, app_paths.CONFIG_NAME), {"esp32_ip": "1.2.3.4"})
        app_paths.migrate_legacy_config(self.new, self.old)
        self.assertFalse(any(n.endswith(".tmp") for n in os.listdir(self.new)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
