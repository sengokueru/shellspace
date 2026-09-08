"""Generate missing assets and verify them, without deleting legacy/user files."""
from pathlib import Path
import subprocess
import sys
from ir_manifest import EXPECTED


def prepare(directory):
    directory = Path(directory).resolve()
    scripts = Path(__file__).resolve().parent
    present = {p.name for p in directory.glob("*.wav")}
    extra = present - EXPECTED
    if extra:
        raise ValueError("Move unexpected WAVs to a backup folder before building: "
                         + ", ".join(sorted(extra)))
    if not EXPECTED <= present:
        subprocess.run([sys.executable, str(scripts / "make_ir.py"), str(directory)], check=True)
    subprocess.run([sys.executable, str(scripts / "verify_ir.py"), str(directory)], check=True)


if __name__ == "__main__":
    prepare(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent)
