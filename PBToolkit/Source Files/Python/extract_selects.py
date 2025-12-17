import re
import sys
from pathlib import Path


def read_clean_text(path: Path) -> str:
    """
    Reads a combined TXT file generated in the Converted stage and normalizes
    line endings and spacing.

    Converted files are produced by combine_to_files.py and are expected
    to be UTF-8 encoded. A fallback encoding is used for robustness.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        text = path.read_text(encoding="windows-1252", errors="ignore")

    # Normalize line endings and collapse excessive blank lines
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = re.sub(r"\n{2,}", "\n", text)
    return text


def extract_selects_pb(text: str) -> list[str]:
    """
    Extracts SQL SELECT blocks from PowerBuilder-generated source text.

    A SELECT block starts when the keyword 'select' appears and ends when:
      - A semicolon is found at line end, or
      - An 'end' keyword is encountered.

    Returns a list of raw SELECT blocks preserving original formatting.
    """
    selects = []
    buf = []
    inside = False
    select_re = re.compile(r"(?i)\bselect\b")

    for line in text.splitlines():
        stripped = line.strip()

        # Detect start of SELECT block
        if select_re.search(stripped):
            if inside and buf:
                selects.append("\n".join(buf).strip())
                buf = []
            inside = True

        if inside:
            buf.append(line.rstrip())

            # Detect end of SELECT block
            if stripped.endswith(";") or re.match(r"(?i)^end\b", stripped):
                selects.append("\n".join(buf).strip())
                buf = []
                inside = False

    # Handle unterminated SELECT at EOF
    if inside and buf:
        selects.append("\n".join(buf).strip())

    # Safety filter
    return [s for s in selects if "select" in s.lower()]


def process_file(input_file: Path, output_file: Path, pbl_name: str):
    """
    Processes a single Converted TXT file:
      - Reads and cleans the content.
      - Extracts all SELECT blocks.
      - Writes a new TXT file in the Selects folder.

    The output file always includes a mandatory header indicating
    the originating PBL name.
    """
    text = read_clean_text(input_file)
    selects = extract_selects_pb(text)

    output_file.parent.mkdir(parents=True, exist_ok=True)

    with output_file.open("w", encoding="utf-8") as f:
        # Mandatory header
        f.write(f"PBL: {pbl_name}\n")

        # Write each SELECT with a numbered marker
        for i, sel in enumerate(selects, 1):
            f.write(f"\n-- SELECT {i}\n{sel}\n")


def collect_inputs_recursive(root: Path):
    """
    Recursively collects all .txt files under the given root directory.
    Used to scan the entire Converted tree.
    """
    return sorted(root.rglob("*.txt"))


def main(argv):
    """
    Main execution workflow:
      - Receives input (Converted) and output (Selects) directories.
      - Scans all Converted TXT files recursively.
      - For each file:
          * Derives the PBL name from the filename.
          * Preserves relative directory structure.
          * Extracts SELECT blocks into a new TXT file.
    """
    input_root = Path(argv[1])   # Converted directory
    output_root = Path(argv[2])  # Selects directory

    files = collect_inputs_recursive(input_root)
    if not files:
        print("No files found in Converted directory")
        return 1

    for file in files:
        # Preserve version / folder structure
        rel = file.relative_to(input_root)     # e.g. 6.5/ftp8.txt
        pbl_name = file.stem + ".pbl"          # ftp8.pbl

        out_file = output_root / rel           # Selects/6.5/ftp8.txt
        process_file(file, out_file, pbl_name)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))