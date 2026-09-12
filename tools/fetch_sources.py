#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Win7Taskbar - scaricatore automatico dei bundle MediaFire.

Serve a recuperare i file originali del progetto (sorgenti + build distribuita)
dal link MediaFire indicato dal maintainer, ed eventualmente a estrarli
direttamente dentro il repository, cosi' da poter essere committati/pushati
con un solo comando.

Non richiede dipendenze esterne: usa solo la libreria standard di Python 3.8+.

Esempi tipici
-------------
Scarica ed estrae i sorgenti nella cartella ./_incoming:

    python tools/fetch_sources.py --extract

Scarica, estrae e committa/pusha automaticamente sul branch corrente:

    python tools/fetch_sources.py --extract --commit-push

Scarica solo un file:

    python tools/fetch_sources.py --only win7taskbarforgithub.zip
"""

from __future__ import annotations

import argparse
import http.cookiejar
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
)

# Bundle pubblicati su MediaFire dal maintainer del progetto.
SOURCES = {
    # nome file            url pagina MediaFire
    "win7taskbarforgithub.zip": "https://www.mediafire.com/file/erka1phr202p1nz/win7taskbarforgithub.zip/file",
    "Win7Taskbar-v2.58-win-x64.zip": "https://www.mediafire.com/file/wyfsjmgwvg0bohc/Win7Taskbar-v2.58-win-x64.zip/file",
}

# Alcuni file binari del progetto (DLL native, icone, ecc.) sono esclusi da
# .gitignore: quando --commit-push viene usato li aggiungiamo con --force.
FORCE_ADD_EXTENSIONS = (".dll", ".lib", ".exe", ".pdb", ".ico", ".png", ".res", ".rc")


def log(msg: str) -> None:
    print(f"[fetch_sources] {msg}", flush=True)


def build_opener() -> urllib.request.OpenerDirector:
    jar = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
    opener.addheaders = [
        ("User-Agent", USER_AGENT),
        ("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"),
        ("Accept-Language", "en-US,en;q=0.9,it;q=0.8"),
        ("Referer", "https://www.mediafire.com/"),
    ]
    return opener


def http_get(opener, url: str, retries: int = 4, timeout: int = 60) -> bytes:
    last_error: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            with opener.open(url, timeout=timeout) as response:
                return response.read()
        except Exception as exc:  # noqa: BLE001 - vogliamo riprovare su qualunque errore di rete
            last_error = exc
            log(f"tentativo {attempt}/{retries} fallito su {url}: {exc}")
            time.sleep(2 * attempt)
    raise RuntimeError(f"impossibile scaricare {url}: {last_error}")


def find_direct_link(html: bytes) -> str | None:
    """Estrae il link di download diretto dalla pagina MediaFire."""
    text = html.decode("utf-8", errors="replace")

    # 1) Il pulsante verde "Download" della pagina.
    match = re.search(
        r'id="downloadButton"[^>]*href="([^"]+)"', text, re.IGNORECASE | re.DOTALL
    )
    if match:
        return match.group(1)

    # 2) Qualunque link diretto al CDN di MediaFire.
    match = re.search(r'https://download[0-9]*\.mediafire\.com/[^"\'<>\s]+', text)
    if match:
        return match.group(0)

    # 3) Pagine intermedie ("popsok"):
    match = re.search(r'href="(https://www\.mediafire\.com/[^"]*(?:dkey=|download)[^"]*)"', text)
    if match:
        return match.group(1)

    return None


def human(size: float) -> str:
    for unit in ("B", "KiB", "MiB", "GiB"):
        if size < 1024 or unit == "GiB":
            return f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} GiB"


def download(opener, page_url: str, destination: Path) -> Path:
    log(f"pagina MediaFire: {page_url}")
    html = http_get(opener, page_url)
    link = find_direct_link(html)
    if not link:
        debug = destination.with_suffix(".page.html")
        debug.write_bytes(html)
        raise RuntimeError(
            "link di download non trovato nella pagina MediaFire "
            f"(pagina salvata in {debug} per diagnosi)"
        )

    log(f"link diretto: {link}")
    request = urllib.request.Request(link, headers={"User-Agent": USER_AGENT, "Referer": page_url})
    with opener.open(request, timeout=120) as response, destination.open("wb") as handle:
        total = int(response.headers.get("Content-Length") or 0)
        downloaded = 0
        started = time.time()
        while True:
            chunk = response.read(256 * 1024)
            if not chunk:
                break
            handle.write(chunk)
            downloaded += len(chunk)
            if total:
                pct = downloaded * 100 / total
                sys.stdout.write(f"\r  {human(downloaded)} / {human(total)} ({pct:5.1f}%)")
            else:
                sys.stdout.write(f"\r  {human(downloaded)}")
            sys.stdout.flush()
    sys.stdout.write(
        f"\r  completato: {human(destination.stat().st_size)} in {time.time() - started:.1f}s\n"
    )
    return destination


def verify_and_extract(archive: Path, destination: Path) -> None:
    with zipfile.ZipFile(archive) as zf:
        bad = zf.testzip()
        if bad is not None:
            raise RuntimeError(f"archivio danneggiato ({bad}): {archive}")
        destination.mkdir(parents=True, exist_ok=True)
        zf.extractall(destination)
    log(f"estratto in {destination}")


def flatten_single_root(destination: Path) -> None:
    """Se lo zip contiene una sola cartella radice, ne promuove il contenuto."""
    entries = [p for p in destination.iterdir() if not p.name.startswith("__MACOSX")]
    if len(entries) != 1 or not entries[0].is_dir():
        return
    root = entries[0]
    log(f"rimuovo la cartella radice ridondante '{root.name}/'")
    for item in list(root.iterdir()):
        shutil.move(str(item), str(destination / item.name))
    root.rmdir()


def git(*args: str) -> None:
    log("git " + " ".join(args))
    subprocess.run(["git", *args], check=True)


def commit_and_push(destination: Path, branch: str | None, message: str) -> None:
    if shutil.which("git") is None:
        raise RuntimeError("git non trovato nel PATH")

    git("add", "-A", str(destination))
    # I binari sono esclusi da .gitignore: forziamo solo le estensioni note.
    for path in destination.rglob("*"):
        if path.is_file() and path.suffix.lower() in FORCE_ADD_EXTENSIONS:
            git("add", "-f", str(path))

    status = subprocess.run(
        ["git", "status", "--porcelain", str(destination)], capture_output=True, text=True
    )
    if not status.stdout.strip():
        log("nessuna modifica da committare")
        return

    git("commit", "-m", message)
    if branch:
        git("push", "origin", f"HEAD:{branch}")
    else:
        git("push")


def main() -> int:
    parser = argparse.ArgumentParser(description="Scarica i bundle MediaFire di Win7Taskbar")
    parser.add_argument("--out", default="_incoming", help="cartella di destinazione (default: _incoming)")
    parser.add_argument("--only", default=None, help="scarica solo il bundle con questo nome")
    parser.add_argument("--extract", action="store_true", help="estrai gli archivi scaricati")
    parser.add_argument(
        "--extract-root",
        action="store_true",
        help="estrai direttamente nella cartella di destinazione (senza sottocartella per bundle)",
    )
    parser.add_argument("--commit-push", action="store_true", help="committa e pusha le modifiche")
    parser.add_argument("--branch", default=None, help="branch di destinazione del push")
    parser.add_argument("--message", default="feat: import Win7Taskbar sources", help="messaggio di commit")
    args = parser.parse_args()

    destination = Path(args.out).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    opener = build_opener()

    targets = SOURCES
    if args.only:
        targets = {k: v for k, v in SOURCES.items() if k == args.only}
        if not targets:
            log(f"nessun bundle chiamato '{args.only}'. Disponibili: {', '.join(SOURCES)}")
            return 2

    for name, url in targets.items():
        archive = destination / name
        if archive.exists() and archive.stat().st_size > 1024:
            log(f"{name} presente, salto il download")
        else:
            download(opener, url, archive)
        if args.extract:
            target = destination if args.extract_root else destination / archive.stem
            verify_and_extract(archive, target)
            flatten_single_root(target)

    log(f"contenuto di {destination}:")
    for path in sorted(destination.iterdir()):
        log(f"  - {path.name}")

    if args.commit_push:
        commit_and_push(destination, args.branch, args.message)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print()
        raise SystemExit(130)
    except Exception as exc:  # noqa: BLE001
        log(f"ERRORE: {exc}")
        raise SystemExit(1)
