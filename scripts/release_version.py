"""Use the CMake project version for every release package."""
import os
from pathlib import Path
import re


def read_version(source, tag=""):
    matches = re.findall(r"(?m)^\s*project\(ShellSpace\s+VERSION\s+(\d+\.\d+\.\d+)\s*\)", source)
    if len(matches) != 1:
        raise ValueError("Expected exactly one ShellSpace project version")
    version = matches[0]
    if tag and tag != "v" + version:
        raise ValueError(f"Tag {tag} does not match CMake version {version}")
    return version


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    tag = os.environ.get("GITHUB_REF_NAME", "") if os.environ.get("GITHUB_REF_TYPE") == "tag" else ""
    version = read_version((root / "ShellSpace/CMakeLists.txt").read_text(encoding="utf-8"), tag)
    if os.environ.get("GITHUB_ENV"):
        with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as env_file:
            env_file.write(f"VERSION={version}\n")
    print(version)
