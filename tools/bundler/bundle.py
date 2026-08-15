#!/usr/bin/env python3
"""
TD Engine — EXE Bundler (Inno Setup-style installer generator)

Takes a finished web game (index.html + JS + assets + the engine WASM runtime)
and produces a Windows installer .exe using Inno Setup.

USAGE
    python tools/bundler/bundle.py \\
        --game path/to/my-game \\
        --name "My Cool Game" \\
        --version 1.0.0 \\
        --publisher "Some Studio" \\
        --icon my-game.ico \\
        --out MyCoolGame-Setup.exe

WHAT IT DOES
    1. Validates the game folder (needs index.html).
    2. Stages files into a build/ directory:
         build/host/    <- the WebView2 host .exe (renamed to the game's exe)
         build/runtime/ <- td-engine.js, td-engine.wasm, js_bridge.js, *.js
         build/game/    <- the user's game files (recursive)
         build/webview2/ <- optional WebView2 bootstrapper (if --bundle-runtime)
    3. Reads installer/td-game-template.iss, substitutes {{PLACEHOLDERS}}.
    4. Invokes ISCC.exe on the generated .iss.
    5. Copies the resulting setup .exe to --out.

PREREQUISITES
    - Inno Setup 6+ installed (ISCC.exe on PATH, or pass --iscc /path/ISCC.exe)
      Download: https://jrsoftware.org/isdl.php
    - The host shell built (cmake --build build --target td-host)
      Or pass --host-exe /path/to/td-host.exe
    - The web build done (make web) so web/td-engine.js + .wasm exist
      Or pass --web-dir /path/to/web

CUSTOM INSTALLER SCRIPT
    If your game folder contains `installer.iss`, it is used instead of the
    default template. This lets you add custom [Run]/[UninstallRun]/[Code]
    steps specific to your game.

PROJECT FILE
    If your game folder contains `game.tdproj` (JSON), it is read for default
    values of name / version / publisher / id / icon / url. CLI args override.

      {
        "name": "My Cool Game",
        "version": "1.0.0",
        "publisher": "Some Studio",
        "id": "my-cool-game",          // optional, derived from name if absent
        "icon": "assets/icon.ico",     // optional
        "url": "https://example.com",  // optional
        "bundle_runtime": false         // optional, default false
      }
"""

from __future__ import annotations
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# =============================================================================
#  Constants
# =============================================================================

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TEMPLATE = REPO_ROOT / "installer" / "td-game-template.iss"
DEFAULT_WEB_DIR = REPO_ROOT / "web"
DEFAULT_HOST_EXE = REPO_ROOT / "build" / "bin" / "td-host.exe"

# Files that constitute the engine runtime (copied into runtime/ of the bundle)
RUNTIME_FILES = [
    "td-engine.js",   # Emscripten glue (build artifact)
    "td-engine.wasm", # compiled C++ engine (build artifact)
    "js_bridge.js",   # TDBridge global
    "td_api.js",      # TDEngine.* namespace
    "net_websocket.js",
    "net_peer.js",
    "server_router.js",
    "inspector.js",
    "profiler.js",
    "persistence.js",
    "error_boundary.js",
    "deprecated_tracker.js",
]

# Files in the game folder that should NOT be copied into game/
GAME_EXCLUDE = {
    "installer.iss",   # custom installer script (read, not copied)
    "game.tdproj",     # project file (read, not copied)
    ".td-installed",   # marker file (if dev-installed)
    "Thumbs.db",
    ".DS_Store",
}

# =============================================================================
#  Helpers
# =============================================================================

def log(msg: str) -> None:
    """Info log to stderr (so stdout stays clean for --version etc)."""
    print(f"[bundler] {msg}", file=sys.stderr)


def die(msg: str, code: int = 1) -> None:
    """Error log + exit."""
    print(f"[bundler] ERROR: {msg}", file=sys.stderr)
    sys.exit(code)


