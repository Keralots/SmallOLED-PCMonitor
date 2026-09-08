"""Data directory + one-time migration off the shared legacy config folder.

Every companion forked from this v4 code claimed %APPDATA%\\PCStatsMonitor
(~/.config/PCStatsMonitor), so two of them fought over one config file. The
legacy folder is copied from, never written to or deleted: another product may
still read it, and a rollback has to find its settings where it left them.
"""

import json
import os
import shutil
import sys
import time

APP_TAG = "smalloled"

CONFIG_NAME = "monitor_config.json"
BREADCRUMB_NAME = "MIGRATED-FROM.txt"

# Keys only one product writes, and both write on every save.
_OURS_ONLY = ("sensor_source",)
_THEIRS_ONLY = ("send_pc_stats",)

LEGACY_DIR_NAME = "PCStatsMonitor"


def legacy_data_dir():
    if sys.platform == "win32":
        base = os.environ.get("APPDATA")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    if not base:
        return None
    return os.path.join(base, LEGACY_DIR_NAME)


def _same_dir(a, b):
    if not a or not b:
        return False
    return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


def config_owner(config):
    """'ours', 'theirs', 'mixed' or 'unknown'. Configs predating the stamp fall
    back to the product-specific keys."""
    if not isinstance(config, dict):
        return "unknown"
    stamped = config.get("app")
    if stamped == APP_TAG:
        return "ours"
    if isinstance(stamped, str) and stamped:
        return "theirs"
    ours = any(k in config for k in _OURS_ONLY)
    theirs = any(k in config for k in _THEIRS_ONLY)
    if ours and theirs:
        return "mixed"
    if ours:
        return "ours"
    if theirs:
        return "theirs"
    return "unknown"


def _copy_aside(src, dst):
    try:
        shutil.copy2(src, dst)
        return True
    except Exception:
        return False


def _write_atomic(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def migrate_legacy_config(data_dir, legacy_dir=None, app_tag=APP_TAG):
    """Copy the legacy config into data_dir once. Returns a status line to log,
    or "" when there was nothing to do.

    Call only from the process holding the single-instance lock.
    """
    if legacy_dir is None:
        legacy_dir = legacy_data_dir()
    if not data_dir or not legacy_dir or _same_dir(data_dir, legacy_dir):
        return ""

    # isfile, not exists: anything else on that name would skip the migration
    # and leave the user with no settings at all.
    new_cfg = os.path.join(data_dir, CONFIG_NAME)
    if os.path.isfile(new_cfg):
        return ""

    old_cfg = os.path.join(legacy_dir, CONFIG_NAME)
    if not os.path.isfile(old_cfg):
        return ""

    try:
        with open(old_cfg, "r", encoding="utf-8") as f:
            raw = f.read()
    except Exception as e:
        return ("Could not read the previous configuration at %s (%s). "
                "Starting with defaults; the old file was left untouched." % (old_cfg, e))

    try:
        config = json.loads(raw)
        if not isinstance(config, dict):
            raise ValueError("top level is %s, not an object" % type(config).__name__)
    except Exception as e:
        aside = os.path.join(data_dir, CONFIG_NAME + ".unreadable")
        _copy_aside(old_cfg, aside)
        return ("The previous configuration at %s is not valid JSON (%s). "
                "A copy was kept as %s and defaults are in use." % (old_cfg, e, aside))

    owner = config_owner(config)
    config["app"] = app_tag

    try:
        _write_atomic(new_cfg, json.dumps(config, indent=2))
    except Exception as e:
        return ("Could not write the migrated configuration to %s (%s). "
                "Settings from %s are in use for this session only."
                % (new_cfg, e, old_cfg))

    copied_backups = 0
    try:
        for name in sorted(os.listdir(legacy_dir)):
            if name.endswith("_backup.json") and name.startswith("monitor_config"):
                if _copy_aside(os.path.join(legacy_dir, name),
                               os.path.join(data_dir, name)):
                    copied_backups += 1
    except Exception:
        pass

    try:
        _write_atomic(os.path.join(data_dir, BREADCRUMB_NAME),
                      "Configuration copied from:\n  %s\non %s\n\n"
                      "The original was left in place and is no longer read or\n"
                      "written by this app. Delete it only once you are sure no\n"
                      "other application still uses it.\n"
                      % (old_cfg, time.strftime("%Y-%m-%d %H:%M:%S")))
    except Exception:
        pass

    msg = "Configuration copied from %s to %s" % (old_cfg, new_cfg)
    if copied_backups:
        msg += " (+%d backup file(s))" % copied_backups
    msg += ". The original was left untouched."
    if owner == "theirs":
        msg += ("\n  NOTE: that configuration was last saved by a different companion "
                "app sharing the old folder, so its device address and metric list "
                "may belong to the other device. Check them in the settings window.")
    elif owner == "mixed":
        msg += ("\n  NOTE: the old folder was shared with another companion app and "
                "both had written to it, so the device address and metric list are "
                "whichever app saved last. Check them in the settings window.")
    return msg
