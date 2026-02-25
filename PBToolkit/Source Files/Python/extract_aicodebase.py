import re
import sys
from pathlib import Path

# Versions supported by the mirror layout (Mirror/<version>/...).
VERSIONS = ["6.5", "7.0", "8.0", "9.0", "10.5", "12.5"]

# Enables/disable the final warnings summary output.
# Warnings are still collected during execution even if disabled.
WARNINGS_ENABLED = False

# File extensions belonging to exported PowerBuilder source objects.
# *.sr* → general source files (SRD, SRW, SRU…)
# *.srd → DataWindow source
# *.srs → Structure source
EXTS = ["*.sr*", "*.srd", "*.srs"]

# Extracts the Targets block from a PBW file.
PBW_TARGETS_BLOCK_RE = re.compile(r"@begin\s+Targets(.*?)@end;", re.IGNORECASE | re.DOTALL)

# Extracts quoted paths inside the PBW Targets block.
QUOTED_PATH_RE = re.compile(r'"([^"]+)"')

# Extracts PBL references from a PBT file.
# Covers: LibList, Libs, AppLib
PBL_LIST_RE = re.compile(r'(liblist|libs|applib)\s*=?\s*"([^"]+)"', re.IGNORECASE)

# Detects start of a member definition inside an exported PB object.
# Captures:
#   group(2) -> kind (function|subroutine|event)
#   group(3) -> remainder of signature (name + params, etc.)
MEMBER_START_RE = re.compile(
    r"^\s*(public|private|protected)?\s*(function|subroutine|event)\s+(.+?)\s*$",
    re.IGNORECASE,
)

# Detects end of a member block.
MEMBER_END_RE = re.compile(r"^\s*end\s+(function|subroutine|event)\b", re.IGNORECASE)

# Detects a single-member "object" (e.g., a global function exported as an object file).
# Includes modifiers: global/public/private/protected (or none).
SINGLE_MEMBER_START_RE = re.compile(
    r"^\s*(global|public|private|protected)?\s*(function|subroutine|event)\s+(.+?)\s*$",
    re.IGNORECASE,
)


def sanitize(name: str) -> str:
    """
    Sanitizes a name for filesystem usage (directory/file name).
    Replaces invalid path characters and normalizes whitespace/underscores.
    """
    name = name.strip()
    name = re.sub(r'[<>:"/\\|?*]', "_", name)
    name = re.sub(r"\s+", "_", name)
    name = re.sub(r"_+", "_", name)
    return name.strip("_")


