# T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
# Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
# SPDX-License-Identifier: GPL-3.0-or-later
# Made by N1k0droid. Like T-Deck SondeTracker? A star on
# https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

Import("env")
import os
import re

# Merged image (bootloader + partitions + boot_app0 + app) for Launcher / web flasher.


def fw_version():
    """The version is written once, in src/main.cpp (#define FW_VERSION "x.y", #define FW_BETA n)."""
    with open(os.path.join(env.subst("$PROJECT_DIR"), "src", "main.cpp"), encoding="utf-8") as f:
        src = f.read()
    m = re.search(r'#define FW_VERSION "([^"]+)"', src)
    b = re.search(r'#define FW_BETA (\d+)', src)
    if not m or not b:
        raise RuntimeError("FW_VERSION or FW_BETA not found in src/main.cpp")
    return m.group(1), int(b.group(1))


FW_VERSION, FW_BETA = fw_version()
# Release name, the same for the tag, the release title and the file (user's rule, 24/11/2026): the 0.x
# versions are betas with a build number, "v-0.93-beta2", a new number for every firmware handed out, never
# overwritten; from 1.0.0 on "v-1.0.0". Bench builds pointed at the PC mock end in "-test".
RELEASE_NAME = "v-%s%s" % (FW_VERSION, "-beta%d" % FW_BETA if FW_BETA else "")
_flags = os.environ.get("PLATFORMIO_BUILD_FLAGS", "")
if _flags.strip():  # any extra flag (mock server, FIELD_DEBUG, SH_DEV...) is a test build, never the release name
    RELEASE_NAME += "-test"


def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    app = os.path.join(build_dir, "firmware.bin")
    boot = os.path.join(build_dir, "bootloader.bin")
    parts = os.path.join(build_dir, "partitions.bin")
    fw_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    boot_app0 = os.path.join(fw_dir, "tools", "partitions", "boot_app0.bin")
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "release")
    os.makedirs(out_dir, exist_ok=True)
    # another build may give its own file name (custom_fw_name in its env), same release name
    name = env.GetProjectOption("custom_fw_name", "t-deck-sonde-tracker")
    out = os.path.join(out_dir, "%s-%s.bin" % (name, RELEASE_NAME))
    cmd = [
        env.subst("$PYTHONEXE"),
        os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py"),
        "--chip", "esp32s3", "merge_bin", "-o", out,
        "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "16MB",
        "0x0", boot, "0x8000", parts, "0xe000", boot_app0, "0x10000", app,
    ]
    env.Execute(" ".join('"%s"' % c for c in cmd))
    print("Merged image: " + out)


env.AddPostAction("$BUILD_DIR/firmware.bin", merge_bin)
