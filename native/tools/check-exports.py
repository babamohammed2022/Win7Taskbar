#!/usr/bin/env python3
"""Controllo degli export della DLL rispetto al codice sorgente.

Perche' esiste (v2.62): la tabella degli export puo' essere INCOMPLETA senza
che nessuno se ne accorga. E' successo davvero: la DLL in dist/ era stata
ricostruita con lo stesso comando di sempre ma senza nove funzioni
(W7T_IsWindows11, le funzioni della lingua, ...). Il compilatore non dice
niente, il collegamento non dice niente, il programma si avvia - e al primo
clic che passa da una di quelle funzioni il livello gestito prende un
EntryPointNotFoundException, che per l'utente e' "non funziona".

Il controllo e' diretto: si leggono i nomi delle funzioni esportate dai
sorgenti in native/src/ e si verifica che ognuno compaia nella tabella degli
export della DLL. In piu' segnala gli export che nel sorgente non esistono
piu' (avanzaglio di una build vecchia).

Uso:
    python3 native/tools/check-exports.py dist/Win7TaskbarCore.dll
"""

import re
import struct
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"

# Le funzioni pubbliche sono dichiarate con W7T_API (__declspec(dllexport))
# oppure direttamente con __declspec(dllexport), dentro extern "C".
PATTERNS = [
    re.compile(r'extern\s+"C"[^;{]{0,120}?\b(W7T_[A-Za-z0-9_]+)\s*\('),
    re.compile(r'^\s*W7T_API\b[^;{]{0,120}?\b(W7T_[A-Za-z0-9_]+)\s*\(',
               re.MULTILINE),
]

# Funzioni interne che iniziano per W7T_ ma non sono export.
NON_EXPORT = re.compile(r'^W7T_(Internal|Test)')


def exported_from_source() -> dict[str, str]:
    """nome -> file che lo dichiara"""
    out: dict[str, str] = {}
    for path in sorted(SRC.glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for pattern in PATTERNS:
            for name in pattern.findall(text):
                if not NON_EXPORT.match(name):
                    out.setdefault(name, path.name)
    return out


def exported_from_dll(path: Path) -> set[str]:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise SystemExit(f"ERRORE: {path} non e' un eseguibile PE")

    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SystemExit(f"ERRORE: {path} non e' un eseguibile PE")

    coff = pe + 4
    sections = struct.unpack_from("<H", data, coff + 2)[0]
    options = coff + 20
    magic = struct.unpack_from("<H", data, options)[0]
    directories = options + (96 if magic == 0x10B else 112)
    export_rva = struct.unpack_from("<I", data, directories)[0]

    table = []
    for index in range(sections):
        header = options + (224 if magic == 0x10B else 240) + index * 40
        name = data[header:header + 8].rstrip(b"\0").decode(errors="replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data,
                                                           header + 8)
        table.append((vaddr, vsize, rawptr, rawsize, name))

    def to_offset(rva: int) -> int:
        for vaddr, vsize, rawptr, rawsize, _ in table:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + (rva - vaddr)
        raise ValueError(hex(rva))

    header = to_offset(export_rva)
    count = struct.unpack_from("<I", data, header + 24)[0]
    names_rva = struct.unpack_from("<I", data, header + 32)[0]
    names = to_offset(names_rva)

    out = set()
    for index in range(count):
        name_rva = struct.unpack_from("<I", data, names + index * 4)[0]
        offset = to_offset(name_rva)
        end = data.index(b"\0", offset)
        out.add(data[offset:end].decode(errors="replace"))
    return out


def main() -> int:
    if len(sys.argv) < 2:
        print("uso: check-exports.py <percorso della DLL>")
        return 2

    dll = Path(sys.argv[1])
    if not dll.is_file():
        print(f"ERRORE: file non trovato: {dll}")
        return 2

    wanted = exported_from_source()
    have = exported_from_dll(dll)

    missing = sorted(name for name in wanted if name not in have)
    extra = sorted(name for name in have
                   if name.startswith("W7T_") and name not in wanted)

    print(f"sorgenti: {len(wanted)} funzioni esportate dichiarate")
    print(f"{dll}: {len([n for n in have if n.startswith('W7T_')])} export W7T_")

    for name in missing:
        print(f"  MANCANTE  {name}  (dichiarata in {wanted[name]})")
    for name in extra:
        print(f"  ORFANO    {name}  (non piu' nel sorgente)")

    if missing:
        print("\nLa DLL NON contiene tutte le funzioni del sorgente: "
              "il livello gestito fallirebbe.")
        return 1

    print("\nTutte le funzioni esportate dal sorgente sono nella DLL.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
