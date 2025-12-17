import sys
import re
from pathlib import Path

# File extensions belonging to exported PowerBuilder source objects.
# *.sr* → general source files (SRD, SRW, SRU…)
# *.srd → DataWindow source
# *.srs → Structure source
EXTS = ["*.sr*", "*.srd", "*.srs"]

def collect_files(root: Path) -> list[Path]:
    """
    Recursively collects all PowerBuilder source files under the given root directory.
    Matches against the file extensions defined in EXTS.
    Returns a sorted list of file paths, filtering out non-files.
    """
    files = []
    for pattern in EXTS:
        files.extend(root.rglob(pattern))
    return sorted(p for p in files if p.is_file())

def read_source(f: Path) -> str:
    """
    Reads a PowerBuilder source file while handling multiple possible encodings.
    Behavior:
      1) Detect UTF-16 (LE or BE) via BOM.
      2) Attempt UTF-8 decoding.
      3) Fallback to Windows-1252 for legacy ANSI files.
    After decoding:
      - Removes null bytes.
      - Normalizes all newline formats to '\n'.
    Returns the decoded and cleaned text.
    """
    raw = f.read_bytes()

    # UTF-16 BOM detection
    if raw.startswith(b"\xff\xfe"):
        text = raw.decode("utf-16-le", errors="ignore")
    elif raw.startswith(b"\xfe\xff"):
        text = raw.decode("utf-16-be", errors="ignore")
    else:
        # Prefer UTF-8, fallback to CP1252
        try:
            text = raw.decode("utf-8")
        except:
            text = raw.decode("windows-1252", errors="ignore")

    # Remove nulls and force normalized newlines
    text = text.replace("\x00", "")
    text = text.replace("\r\n", "\n").replace("\r", "\n")

    return text

def write_combined_file(files: list[Path], output_file: Path):
    """
    Writes a unified text file containing all collected source files.
    Each file is prefixed with a header:
        --- FILE: <path> ---
    Output is always encoded as UTF-8.
    Directory structure is created if missing.
    """
    output_file.parent.mkdir(parents=True, exist_ok=True)

    with output_file.open("w", encoding="utf-8") as out:
        for f in files:
            text = read_source(f)
            out.write(f"\n\n--- FILE: {f} ---\n\n")
            out.write(text)
            out.write("\n")

def process_version(version_dir: Path, output_root: Path):
    """
    Processes all modules inside a version directory.
    Each subdirectory is treated as a module.
    For every module:
      - Collect .sr*/.srd/.srs files
      - Generate a combined output file named after the module
    Output filename: <module>.txt stored under the version folder.
    """
    version = version_dir.name
    modules = [d for d in version_dir.iterdir() if d.is_dir()]
    if not modules:
        return

    for module in modules:
        files = collect_files(module)
        if not files:
            continue

        output_file = output_root / version / f"{module.name}.txt"
        write_combined_file(files, output_file)

def main(argv: list[str]) -> int:
    """
    Main entry point.
    Arguments:
        argv[1] → Root folder containing extracted PowerBuilder source files.
        argv[2] → Output root where consolidated files will be written.

    If arguments are not provided, default paths inside
    'Resource Files/Extraction' are used.

    Workflow:
      - Validate source root
      - Iterate through version directories
      - Process each version via process_version()
    Returns 0 on success, 1 on invalid/missing directory.
    """
    # Input / fallback paths
    if len(argv) >= 3:
        sources_root = Path(argv[1])
        converted_root = Path(argv[2])
    else:
        base = Path(__file__).resolve().parents[2]
        sources_root = base / "Resource Files" / "Extraction" / "Sources"
        converted_root = base / "Resource Files" / "Extraction" / "Converted"

    if not sources_root.exists():
        return 1

    versions = [d for d in sources_root.iterdir() if d.is_dir()]
    if not versions:
        return 1

    for version in versions:
        process_version(version, converted_root)

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))