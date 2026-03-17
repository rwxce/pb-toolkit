import os
import re
import sys
import json
import time
import shutil
import ctypes
import hashlib
import tempfile
import subprocess
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# Versions supported by the mirror layout (Mirror/<version>/...).
VERSIONS = ["6.5", "7.0", "8.0", "9.0", "10.5", "12.5"]

# Enables/disables the final warnings summary output (kept for diagnostics).
WARNINGS_ENABLED = False

# File extensions belonging to exported PowerBuilder source objects.
EXTS = ["*.sr*", "*.srd", "*.srs"]

# Manifest schema for content-aware PBL fingerprints.
FINGERPRINT_SCHEMA = 2

# PBW parsing
PBW_TARGETS_BLOCK_RE = re.compile(r"@begin\s+Targets(.*?)@end;", re.IGNORECASE | re.DOTALL)
QUOTED_PATH_RE = re.compile(r'"([^"]+)"')

# PBT parsing (covers LibList, Libs, AppLib)
PBL_LIST_RE = re.compile(r'(liblist|libs|applib)\s*=?\s*"([^"]+)"', re.IGNORECASE)

# Member splitting inside exported objects
MEMBER_START_RE = re.compile(
    r"^\s*(public|private|protected)?\s*(function|subroutine|event)\s+(.+?)\s*$",
    re.IGNORECASE,
)
MEMBER_END_RE = re.compile(r"^\s*end\s+(function|subroutine|event)\b", re.IGNORECASE)

# Single-member fallback extraction (object files that are essentially a single implementation)
END_ANY_RE = re.compile(r"^\s*end\s+(function|subroutine|event)\b", re.IGNORECASE)


def sanitize(name: str) -> str:
    """Sanitizes a name for filesystem usage (directory/file name)."""
    name = name.strip()
    name = re.sub(r'[<>:"/\\|?*]', "_", name)
    name = re.sub(r"\s+", "_", name)
    name = re.sub(r"_+", "_", name)
    return name.strip("_")