def read_source_file(f: Path) -> str:
    """
    Reads a PowerBuilder source export file while handling multiple possible encodings.
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
        try:
            text = raw.decode("utf-8")
        except Exception:
            text = raw.decode("windows-1252", errors="ignore")

    text = text.replace("\x00", "")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text


def parse_pbw_targets(pbw_path: Path, warnings: list[str]) -> list[Path]:
    """
    Parses a PBW file and returns the list of referenced PBT targets.
    - Reads @begin Targets ... @end; block.
    - Extracts quoted paths.
    - Resolves paths relative to the PBW folder.
    - Skips missing PBTs (adds a warning).
    Returns a deterministic, unique, sorted list of Paths.
    """
    txt = pbw_path.read_text(encoding="utf-8", errors="ignore")
    m = PBW_TARGETS_BLOCK_RE.search(txt)
    if not m:
        return []

    block = m.group(1)
    pbts: list[Path] = []

    for qp in QUOTED_PATH_RE.findall(block):
        p = (pbw_path.parent / qp).resolve()
        if p.suffix.lower() != ".pbt":
            continue

        if p.exists():
            pbts.append(p)
        else:
            warnings.append(f"[AICodebase][WARN] Missing PBT referenced by PBW '{pbw_path.name}': {p}")

    # Deterministic unique
    seen: set[str] = set()
    out: list[Path] = []
    for p in sorted(pbts, key=lambda x: x.as_posix().lower()):
        k = p.as_posix().lower()
        if k not in seen:
            seen.add(k)
            out.append(p)
    return out


def parse_pbt_libs(pbt_path: Path) -> list[str]:
    """
    Parses a PBT file and returns the list of referenced PBL filenames.
    - Extracts LibList/Libs/AppLib values.
    - Splits by ';' and keeps filenames only.
    Returns a deterministic, unique list preserving first-seen order.
    """
    if not pbt_path.exists():
        return []

    txt = pbt_path.read_text(encoding="utf-8", errors="ignore")

    pbls: list[str] = []
    for m in PBL_LIST_RE.finditer(txt):
        raw = m.group(2)
        for entry in raw.split(";"):
            clean = entry.strip()
            if clean:
                pbls.append(Path(clean).name)

    # Deterministic unique preserving order
    seen: set[str] = set()
    out: list[str] = []
    for p in pbls:
        k = p.lower()
        if k not in seen:
            seen.add(k)
            out.append(p)
    return out


def collect_object_files(pbl_sources_dir: Path) -> list[Path]:
    """
    Collects all exported PB source files under a PBL directory in Sources/<version>/<pbl>/...
    Matches against EXTS and returns a deterministic sorted list of files.
    """
    files: list[Path] = []
    for pattern in EXTS:
        files.extend(pbl_sources_dir.rglob(pattern))
    return sorted((p for p in files if p.is_file()), key=lambda x: x.as_posix().lower())


def normalize_member_name(kind: str, signature: str) -> str:
    """
    Builds a stable filesystem name for a member.
    Attempts to extract the member identifier from the signature.
    Returns a sanitized filename stem like: 'function_of_xxx', 'event_clicked', ...
    """
    tokens = re.findall(r"[A-Za-z_]\w*", signature)

    kind_l = kind.lower()
    if kind_l == "function":
        # Usually: <returnType> <functionName> (...)
        name = tokens[1] if len(tokens) >= 2 else (tokens[0] if tokens else "unknown")
    else:
        # subroutine/event: usually starts with the member name
        name = tokens[0] if tokens else "unknown"

    return sanitize(f"{kind_l}_{name}")


def split_object_members(content: str) -> tuple[str, list[tuple[str, str]]]:
    """
    Splits an exported PB object into:
      - header_text: everything outside member blocks
      - members: list of (filename, raw_member_text_including_wrappers)
    Member blocks are detected via MEMBER_START_RE ... MEMBER_END_RE.
    If a file ends while inside a member, the member is still flushed (best effort).
    """
    lines = content.split("\n")

    header_lines: list[str] = []
    members: list[tuple[str, str]] = []

    in_member = False
    member_kind = ""
    member_sig = ""
    member_buf: list[str] = []
    pre_buf: list[str] = []

    def flush_member() -> None:
        nonlocal member_kind, member_sig, member_buf
        if member_buf:
            fname = normalize_member_name(member_kind, member_sig) + ".txt"
            members.append((fname, "\n".join(member_buf).strip() + "\n"))
        member_kind = ""
        member_sig = ""
        member_buf = []

    for line in lines:
        m = MEMBER_START_RE.match(line)
        if not in_member and m:
            # Non-member content collected so far goes to header.
            header_lines.extend(pre_buf)
            pre_buf = []

            member_kind = m.group(2).lower()
            member_sig = m.group(3).strip()
            in_member = True
            member_buf = [line]
            continue

        if in_member:
            member_buf.append(line)
            if MEMBER_END_RE.match(line):
                flush_member()
                in_member = False
            continue

        pre_buf.append(line)

    # If file ends mid-member, still flush to avoid losing content.
    if in_member:
        flush_member()

    header_lines.extend(pre_buf)

    header_text = "\n".join(header_lines).strip()
    if header_text:
        header_text += "\n"
    return header_text, members


def strip_member_wrappers(member_text: str, kind: str) -> str:
    """
    Removes only the wrappers required to paste the member body into the PB editor:
      - Removes the first line (member declaration).
      - Removes the last 'end <kind>' line (if present).
    Keeps everything else unchanged.
    """
    lines = member_text.split("\n")

    # Remove header line (declaration)
    if lines:
        lines.pop(0)

    # Trim trailing empty lines
    while lines and lines[-1].strip() == "":
        lines.pop()

    # Remove last 'end <kind>' if present
    end_re = re.compile(rf"^\s*end\s+{re.escape(kind)}\b", re.IGNORECASE)
    if lines and end_re.match(lines[-1]):
        lines.pop()

    # Trim trailing empty lines again
    while lines and lines[-1].strip() == "":
        lines.pop()

    return "\n".join(lines) + ("\n" if lines else "")


def extract_single_member_body_if_any(content: str) -> str | None:
    """
    Fallback extractor for objects that are actually a single member (common with 'function_object' exports).
    The exported file may contain wrapper sections (types, forward prototypes, etc.).
    Strategy:
      1) Find the last 'end function|subroutine|event' in the file (assumed to be the real implementation).
      2) Walk upwards to find the nearest matching member start header.
      3) Return only the body (drop first and last line only).
    Returns None if a safe extraction is not possible.
    """
    lines = content.split("\n")

    # 1) Find last end <kind>
    end_idx = None
    kind = None
    end_any_re = re.compile(r"^\s*end\s+(function|subroutine|event)\b", re.IGNORECASE)

    for i in range(len(lines) - 1, -1, -1):
        m = end_any_re.match(lines[i])
        if m:
            kind = m.group(1).lower()
            end_idx = i
            break

    if end_idx is None or kind is None:
        return None

    # 2) Find nearest preceding start header for that kind
    start_idx = None
    start_re = re.compile(
        rf"^\s*(global|public|private|protected)?\s*{re.escape(kind)}\s+(.+?)\s*$",
        re.IGNORECASE,
    )

    for j in range(end_idx - 1, -1, -1):
        if start_re.match(lines[j]):
            start_idx = j
            break

    if start_idx is None:
        return None

    # 3) Extract block and drop only first + last line
    block = lines[start_idx : end_idx + 1]
    if len(block) < 2:
        return None

    body = "\n".join(block[1:-1]).strip()
    return body + ("\n" if body else "")


def infer_kind_from_filename(fname: str) -> str:
    """
    Infers the member kind from the generated member filename.
    """
    f = fname.lower()
    if f.startswith("function_"):
        return "function"
    if f.startswith("event_"):
        return "event"
    return "subroutine"


def main(argv: list[str]) -> int:
    """
    Entry point.
    Usage:
      extract_aicodebase.py <mirror_root> <sources_root> <output_root>

    Generates:
      Extraction/AICodebase/<version>/<pbw>/<pbl>/<object>/
        - _object.txt           (header/outside-members content)
        - function_*.txt        (member bodies without wrappers)
        - event_*.txt
        - subroutine_*.txt
        - object.txt            (fallback: single-member body if detected, else full object)

    Notes:
      - PBLs are not deduplicated across PBWs (each PBW has its own output tree).
      - Within a PBW, PBLs are unioned across targets to avoid folder collisions.
      - Output is deterministic (sorted PBWs, PBTs, objects, members).
    """
    if len(argv) != 4:
        print("Usage: extract_aicodebase.py <mirror_root> <sources_root> <output_root>")
        return 2

    mirror_root = Path(argv[1])
    sources_root = Path(argv[2])
    output_root = Path(argv[3])

    warnings: list[str] = []

    for version in VERSIONS:
        version_mirror = mirror_root / version
        version_sources = sources_root / version
        version_out = output_root / version

        if not version_mirror.exists():
            continue
        if not version_sources.exists():
            warnings.append(f"[AICodebase][WARN] Missing Sources for version {version}: {version_sources}")
            continue

        # Discover PBWs under the mirror version folder.
        pbws = sorted(version_mirror.rglob("*.pbw"), key=lambda p: p.as_posix().lower())
        for pbw in pbws:
            pbw_name = sanitize(pbw.stem)
            out_pbw_dir = version_out / pbw_name

            pbts = parse_pbw_targets(pbw, warnings)

            # Union of PBLs across targets within the same PBW (avoid folder collisions).
            seen_pbl: set[str] = set()
            pbl_list: list[str] = []
            for pbt in pbts:
                for pbl in parse_pbt_libs(pbt):
                    k = pbl.lower()
                    if k not in seen_pbl:
                        seen_pbl.add(k)
                        pbl_list.append(pbl)

            for pbl in pbl_list:
                pbl_stem = sanitize(Path(pbl).stem)
                pbl_sources_dir = version_sources / pbl_stem
                if not pbl_sources_dir.exists():
                    warnings.append(f"[AICodebase][WARN] {version}/{pbw_name}: PBL not found in Sources: {pbl_stem}")
                    continue

                out_pbl_dir = out_pbw_dir / pbl_stem
                out_pbl_dir.mkdir(parents=True, exist_ok=True)

                obj_files = collect_object_files(pbl_sources_dir)
                for f in obj_files:
                    obj_stem = sanitize(f.stem)
                    obj_dir = out_pbl_dir / obj_stem
                    obj_dir.mkdir(parents=True, exist_ok=True)

                    content = read_source_file(f)
                    header_text, members = split_object_members(content)

                    # Always write a context/header container.
                    (obj_dir / "_object.txt").write_text(header_text, encoding="utf-8", errors="ignore")

                    if not members:
                        # Fallback:
                        # - Try extracting a single member body (ignoring PB export wrapper sections).
                        # - If not possible, keep the full object export.
                        cleaned = extract_single_member_body_if_any(content)
                        if cleaned is not None:
                            (obj_dir / "object.txt").write_text(cleaned, encoding="utf-8", errors="ignore")
                        else:
                            (obj_dir / "object.txt").write_text(content, encoding="utf-8", errors="ignore")
                        continue

                    # Write each member as its own file, without wrappers.
                    for fname, body in sorted(members, key=lambda x: x[0].lower()):
                        kind = infer_kind_from_filename(fname)
                        cleaned = strip_member_wrappers(body, kind)
                        (obj_dir / fname).write_text(cleaned, encoding="utf-8", errors="ignore")

    # Print warnings only at the end (avoid polluting the progress bar).
    if WARNINGS_ENABLED and warnings:
        print("\n[AICodebase] Warnings summary:")
        for w in warnings:
            print(w)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))