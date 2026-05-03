import datetime
import json
import os

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
print(f"  Build version: {version}")

env.Append(CPPDEFINES=[
    ("FW_VERSION", env.StringifyMacro(version))
])