def slugify(name: str) -> str:
    """Convert a human name to a filesystem/registry slug.

    "My Cool Game" -> "my-cool-game"
    "Void Runner 2!" -> "void-runner-2"
    """
    s = name.lower().strip()
    s = re.sub(r"[^a-z0-9]+", "-", s)
    s = re.sub(r"^-+|-+$", "", s)
    return s or "td-game"


def exe_name_for(name: str) -> str:
    """Convert a human name to an .exe filename.

    "My Cool Game" -> "MyCoolGame.exe"
    """
    s = re.sub(r"[^A-Za-z0-9]+", "", name)
    if not s:
        s = "TDGame"
    return s + ".exe"


def find_iscc(explicit: str | None) -> str:
    """Locate ISCC.exe. Checks: explicit arg, PATH, common install paths."""
    if explicit:
        p = Path(explicit)
        if p.is_file():
            return str(p)
        die(f"--iscc path does not exist: {explicit}")

    # On PATH?
    found = shutil.which("ISCC") or shutil.which("iscc")
    if found:
        return found

    # Common Windows install paths
    candidates = [
        r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        r"C:\Program Files\Inno Setup 6\ISCC.exe",
        os.path.expandvars(r"%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"),
    ]
    for c in candidates:
        if Path(c).is_file():
            return c

    die(
        "Inno Setup compiler (ISCC.exe) not found.\n"
        "  Install Inno Setup 6+ from https://jrsoftware.org/isdl.php\n"
        "  or pass --iscc /path/to/ISCC.exe"
    )


# =============================================================================
#  Project file
# =============================================================================

def load_tdproj(game_dir: Path) -> dict:
    """Read game.tdproj if it exists. Returns {} if absent."""
    p = game_dir / "game.tdproj"
    if not p.is_file():
        return {}
    try:
        with p.open("r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            die(f"{p}: expected a JSON object at the top level")
        log(f"Loaded project file: {p}")
        return data
    except json.JSONDecodeError as e:
        die(f"{p}: invalid JSON — {e}")


# =============================================================================
#  Staging
# =============================================================================

def stage_host(staging: Path, host_exe: Path, target_exe_name: str) -> None:
    """Copy the host .exe into staging/host/, renamed to the game's exe name."""
    if not host_exe.is_file():
        die(
            f"Host .exe not found: {host_exe}\n"
            "  Build it with: cmake --build build --target td-host\n"
            "  or pass --host-exe /path/to/td-host.exe"
        )
    dst = staging / "host" / target_exe_name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(host_exe, dst)
    log(f"Staged host: {dst.relative_to(staging)}")


def stage_runtime(staging: Path, web_dir: Path, runtime_files: list[str]) -> None:
    """Copy the engine runtime files into staging/runtime/."""
    dst = staging / "runtime"
    dst.mkdir(parents=True, exist_ok=True)
    missing = []
    for name in runtime_files:
        src = web_dir / name
        if not src.is_file():
            missing.append(name)
            continue
        shutil.copy2(src, dst / name)
    if missing:
        log(f"Note: skipped {len(missing)} runtime file(s) not present in {web_dir}:")
        for m in missing:
            log(f"  - {m}")
        # td-engine.js + td-engine.wasm + js_bridge.js are REQUIRED
        for required in ("td-engine.js", "td-engine.wasm", "js_bridge.js"):
            if required in missing:
                die(
                    f"Required runtime file missing: {required}\n"
                    f"  Run `make web` first to build the WASM engine, or pass --web-dir."
                )
    log(f"Staged runtime: {dst.relative_to(staging)} ({len(runtime_files) - len(missing)} files)")


def stage_game(staging: Path, game_dir: Path) -> None:
    """Copy the user's game files into staging/game/."""
    dst = staging / "game"
    dst.mkdir(parents=True, exist_ok=True)

    if not (game_dir / "index.html").is_file():
        die(f"Game folder must contain index.html: {game_dir}")

    count = 0
    for root, dirs, files in os.walk(game_dir):
        # Skip hidden dirs + node_modules + .git
        dirs[:] = [d for d in dirs if not d.startswith(".") and d != "node_modules"]
        for fname in files:
            if fname in GAME_EXCLUDE:
                continue
            src = Path(root) / fname
            rel = src.relative_to(game_dir)
            out = dst / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, out)
            count += 1
    log(f"Staged game: {dst.relative_to(staging)} ({count} files)")