def read_source_file(f: Path) -> str:
    """
    Reads a PowerBuilder source export file while handling multiple possible encodings.
    Normalizes newlines to '\n'.
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


def parse_pbw_targets(pbw_path: Path, warnings: List[str]) -> List[Path]:
    """
    Parses a PBW file and returns referenced PBT targets (existing only).
    """
    txt = pbw_path.read_text(encoding="utf-8", errors="ignore")
    m = PBW_TARGETS_BLOCK_RE.search(txt)
    if not m:
        return []

    block = m.group(1)
    pbts: List[Path] = []

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
    out: List[Path] = []
    for p in sorted(pbts, key=lambda x: x.as_posix().lower()):
        k = p.as_posix().lower()
        if k not in seen:
            seen.add(k)
            out.append(p)
    return out


def parse_pbt_libs(pbt_path: Path) -> List[str]:
    """
    Parses a PBT file and returns referenced PBL filenames (unique, ordered).
    """
    if not pbt_path.exists():
        return []

    txt = pbt_path.read_text(encoding="utf-8", errors="ignore")

    pbls: List[str] = []
    for m in PBL_LIST_RE.finditer(txt):
        raw = m.group(2)
        for entry in raw.split(";"):
            clean = entry.strip()
            if clean:
                pbls.append(Path(clean).name)

    # Unique preserving order
    seen: set[str] = set()
    out: List[str] = []
    for p in pbls:
        k = p.lower()
        if k not in seen:
            seen.add(k)
            out.append(p)
    return out


def collect_object_files(pbl_sources_dir: Path) -> List[Path]:
    """
    Collects exported PB source files under a PBL directory in Sources/<version>/<pbl>/...
    """
    files: List[Path] = []
    for pattern in EXTS:
        files.extend(pbl_sources_dir.rglob(pattern))
    return sorted((p for p in files if p.is_file()), key=lambda x: x.as_posix().lower())


def normalize_member_name(kind: str, signature: str) -> str:
    """
    Builds a stable filesystem name for a member.
    """
    tokens = re.findall(r"[A-Za-z_]\w*", signature)

    kind_l = kind.lower()
    if kind_l == "function":
        name = tokens[1] if len(tokens) >= 2 else (tokens[0] if tokens else "unknown")
    else:
        name = tokens[0] if tokens else "unknown"

    return sanitize(f"{kind_l}_{name}")


def split_object_members(content: str) -> Tuple[str, List[Tuple[str, str]]]:
    """
    Splits an exported PB object into header_text and member blocks.
    Members include wrappers (first line + end line), stripped later.
    """
    lines = content.split("\n")

    header_lines: List[str] = []
    members: List[Tuple[str, str]] = []

    in_member = False
    member_kind = ""
    member_sig = ""
    member_buf: List[str] = []
    pre_buf: List[str] = []

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

    if in_member:
        flush_member()

    header_lines.extend(pre_buf)

    header_text = "\n".join(header_lines).strip()
    if header_text:
        header_text += "\n"
    return header_text, members


def strip_member_wrappers(member_text: str, kind: str) -> str:
    """
    Safe wrapper stripping:
    - removes only the first line (declaration)
    - removes only the last 'end <kind>' line (if present)
    """
    lines = member_text.split("\n")

    if lines:
        lines.pop(0)

    while lines and lines[-1].strip() == "":
        lines.pop()

    end_re = re.compile(rf"^\s*end\s+{re.escape(kind)}\b", re.IGNORECASE)
    if lines and end_re.match(lines[-1]):
        lines.pop()

    while lines and lines[-1].strip() == "":
        lines.pop()

    return "\n".join(lines) + ("\n" if lines else "")


def infer_kind_from_filename(fname: str) -> str:
    f = fname.lower()
    if f.startswith("function_"):
        return "function"
    if f.startswith("event_"):
        return "event"
    return "subroutine"


def extract_single_member_body_if_any(content: str) -> Optional[str]:
    """
    Fallback extractor for objects that are a single real implementation.
    Avoids 'forward prototypes' by:
      - finding the last 'end function|subroutine|event'
      - walking upwards to find the nearest matching start line for that kind
      - returning only the body (drop first + last line)
    """
    lines = content.split("\n")

    end_idx = None
    kind = None

    for i in range(len(lines) - 1, -1, -1):
        m = END_ANY_RE.match(lines[i])
        if m:
            kind = m.group(1).lower()
            end_idx = i
            break

    if end_idx is None or kind is None:
        return None

    start_re = re.compile(
        rf"^\s*(global|public|private|protected)?\s*{re.escape(kind)}\s+(.+?)\s*$",
        re.IGNORECASE,
    )

    start_idx = None
    for j in range(end_idx - 1, -1, -1):
        if start_re.match(lines[j]):
            start_idx = j
            break

    if start_idx is None:
        return None

    block = lines[start_idx : end_idx + 1]
    if len(block) < 2:
        return None

    body = "\n".join(block[1:-1]).strip()
    return body + ("\n" if body else "")


def sha256_bytes(data: bytes) -> str:
    h = hashlib.sha256()
    h.update(data)
    return h.hexdigest()


def sha256_file(path: Path) -> str:
    """
    Computes sha256 for a file using chunked reads to limit memory usage.
    """
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def compute_pbl_fingerprint(
    pbl_sources_dir: Path,
    previous_entry: Optional[Dict[str, object]] = None,
) -> Dict[str, object]:
    """
    Computes a deterministic fingerprint of the PBL subtree under Sources/<version>/<pbl>.
    v2 content-aware fingerprint:
      - metadata: relative_path, size, mtime_ns
      - content hash: sha256 per file
    Computes content hash for each file to guarantee detection even when timestamps are preserved.
    """
    files = collect_object_files(pbl_sources_dir)

    entries: List[Tuple[str, int, int, str]] = []
    files_map: Dict[str, Dict[str, object]] = {}
    for f in files:
        st = f.stat()
        rel = f.relative_to(pbl_sources_dir).as_posix()
        size = int(st.st_size)
        mtime_ns = int(st.st_mtime_ns)
        file_sha = sha256_file(f)

        files_map[rel] = {
            "size": size,
            "mtime_ns": mtime_ns,
            "sha256": file_sha,
        }
        entries.append((rel, size, mtime_ns, file_sha))

    # Deterministic serialization
    blob = "\n".join(
        f"{rel}|{size}|{mtime}|{file_sha}" for (rel, size, mtime, file_sha) in entries
    ).encode("utf-8", errors="ignore")
    fp = sha256_bytes(blob)

    total_size = sum(e[1] for e in entries)
    mtime_max = max((e[2] for e in entries), default=0)

    return {
        "fingerprint_schema": FINGERPRINT_SCHEMA,
        "fingerprint": fp,
        "total_size": total_size,
        "mtime_max": mtime_max,
        "file_count": len(entries),
        "files": files_map,
    }


def load_json(path: Path) -> Dict[str, object]:
    """
    Loads a JSON file as a dict, returning {} on missing/invalid content.
    """
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="ignore"))
    except Exception:
        return {}


def read_text_if_exists(path: Path) -> Optional[str]:
    """
    Best-effort text read for an existing file, otherwise None.
    """
    if not path.exists() or not path.is_file():
        return None
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return None


def set_file_attributes_normal_windows(path: Path) -> None:
    """
    Clears restrictive attributes on Windows before writing (best effort).
    """
    if os.name != "nt":
        return
    try:
        FILE_ATTRIBUTE_NORMAL = 0x80
        ctypes.windll.kernel32.SetFileAttributesW(str(path), FILE_ATTRIBUTE_NORMAL)
    except Exception:
        pass


def save_json(path: Path, data: Dict[str, object]) -> None:
    """
    Safely persists JSON to disk on Windows:
      - skip if unchanged
      - repair invalid path state (directory where file is expected)
      - atomic replace via temporary file
      - retry on transient PermissionError
    """
    path.parent.mkdir(parents=True, exist_ok=True)

    payload = json.dumps(data, indent=2, ensure_ascii=False)
    existing = read_text_if_exists(path)
    if existing == payload:
        return

    # Defensive cleanup for corrupt state: manifest path must be a file.
    if path.exists() and path.is_dir():
        ensure_removed(path)

    max_attempts = 4
    for attempt in range(max_attempts):
        tmp_path = None
        try:
            if path.exists():
                set_file_attributes_normal_windows(path)

            fd, tmp_name = tempfile.mkstemp(prefix=f"{path.name}.tmp.", dir=str(path.parent))
            tmp_path = Path(tmp_name)
            with os.fdopen(fd, "w", encoding="utf-8", errors="ignore") as handle:
                handle.write(payload)

            set_file_attributes_normal_windows(tmp_path)
            os.replace(str(tmp_path), str(path))
            return
        except PermissionError as ex:
            if tmp_path is not None:
                ensure_removed(tmp_path)
            if attempt >= max_attempts - 1:
                raise PermissionError(f"{path} ({ex})")
            time.sleep(0.12 * (attempt + 1))
        except Exception:
            if tmp_path is not None:
                ensure_removed(tmp_path)
            raise


def set_hidden_windows(path: Path) -> None:
    """
    Marks a file as Hidden on Windows (best effort).
    """
    if os.name != "nt":
        return
    try:
        FILE_ATTRIBUTE_HIDDEN = 0x02
        ctypes.windll.kernel32.SetFileAttributesW(str(path), FILE_ATTRIBUTE_HIDDEN)
    except Exception:
        # Best effort: do not fail the script
        pass


def ensure_removed(path: Path) -> None:
    if not path.exists():
        return
    if path.is_symlink():
        path.unlink(missing_ok=True)
        return
    if path.is_dir():
        shutil.rmtree(path, ignore_errors=True)
        return
    path.unlink(missing_ok=True)


def try_create_junction(dest_dir: Path, src_dir: Path) -> bool:
    """
    Creates a directory junction on Windows (best effort).
    Returns True if junction/symlink created, False otherwise.
    """
    if os.name != "nt":
        return False

    dest_dir.parent.mkdir(parents=True, exist_ok=True)

    # If exists, remove it (we'll re-create)
    if dest_dir.exists() or dest_dir.is_symlink():
        ensure_removed(dest_dir)

    # Use cmd mklink /J (junction)
    cmd = f'mklink /J "{str(dest_dir)}" "{str(src_dir)}"'
    try:
        result = subprocess.run(["cmd", "/c", cmd], capture_output=True, text=True)
        return result.returncode == 0
    except Exception:
        return False


def copy_tree_incremental(src: Path, dst: Path) -> None:
    """
    Copies a directory tree (used only as a fallback if junction cannot be created).
    Deletes dst first to keep it deterministic.
    """
    ensure_removed(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, dst)


def build_cache_for_pbl(
    version: str,
    pbl_stem: str,
    pbl_sources_dir: Path,
    cache_pbl_dir: Path,
) -> None:
    """
    Builds/updates cache output for a single PBL:
      .pblcache/<PBL>/<object>/{_object.txt, function_*.txt, ...}
    Also deletes cache objects that no longer exist in Sources.
    """
    cache_pbl_dir.mkdir(parents=True, exist_ok=True)

    # Current inputs
    obj_files = collect_object_files(pbl_sources_dir)

    # Build set of expected object dirs (by sanitized stem)
    expected_obj_dirs: set[str] = set()

    for f in obj_files:
        obj_stem = sanitize(f.stem)
        expected_obj_dirs.add(obj_stem)

        obj_dir = cache_pbl_dir / obj_stem
        obj_dir.mkdir(parents=True, exist_ok=True)

        content = read_source_file(f)
        header_text, members = split_object_members(content)

        # _object.txt is kept as-is (context)
        (obj_dir / "_object.txt").write_text(header_text, encoding="utf-8", errors="ignore")

        if not members:
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

        # If previously we had object.txt fallback, remove it to avoid stale artifacts
        stale_object_txt = obj_dir / "object.txt"
        if stale_object_txt.exists():
            stale_object_txt.unlink(missing_ok=True)

    # Delete cache object directories that are no longer present in Sources
    for child in sorted(cache_pbl_dir.iterdir(), key=lambda p: p.name.lower()):
        if not child.is_dir():
            continue
        if child.name not in expected_obj_dirs:
            ensure_removed(child)


def discover_pbw_to_pbls(
    version_mirror: Path,
    warnings: List[str],
) -> Dict[str, List[str]]:
    """
    Discovers PBWs in a mirror version folder and resolves their PBL list.
    Returns mapping: pbw_name (stem) -> list of pbl filenames (ordered, unique).
    """
    pbw_map: Dict[str, List[str]] = {}

    pbws = sorted(version_mirror.rglob("*.pbw"), key=lambda p: p.as_posix().lower())
    for pbw in pbws:
        pbw_name = sanitize(pbw.stem)
        pbts = parse_pbw_targets(pbw, warnings)

        seen_pbl: set[str] = set()
        pbl_list: List[str] = []
        for pbt in pbts:
            for pbl in parse_pbt_libs(pbt):
                k = pbl.lower()
                if k not in seen_pbl:
                    seen_pbl.add(k)
                    pbl_list.append(pbl)

        pbw_map[pbw_name] = pbl_list

    return pbw_map


def cleanup_not_in_mirror(
    version_out: Path,
    cache_dir: Path,
    pbw_map: Dict[str, List[str]],
) -> None:
    """
    Deletes:
      - PBW folders not in pbw_map
      - PBW/PBL folders not referenced by mirror for that PBW
      - cache PBL folders not referenced by any PBW
    """
    # 1) PBW output cleanup (ignore cache folder)
    allowed_pbws = set(pbw_map.keys())

    if version_out.exists():
        for child in sorted(version_out.iterdir(), key=lambda p: p.name.lower()):
            if child.name == ".pblcache":
                continue
            if child.is_dir() and child.name not in allowed_pbws:
                ensure_removed(child)

    # 2) Inside each PBW, remove PBLs not listed
    for pbw_name, pbls in pbw_map.items():
        pbw_dir = version_out / pbw_name
        if not pbw_dir.exists():
            continue
        allowed_pbl_dirs = set(sanitize(Path(p).stem) for p in pbls)
        for child in sorted(pbw_dir.iterdir(), key=lambda p: p.name.lower()):
            if child.is_dir() and child.name not in allowed_pbl_dirs:
                ensure_removed(child)

    # 3) Cache cleanup: remove cache PBL dirs not referenced by any PBW
    required_pbls: set[str] = set()
    for pbls in pbw_map.values():
        for p in pbls:
            required_pbls.add(sanitize(Path(p).stem))

    if cache_dir.exists():
        for child in sorted(cache_dir.iterdir(), key=lambda p: p.name.lower()):
            if not child.is_dir():
                continue
            if child.name == ".manifest.json" or child.name == "_manifest.json":
                continue
            if child.name not in required_pbls:
                ensure_removed(child)


def parse_scope_args(argv: List[str]) -> Tuple[Optional[str], Optional[str], Optional[str], Optional[str]]:
    """
    Supported signatures:
      1) extract_aicodebase.py <mirror_root> <sources_root> <output_root>
      2) extract_aicodebase.py <mirror_root> <sources_root> <output_root> all
      3) extract_aicodebase.py <mirror_root> <sources_root> <output_root> version <v>
      4) extract_aicodebase.py <mirror_root> <sources_root> <output_root> project <v> <pbw>
    """
    if len(argv) == 4:
        return ("all", None, None, None)

    if len(argv) == 5 and argv[4].lower() == "all":
        return ("all", None, None, None)

    if len(argv) == 6 and argv[4].lower() == "version":
        return ("version", argv[5], None, None)

    if len(argv) == 7 and argv[4].lower() == "project":
        return ("project", argv[5], argv[6], None)

    usage = (
        "Usage:\n"
        "  extract_aicodebase.py <mirror_root> <sources_root> <output_root>\n"
        "  extract_aicodebase.py <mirror_root> <sources_root> <output_root> all\n"
        "  extract_aicodebase.py <mirror_root> <sources_root> <output_root> version <v>\n"
        "  extract_aicodebase.py <mirror_root> <sources_root> <output_root> project <v> <pbw>"
    )
    return (None, None, None, usage)


def process_version(
    version: str,
    mirror_root: Path,
    sources_root: Path,
    output_root: Path,
    warnings: List[str],
    project_filter: Optional[str] = None,
) -> int:
    """
    Processes one version end-to-end:
      - discovers PBWs/PBLs
      - updates cache per PBL incrementally
      - assembles PBW output from cache
      - persists manifest safely
    Returns non-zero on hard errors (for example manifest save failure).
    """
    version_mirror = mirror_root / version
    version_sources = sources_root / version
    version_out = output_root / version

    if not version_mirror.exists():
        warnings.append(f"[AICodebase][WARN] Missing Mirror for version {version}: {version_mirror}")
        return 0

    if not version_sources.exists():
        warnings.append(f"[AICodebase][WARN] Missing Sources for version {version}: {version_sources}")
        return 0

    cache_dir = version_out / ".pblcache"
    cache_dir.mkdir(parents=True, exist_ok=True)

    manifest_path = cache_dir / "_manifest.json"
    manifest = load_json(manifest_path)
    if "pbls" not in manifest:
        manifest["pbls"] = {}
    if "pbws" not in manifest:
        manifest["pbws"] = {}

    pbw_map_all = discover_pbw_to_pbls(version_mirror, warnings)
    pbw_map = pbw_map_all

    if project_filter is not None:
        project_key = sanitize(project_filter)
        if project_key not in pbw_map_all:
            print(f"[AICodebase][ERROR] Project '{project_filter}' not found in version '{version}'.")
            return 1
        pbw_map = {project_key: pbw_map_all[project_key]}

    version_out.mkdir(parents=True, exist_ok=True)

    if project_filter is None:
        cleanup_not_in_mirror(version_out, cache_dir, pbw_map)
    else:
        # Targeted cleanup for selected PBW only.
        for pbw_name, pbls in pbw_map.items():
            pbw_dir = version_out / pbw_name
            if not pbw_dir.exists():
                continue
            allowed_pbl_dirs = set(sanitize(Path(p).stem) for p in pbls)
            for child in sorted(pbw_dir.iterdir(), key=lambda p: p.name.lower()):
                if child.is_dir() and child.name not in allowed_pbl_dirs:
                    ensure_removed(child)

    required_pbls: List[str] = []
    seen_req: set[str] = set()
    for pbw_name in sorted(pbw_map.keys(), key=lambda s: s.lower()):
        for pbl in pbw_map[pbw_name]:
            pbl_stem = sanitize(Path(pbl).stem)
            if pbl_stem not in seen_req:
                seen_req.add(pbl_stem)
                required_pbls.append(pbl_stem)

    for pbl_stem in sorted(required_pbls, key=lambda s: s.lower()):
        pbl_sources_dir = version_sources / pbl_stem
        if not pbl_sources_dir.exists():
            warnings.append(f"[AICodebase][WARN] {version}: PBL not found in Sources: {pbl_stem}")
            continue

        old = manifest["pbls"].get(pbl_stem, {})
        old_schema = old.get("fingerprint_schema")
        fp = compute_pbl_fingerprint(pbl_sources_dir, old if isinstance(old, dict) else None)
        old_fp = old.get("fingerprint") if isinstance(old, dict) else None

        cache_pbl_dir = cache_dir / pbl_stem
        # Force one rebuild when migrating from old schema to guarantee correctness.
        schema_migrating = old_schema != FINGERPRINT_SCHEMA

        if not schema_migrating and old_fp == fp["fingerprint"] and cache_pbl_dir.exists():
            # Keep manifest in sync with reused per-file hashes (cheap no-op if unchanged).
            manifest["pbls"][pbl_stem] = fp
            continue

        build_cache_for_pbl(version, pbl_stem, pbl_sources_dir, cache_pbl_dir)
        manifest["pbls"][pbl_stem] = fp

    for pbw_name in sorted(pbw_map.keys(), key=lambda s: s.lower()):
        out_pbw_dir = version_out / pbw_name
        out_pbw_dir.mkdir(parents=True, exist_ok=True)
        manifest["pbws"][pbw_name] = [sanitize(Path(p).stem) for p in pbw_map[pbw_name]]

        for pbl in pbw_map[pbw_name]:
            pbl_stem = sanitize(Path(pbl).stem)
            cache_pbl_dir = cache_dir / pbl_stem
            if not cache_pbl_dir.exists():
                continue

            dest = out_pbw_dir / pbl_stem
            if dest.exists() and not dest.is_symlink():
                ensure_removed(dest)

            created = try_create_junction(dest, cache_pbl_dir)
            if not created:
                copy_tree_incremental(cache_pbl_dir, dest)

    try:
        save_json(manifest_path, manifest)
    except PermissionError:
        print(f"[AICodebase][ERROR] Failed saving manifest for version {version}: {manifest_path}")
        return 1
    set_hidden_windows(manifest_path)
    return 0


def main(argv: List[str]) -> int:
    """
    Usage:
      extract_aicodebase.py <mirror_root> <sources_root> <output_root>
      extract_aicodebase.py <mirror_root> <sources_root> <output_root> all
      extract_aicodebase.py <mirror_root> <sources_root> <output_root> version <v>
      extract_aicodebase.py <mirror_root> <sources_root> <output_root> project <v> <pbw>

    Always incremental:
      - Builds a shared cache per PBL:  Extraction/AICodebase/<version>/.pblcache/<PBL>/...
      - Assembles PBWs using junctions (or copy fallback):
            Extraction/AICodebase/<version>/<PBW>/<PBL> -> junction to .pblcache/<PBL>
      - Removes outputs not present in the current mirror model.
    """
    if len(argv) < 4:
        print("Usage: extract_aicodebase.py <mirror_root> <sources_root> <output_root>")
        return 2

    mirror_root = Path(argv[1])
    sources_root = Path(argv[2])
    output_root = Path(argv[3])

    mode, target_version, target_project, parse_err = parse_scope_args(argv)
    if parse_err is not None or mode is None:
        print(parse_err or "[AICodebase][ERROR] Invalid arguments.")
        return 2

    if target_version is not None and target_version not in VERSIONS:
        print(f"[AICodebase][ERROR] Unsupported version: {target_version}")
        print(f"[AICodebase][ERROR] Supported versions: {', '.join(VERSIONS)}")
        return 2

    warnings: List[str] = []
    versions_to_process = [target_version] if target_version is not None else list(VERSIONS)
    project_filter = target_project if mode == "project" else None

    for version in versions_to_process:
        rc = process_version(
            version=version,
            mirror_root=mirror_root,
            sources_root=sources_root,
            output_root=output_root,
            warnings=warnings,
            project_filter=project_filter,
        )
        if rc != 0:
            return rc

    if WARNINGS_ENABLED and warnings:
        print("\n[AICodebase] Warnings summary:")
        for w in warnings:
            print(w)

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))