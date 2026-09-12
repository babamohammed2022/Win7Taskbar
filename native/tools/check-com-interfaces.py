#!/usr/bin/env python3
"""Controllo di sicurezza sulle interfacce COM dichiarate a mano.

Se un'interfaccia COM viene dichiarata dentro un namespace anonimo, il
compilatore deduce che nessuna classe visibile la implementa e sostituisce
ogni chiamata ai suoi metodi con __cxa_pure_virtual, cioe' con
l'interruzione del processo. Il codice compila senza un solo avviso e la
funzionalita' smette di funzionare in silenzio.

Questo script rilegge la DLL prodotta e verifica che gli identificatori
delle interfacce che ci aspettiamo siano davvero finiti nel binario: se il
compilatore ha eliminato quel percorso, gli identificatori spariscono
insieme a esso.
"""
import struct
import sys
from pathlib import Path

ATTESI = [
    ("CLSID ImmersiveShell",    0xC2F03A33, 0x21F5, 0x47FA,
     [0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39]),
    ("CLSID ExperienceFactory", 0x2E8FCB18, 0xA0EE, 0x41AD,
     [0x8E, 0xF8, 0x77, 0xFB, 0x3A, 0x37, 0x0C, 0xA5]),
    ("IID riquadro orologio",   0xB1604325, 0x6B59, 0x427B,
     [0xBF, 0x1B, 0x80, 0xA2, 0xDB, 0x02, 0xD3, 0xD8]),
    ("IID riquadro volume",     0x7154C95D, 0xC519, 0x49BD,
     [0xA9, 0x7E, 0x64, 0x5B, 0xBF, 0xAB, 0xE1, 0x11]),
    ("IID riquadro rete",       0xC9DDC674, 0xB44B, 0x4C67,
     [0x9D, 0x79, 0x2B, 0x23, 0x7D, 0x9B, 0xE0, 0x5A]),
    ("IID riquadro batteria",   0x0A73AEDC, 0x1C68, 0x410D,
     [0x8D, 0x53, 0x63, 0xAF, 0x80, 0x95, 0x1E, 0x8F]),
    ("CLSID Aero Clock",        0xA323554A, 0x0FE1, 0x4E49,
     [0xAE, 0xE1, 0x67, 0x22, 0x46, 0x5D, 0x79, 0x9F]),
    ("IID Aero Clock",          0x7A5FCA8A, 0x76B1, 0x44C8,
     [0xA9, 0x7C, 0xE7, 0x17, 0x3C, 0xCA, 0x5F, 0x4F]),
]


def main() -> int:
    if len(sys.argv) < 2:
        print("uso: check-com-interfaces.py <percorso della DLL>")
        return 2

    dll = Path(sys.argv[1])
    if not dll.is_file():
        print(f"ERRORE: file non trovato: {dll}")
        return 2

    data = dll.read_bytes()
    mancanti = []

    for nome, d1, d2, d3, coda in ATTESI:
        blob = struct.pack("<IHH", d1, d2, d3) + bytes(coda)
        if blob in data:
            print(f"  ok       {nome}")
        else:
            print(f"  MANCANTE {nome}")
            mancanti.append(nome)

    if mancanti:
        print()
        print("Identificatori COM spariti dal binario: " + ", ".join(mancanti))
        print("Causa tipica: l'interfaccia e' stata dichiarata dentro un")
        print("namespace anonimo e il compilatore ne ha eliminato le chiamate,")
        print("sostituendole con il gestore di errore __cxa_pure_virtual.")
        print("Rimedio: spostare la dichiarazione fuori dal namespace anonimo.")
        return 1

    print()
    print("Tutte le interfacce COM attese sono presenti nel binario.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