def stage_webview2_bootstrapper(staging: Path, bootstrapper_path: Path | None) -> None:
    """If --bundle-runtime, copy the WebView2 bootstrapper into staging/webview2/."""
    if not bootstrapper_path:
        return
    if not bootstrapper_path.is_file():
        die(f"WebView2 bootstrapper not found: {bootstrapper_path}")
    dst = staging / "webview2" / "MicrosoftEdgeWebview2Setup.exe"
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(bootstrapper_path, dst)
    log(f"Staged WebView2 bootstrapper: {dst.relative_to(staging)}")


# =============================================================================
#  ISS template substitution
# =============================================================================

def render_iss(
    template: Path,
    out_path: Path,
    subst: dict,
    bundle_runtime: bool,
    license_file: Path | None,
    readme_file: Path | None,
) -> None:
    """Read the .iss template, substitute placeholders, write the result.

    Two kinds of placeholders:
      1. {{KEY}}  -> simple string substitution (from `subst` dict)
      2. {{GUARD_BEGIN}} ... {{GUARD_END}} -> conditional blocks, kept or
         stripped based on matching key in `conditional` set.
    """
    with template.open("r", encoding="utf-8") as f:
        text = f.read()

    # ---- Conditional blocks ---------------------------------------------------
    # Pattern: {{BLOCK_NAME_BEGIN}} ... {{BLOCK_NAME_END}}
    # If the block should be kept, replace the markers with empty string.
    # If it should be stripped, remove everything between (inclusive).
    # NOTE: the character class includes 0-9 because some block names contain
    # digits (e.g. WEBVIEW2_CHECK_GUARD).
    cond_pattern = re.compile(
        r"\{\{([A-Z0-9_]+)_BEGIN\}\}(.*?)\{\{\1_END\}\}",
        re.DOTALL,
    )

    def cond_repl(m: re.Match) -> str:
        name = m.group(1)
        keep = False
        if name == "WEBVIEW2_CHECK_GUARD":
            # Keep the WebView2 runtime check ONLY if we are NOT bundling
            # the bootstrapper (bundling handles it via [Setup] directive).
            keep = not bundle_runtime
        return m.group(2) if keep else ""

    text = cond_pattern.sub(cond_repl, text)

    # ---- Simple placeholders --------------------------------------------------
    for key, val in subst.items():
        text = text.replace("{{" + key + "}}", val)

    # ---- WebView2 bootstrapper line (in [Setup]) ------------------------------
    if bundle_runtime:
        text = text.replace(
            "{{WEBVIEW2_BOOTSTRAPPER_LINE}}",
            "WebView2Installer=webview2\\MicrosoftEdgeWebview2Setup.exe\n"
            "WebView2InstallerArgs=/silent /install",
        )
        text = text.replace(
            "{{WEBVIEW2_FILES_LINE}}",
            'Source: "webview2\\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; '
            'Flags: deleteafterinstall',
        )
    else:
        text = text.replace("{{WEBVIEW2_BOOTSTRAPPER_LINE}}", "")
        text = text.replace("{{WEBVIEW2_FILES_LINE}}", "")

    # ---- License / README lines -----------------------------------------------
    if license_file and license_file.is_file():
        text = text.replace(
            "{{LICENSE_FILE_LINE}}",
            f'Source: "{license_file.name}"; DestDir: "{{app}}"; '
            f'Flags: ignoreversion; Components: docs',
        )
    else:
        text = text.replace("{{LICENSE_FILE_LINE}}", "; (no license file)")

    if readme_file and readme_file.is_file():
        text = text.replace(
            "{{README_FILE_LINE}}",
            f'Source: "{readme_file.name}"; DestDir: "{{app}}"; '
            f'Flags: ignoreversion isreadme; Components: docs',
        )
    else:
        text = text.replace("{{README_FILE_LINE}}", "; (no readme file)")

    # ---- OutputDir (absolute, so ISCC doesn't care about cwd) -----------------
    out_dir = out_path.parent.resolve()
    text = text.replace("OutputBaseFilename=", f"OutputDir={out_dir}\nOutputBaseFilename=")

    with out_path.open("w", encoding="utf-8", newline="\r\n") as f:
        f.write(text)
    log(f"Wrote installer script: {out_path}")


