import re
from pathlib import Path
import sys

# Root folder where mirrored PowerBuilder versions are stored.
MIRROR_ROOT = Path(sys.argv[1])

# Folder where the generated report files will be written.
OUTPUT_ROOT = Path(sys.argv[2])

# PowerBuilder versions expected inside the mirror structure.
VERSIONS = ["6.5", "7.0", "8.0", "9.0", "10.5", "12.5"]

# Regex to extract PBLs from declarations such as:
# applib "xxx"; libs="a.pbl;b.pbl"; liblist "c.pbl"
PBL_LIST_RE = re.compile(
    r'(liblist|libs|applib)\s*=?\s*"([^"]+)"',
    re.IGNORECASE
)

# Regex to extract PBL references from project-style strings like:
# "1&something&file.pbl"
PROJECT_PBL_RE = re.compile(
    r'"\s*[^"&]*&[^"&]*&([^"]+)"'
)

def sanitize(name: str) -> str:
    """
    Produces a filesystem-safe, cross-platform identifier.
    Deterministic, stable, and collision-resistant enough for PB projects.
    """
    name = name.strip()
    name = re.sub(r'[<>:"/\\|?*]', '_', name)   # Windows forbidden
    name = re.sub(r'\s+', '_', name)            # Any whitespace → _
    name = re.sub(r'_+', '_', name)              # Collapse
    return name.strip('_')

def normalize_pbl_name(raw: str) -> str:
    """
    Returns only the filename portion of a PBL reference.
    Ensures consistency when comparing or displaying PBL names.
    """
    return Path(raw).name

def extract_pbl_from_pbt(pbt_path: Path) -> list[str]:
    """
    Parses a .pbt project file and extracts all referenced PBLs.
    Sources:
      1) PROJECT_PBL_RE for project-format PBL references.
      2) PBL_LIST_RE for applib/libs/liblist definitions.
    Returns a deduplicated list preserving the order of appearance.
    """
    text = pbt_path.read_text(encoding="utf-8", errors="ignore")
    pbls = []

    # Extract PBLs from project-encoded lines (@begin Projects section)
    for m in PROJECT_PBL_RE.finditer(text):
        pbls.append(normalize_pbl_name(m.group(1).strip()))

    # Extract PBLs from applib/libs/liblist definitions
    for m in PBL_LIST_RE.finditer(text):
        raw = m.group(2)
        for entry in raw.split(";"):
            clean = entry.strip()
            if clean:
                pbls.append(normalize_pbl_name(clean))

    # Remove duplicates while keeping the original order
    seen = set()
    result = []
    for p in pbls:
        key = p.lower()
        if key not in seen:
            seen.add(key)
            result.append(p)

    return result

def load_all_pbl_from_mirror() -> set[str]:
    """
    Scans all version directories inside the mirror and returns a set
    of all available PBL filenames (lowercase).
    Used to check whether referenced PBLs are actually present.
    """
    all_pbl = set()
    for version in VERSIONS:
        folder = MIRROR_ROOT / version
        if not folder.exists():
            continue
        for p in folder.rglob("*.pbl"):
            all_pbl.add(p.name.lower())
    return all_pbl

def normalize_text(text: str) -> str:
    """
    Normalizes line endings and whitespace to avoid repeated rewrites
    of identical files. Ensures deterministic output.
    """
    text = text.replace("\r\n", "\n")
    text = "\n".join(line.rstrip() for line in text.split("\n"))
    text = text.rstrip()
    return text + "\n"

def write_if_changed(path: Path, content: str):
    """
    Writes 'content' to file only if the normalized version differs from
    the existing file. Prevents unnecessary updates.
    """
    new = normalize_text(content)

    if path.exists():
        old = normalize_text(path.read_text(encoding="utf-8", errors="ignore"))
        if old == new:
            return  # No change → skip write

    path.write_text(new, encoding="utf-8")

def build_output_folder(pbt: Path, version_dir: Path) -> str:
    """
    Builds a deterministic, filesystem-safe folder name for a PBT.
    """
    rel = pbt.relative_to(version_dir)
    pbt_name = sanitize(pbt.name)

    rel_parts = [sanitize(p) for p in rel.parts[:-1]]

    if rel_parts:
        return f"{pbt_name}__{'__'.join(rel_parts)}"
    else:
        return pbt_name

def build_txt_filename(pbt: Path) -> str:
    """
    TXT filename always derived from sanitized PBT name.
    """
    return f"{sanitize(pbt.name)}.txt"

def main():
    """
    Main execution workflow:
      - Load all existing PBLs from the mirror.
      - For each version, scan all .pbt files.
      - Extract referenced PBLs.
      - Generate a text report listing present and missing references.
      - Create a folder per .pbt using flattening rules.
      - Inside that folder, create a .txt with the same name as the .pbt.
      - Write the report using a stable/normalized output format.
    """
    ALL_PBL = load_all_pbl_from_mirror()

    for version in VERSIONS:
        version_dir = MIRROR_ROOT / version
        if not version_dir.exists():
            continue

        out_dir = OUTPUT_ROOT / version
        out_dir.mkdir(parents=True, exist_ok=True)

        # Process each project file within the version folder
        for pbt in version_dir.rglob("*.pbt"):
            pbls = extract_pbl_from_pbt(pbt)

            # Build content for the output report
            lines = [
                f"Project: {pbt.name}",
                f"Version: {version}",
                "PBLs:",
            ]

            for pbl in pbls:
                if pbl.lower() in ALL_PBL:
                    lines.append(f" - {pbl}")
                else:
                    lines.append(f" - {pbl} (NOT FOUND)")

            content = "\n".join(lines)

            # Build folder name (sanitized)
            folder_name = build_output_folder(pbt, version_dir)
            target_dir = out_dir / folder_name
            target_dir.mkdir(parents=True, exist_ok=True)

            # Build .txt filename (sanitized)
            txt_name = build_txt_filename(pbt)
            out_file = target_dir / txt_name

            # Write only if content changed
            write_if_changed(out_file, content)

if __name__ == "__main__":
    main()