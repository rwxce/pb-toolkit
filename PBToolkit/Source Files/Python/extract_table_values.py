import json
import re
import sys
from pathlib import Path
from collections import defaultdict

# Supported PowerBuilder versions
VERSIONS = ["6.5", "7.0", "8.0", "9.0", "10.5", "12.5"]

# Regex to capture full column=(...) blocks, including nested parentheses
COLUMN_PATTERN = re.compile(
    r'column=\((?:[^()]|\([^)]*\))*\)',
    re.IGNORECASE | re.DOTALL,
)


def split_chunks(values_str: str) -> list[str]:
    """
    Splits a PowerBuilder 'values=' string into logical chunks.

    Chunks are separated by single '/' characters. Escaped separators ('//')
    are treated as literal '/' characters.

    Returns a list of raw chunk strings with surrounding whitespace removed.
    """
    chunks, buf = [], []
    i = 0

    while i < len(values_str):
        ch = values_str[i]

        if ch == "/":
            if i + 1 < len(values_str) and values_str[i + 1] == "/":
                buf.append("/")
                i += 2
            else:
                chunk = "".join(buf).strip()
                if chunk:
                    chunks.append(chunk)
                buf = []
                i += 1
        else:
            buf.append(ch)
            i += 1

    if buf:
        chunk = "".join(buf).strip()
        if chunk:
            chunks.append(chunk)

    return chunks


def parse_chunk(chunk: str) -> tuple[str, str]:
    """
    Parses a single value chunk into label and code components.

    The expected format is:
        <label><TAB><code>
    or:
        <label> <code>

    If no code can be determined, an empty string is returned as code.
    """
    s = chunk.rstrip()
    tab = s.rfind("\t")

    if tab != -1:
        return s[:tab].strip(), s[tab + 1:].strip()

    parts = s.rsplit(None, 1)
    if len(parts) == 2:
        return parts[0].strip(), parts[1].strip()

    return s, ""


def extract_from_text(content: str) -> dict[str, dict[str, str]]:
    """
    Extracts database value mappings from PowerBuilder source text.

    For each column=(...) block found:
      - Reads dbname and values attributes.
      - Splits and parses individual value definitions.
      - Aggregates results by database name.

    Returns a dictionary of the form:
        {
            "DBNAME": {
                "CODE": "Label",
                ...
            },
            ...
        }
    """
    result = defaultdict(dict)

    for m in COLUMN_PATTERN.finditer(content):
        block = m.group(0)

        dbm = re.search(r'dbname\s*=\s*"([^"]+)"', block, re.I)
        valm = re.search(r'values\s*=\s*"([^"]+)"', block, re.I | re.S)
        if not dbm or not valm:
            continue

        dbname = dbm.group(1)
        chunks = split_chunks(valm.group(1))
        if not chunks:
            continue

        mapping = {}
        for ch in chunks:
            label, code = parse_chunk(ch)
            mapping[(code or "_").upper()] = label

        result[dbname].update(mapping)

    return result


def load_pbls_from_pbt_txt(path: Path) -> list[str]:
    """
    Reads a *.pbt.txt file and extracts referenced PBL filenames.

    Only lines starting with '- ' are considered.
    Any '(NOT FOUND)' markers are ignored.

    Returned PBL names are normalized to lowercase.
    """
    pbls = []

    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = line.strip()
        if line.startswith("- "):
            pbl = line[2:].replace("(NOT FOUND)", "").strip()
            if pbl:
                pbls.append(pbl.lower())

    return pbls


def project_needs_processing(sources: list[Path], out_file: Path) -> bool:
    """
    Determines whether a project must be reprocessed.

    Processing is required if:
      - The output file does not exist, or
      - Any source file is newer than the output file.
    """
    if not out_file.exists():
        return True

    out_time = out_file.stat().st_mtime
    return any(src.stat().st_mtime > out_time for src in sources)


def main(argv: list[str]) -> int:
    """
    Main execution workflow.

    Expected arguments:
      1. ConvertedRoot: directory containing converted PB sources.
      2. ProjectsRoot: directory containing per-project folders.

    For each version and project:
      - Resolves referenced PBL sources.
      - Extracts column value mappings.
      - Writes a tables.json file per project.
    """
    if len(argv) < 3:
        print("Usage: extract_table_values.py <ConvertedRoot> <ProjectsRoot>")
        return 1

    converted_root = Path(argv[1])
    projects_root = Path(argv[2])

    if not converted_root.exists():
        print(f"[ERROR] ConvertedRoot not found: {converted_root}")
        return 1

    if not projects_root.exists():
        print(f"[ERROR] ProjectsRoot not found: {projects_root}")
        return 1

    processed = skipped = no_sources = 0

    for version in VERSIONS:
        proj_ver_dir = projects_root / version
        conv_ver_dir = converted_root / version

        if not proj_ver_dir.exists():
            continue

        for project_dir in proj_ver_dir.iterdir():
            if not project_dir.is_dir():
                continue

            pbt_txt = next(project_dir.glob("*.pbt.txt"), None)
            if not pbt_txt:
                continue

            pbls = load_pbls_from_pbt_txt(pbt_txt)

            sources = []
            for pbl in pbls:
                stem = Path(pbl).stem
                src = conv_ver_dir / f"{stem}.txt"
                if src.exists():
                    sources.append(src)

            out_file = project_dir / "tables.json"

            if not sources:
                no_sources += 1
                continue

            if not project_needs_processing(sources, out_file):
                skipped += 1
                continue

            aggregated = defaultdict(dict)

            for src in sources:
                content = src.read_text(encoding="utf-8", errors="ignore")
                extracted = extract_from_text(content)

                for dbname, mapping in extracted.items():
                    aggregated[dbname].update(mapping)

            out_file.parent.mkdir(parents=True, exist_ok=True)
            out_file.write_text(
                json.dumps(
                    [{"dbname": k, "values": v} for k, v in aggregated.items()],
                    indent=4,
                    ensure_ascii=False,
                ),
                encoding="utf-8",
            )

            processed += 1
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))