# =============================================================================
#  Main
# =============================================================================

def main() -> int:
    ap = argparse.ArgumentParser(
        prog="bundle.py",
        description="TD Engine — Inno Setup-style EXE bundler",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--game", required=True, type=Path,
                    help="Path to the game folder (must contain index.html)")
    ap.add_argument("--name", help="Game display name (e.g. 'My Cool Game')")
    ap.add_argument("--version", help="Game version (e.g. '1.0.0')")
    ap.add_argument("--publisher", help="Publisher name (e.g. 'Some Studio')")
    ap.add_argument("--id", dest="app_id",
                    help="App ID slug (e.g. 'my-cool-game'). Derived from --name if absent.")
    ap.add_argument("--icon", type=Path, help="Path to .ico file (optional)")
    ap.add_argument("--url", help="Publisher URL (optional)")
    ap.add_argument("--out", type=Path, default=Path("MyGame-Setup.exe"),
                    help="Output .exe path (default: ./MyGame-Setup.exe)")
    ap.add_argument("--host-exe", type=Path, default=DEFAULT_HOST_EXE,
                    help=f"Path to the WebView2 host .exe (default: {DEFAULT_HOST_EXE})")
    ap.add_argument("--web-dir", type=Path, default=DEFAULT_WEB_DIR,
                    help=f"Path to the web/ folder (default: {DEFAULT_WEB_DIR})")
    ap.add_argument("--staging", type=Path, default=None,
                    help="Staging directory (default: build/bundle-<id>/)")
    ap.add_argument("--template", type=Path, default=None,
                    help="Path to .iss template (default: installer/td-game-template.iss)")
    ap.add_argument("--iscc", default=None,
                    help="Path to ISCC.exe (default: search PATH + common paths)")
    ap.add_argument("--bundle-runtime", action="store_true",
                    help="Bundle the WebView2 bootstrapper (~2MB) for offline install")
    ap.add_argument("--webview2-bootstrapper", type=Path, default=None,
                    help="Path to MicrosoftEdgeWebview2Setup.exe (required with --bundle-runtime)")
    ap.add_argument("--license", type=Path, default=None,
                    help="Path to LICENSE file (optional)")
    ap.add_argument("--readme", type=Path, default=None,
                    help="Path to README file (optional)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Stage + generate .iss but don't invoke ISCC")
    ap.add_argument("--keep-staging", action="store_true",
                    help="Don't delete the staging dir after building (for debugging)")
    args = ap.parse_args()

    # ---- Merge with game.tdproj (CLI args win) --------------------------------
    tdproj = load_tdproj(args.game)
    name = args.name or tdproj.get("name") or args.game.name
    version = args.version or tdproj.get("version") or "1.0.0"
    publisher = args.publisher or tdproj.get("publisher") or "Unknown Publisher"
    app_id = args.app_id or tdproj.get("id") or slugify(name)
    icon = args.icon or (Path(tdproj["icon"]) if "icon" in tdproj else None)
    url = args.url or tdproj.get("url", "")
    bundle_runtime = args.bundle_runtime or tdproj.get("bundle_runtime", False)
    bootstrapper = args.webview2_bootstrapper

    if bundle_runtime and not bootstrapper:
        die("--bundle-runtime requires --webview2-bootstrapper PATH")

    # ---- Resolve paths --------------------------------------------------------
    game_dir = args.game.resolve()
    if not game_dir.is_dir():
        die(f"Game folder does not exist: {game_dir}")
    if not (game_dir / "index.html").is_file():
        die(f"Game folder must contain index.html: {game_dir}")

    host_exe = args.host_exe.resolve()
    web_dir = args.web_dir.resolve()
    template = (args.template or (game_dir / "installer.iss") if (game_dir / "installer.iss").is_file() else (args.template or DEFAULT_TEMPLATE)).resolve()
    if not template.is_file():
        die(f"Installer template not found: {template}")

    staging = (args.staging or (REPO_ROOT / "build" / f"bundle-{app_id}")).resolve()
    if staging.exists():
        log(f"Cleaning existing staging: {staging}")
        shutil.rmtree(staging)
    staging.mkdir(parents=True, exist_ok=True)

    target_exe_name = exe_name_for(name)

    # ---- Resolve icon ---------------------------------------------------------
    icon_source = "host.exe"  # default: use the host exe's embedded icon
    if icon:
        icon = icon.resolve()
        if not icon.is_file():
            die(f"Icon file not found: {icon}")
        # Copy icon into staging so ISCC can find it
        shutil.copy2(icon, staging / "app.ico")
        icon_source = str(staging / "app.ico")

    # ---- Stage files ----------------------------------------------------------
    log(f"Staging into: {staging}")
    stage_host(staging, host_exe, target_exe_name)
    stage_runtime(staging, web_dir, RUNTIME_FILES)
    stage_game(staging, game_dir)
    stage_webview2_bootstrapper(staging, bootstrapper)

    # ---- Render .iss ----------------------------------------------------------
    subst = {
        "APP_NAME": name,
        "APP_VERSION": version,
        "APP_PUBLISHER": publisher,
        "APP_ID": app_id,
        "APP_EXE": target_exe_name,
        "APP_ICON": icon_source,
        "APP_URL": url,
    }
    iss_path = staging / f"{app_id}.iss"
    render_iss(
        template=template,
        out_path=iss_path,
        subst=subst,
        bundle_runtime=bundle_runtime,
        license_file=args.license,
        readme_file=args.readme,
    )

    # ---- Dry run? -------------------------------------------------------------
    if args.dry_run:
        log("Dry run — not invoking ISCC.")
        log(f"  Staging: {staging}")
        log(f"  Script:  {iss_path}")
        log(f"  To build: ISCC \"{iss_path}\"")
        return 0

    # ---- Invoke ISCC ----------------------------------------------------------
    iscc = find_iscc(args.iscc)
    log(f"Invoking Inno Setup compiler: {iscc}")
    cmd = [iscc, str(iss_path)]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    except FileNotFoundError:
        die(f"Could not execute ISCC: {iscc}")

    if proc.stdout:
        print(proc.stdout, end="", file=sys.stderr)
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)

    if proc.returncode != 0:
        die(f"ISCC failed with exit code {proc.returncode}")

    # ---- Find + move the output ----------------------------------------------
    setup_exe = staging.parent / f"{app_id}-setup.exe"
    # ISCC writes to OutputDir (which we set to staging.parent) with
    # OutputBaseFilename={app_id}-setup
    if not setup_exe.is_file():
        # Search a few likely locations
        for candidate in [
            staging / f"{app_id}-setup.exe",
            staging.parent / f"{app_id}-setup.exe",
        ]:
            if candidate.is_file():
                setup_exe = candidate
                break
        else:
            die(f"Setup .exe not found after ISCC. Expected: {setup_exe}")

    out_path = args.out.resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(setup_exe, out_path)
    log(f"Built installer: {out_path}")
    log(f"  Size: {out_path.stat().st_size / (1024*1024):.1f} MB")

    # ---- Cleanup --------------------------------------------------------------
    if not args.keep_staging:
        log(f"Cleaning staging: {staging}")
        shutil.rmtree(staging, ignore_errors=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
