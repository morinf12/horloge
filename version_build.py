import datetime
import json
import os
import subprocess

Import("env")

# Build number tracker file
TRACK_FILE = os.path.join(env.subst("$PROJECT_DIR"), ".build_number")

today = datetime.date.today().strftime("%y%m%d")

# Load or initialize tracking
build_num = 1
if os.path.exists(TRACK_FILE):
    try:
        with open(TRACK_FILE, "r") as f:
            data = json.load(f)
        if data.get("date") == today:
            build_num = data.get("num", 0) + 1
    except (json.JSONDecodeError, KeyError):
        pass

# Save updated tracking
with open(TRACK_FILE, "w") as f:
    json.dump({"date": today, "num": build_num}, f)

version = f"V{today}.{build_num:02d}"

# Get latest git tag for release version
try:
    git_tag = subprocess.check_output(
        ["git", "describe", "--tags", "--abbrev=0"],
        cwd=env.subst("$PROJECT_DIR"),
        stderr=subprocess.DEVNULL
    ).decode().strip()
except Exception:
    git_tag = "unknown"

print(f"  Build version: {version} (release: {git_tag})")

env.Append(CPPDEFINES=[
    ("FW_VERSION", env.StringifyMacro(version)),
    ("FW_RELEASE", env.StringifyMacro(git_tag))
])
