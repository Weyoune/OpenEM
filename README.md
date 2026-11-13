# OpenEM

OpenEM ist ein Nachbau des DOS-Spiels *Eishockey Manager* von 1993. Es spielt sich wie das Original: dieselben
Bildschirme mit 320×240 Pixeln und den ursprünglichen Grafiken, Schriften und Klängen, dieselben Menüs und Bedienwege
und dieselben Spielregeln. Die Regeln sind zugexakt gegen das Original geprüft: Aus demselben Spielstand mit denselben
Zufallswerten entstehen dieselben Spiele, Transfers, Finanzen und Saisonwechsel.

OpenEM enthält keine Daten des Originals. Man braucht eine eigene Kopie des DOS-Spiels: den Ordner mit `EM.EXE`
und den Unterordnern `KB` (Bilder) und `TORE` (Torszenen).

## Umfang

- Eine vollständige Karriere mit ein bis vier Managern an einem Rechner: Liga, DEB-Pokal, EuroCup, Play-offs,
  Abstiegsrunde, Weltmeisterschaft, Saisonwechsel, Transfers, Leihe, Jugend, Stadion, Werbung, Bank und Aktien.
- Die Spielansicht mit Anzeigetafeln, Torszenen und Live-Steuerung (Einsatz, Offensive, Reihen, Aufstellung und
  Taktik von der Tafel) für Liga-, Pokal-, Play-off-, EuroCup- und WM-Spiele.
- Der Modus „Nur WM“: als Bundestrainer durch die Weltmeisterschaften, mit Freundschaftsspielen zur Vorbereitung.
- Klänge und Musik des Originals (VOC-Samples und CMF-Musik auf einem nachgebildeten OPL2).
- Spielstände im eigenen Format (`.oem`, mit Zufallszustand) und im Format des Originals (`.hok`).
- `em_editor`: ein Editor für die Originaldaten (Bilder, Torszenengrafik, Paletten, Hallenkarten, Mannschaften).

## Bauen

Voraussetzungen: CMake ab 3.24 und ein C++20-Compiler (Visual Studio 2022 oder neuer, GCC, Clang). SDL2 lädt CMake
beim Konfigurieren selbst herunter; mit `-DEM_USE_SYSTEM_SDL2=ON` wird stattdessen ein installiertes SDL2 benutzt.

```bash
cmake -S . -B build
cmake --build build --config Release
cmake --install build --config Release --prefix <Release-Ordner>
```

## Starten

```bash
openem --data "<Pfad zum EM-Ordner>"
```

Ohne `--data` sucht OpenEM die Originaldaten in dieser Reihenfolge:

1. die Umgebungsvariable `OPENEM_DATA`;
2. der Programmordner selbst, dann seine Unterordner `EM` und `data`;
3. der aktuelle Ordner, dann `EM` und `data` darin.

Findet OpenEM nichts, erscheint eine Meldung mit diesen Möglichkeiten. Liegt im Datenordner ein `START.HOK`, wird
er geladen; sonst beginnt OpenEM mit dem Startmenü. `--new-game` öffnet immer das Startmenü eines neuen Spiels.

Spielstände liegen im Benutzer-Datenordner (`%APPDATA%\OpenEM\OpenEM\saves` unter Windows,
`~/.local/share/OpenEM/OpenEM/saves` unter Linux). Im Dateidialog des Spiels sind sie über `D:` erreichbar.
Bestehende Speicherdateien werden nicht überschrieben.

## Tests

Die Tests vergleichen OpenEM mit dem Original und brauchen deshalb die Originaldaten und aufgezeichnete
Vergleichsdaten, die nicht zu diesem Repository gehören. Wer Zugang zum privaten Testarchiv hat, konfiguriert mit
`-DOPENEM_PRIVATE_DIR=<Checkout>`; `ctest --test-dir build -C Release` führt die Tests dann aus.

## Editor

`em_editor` (Windows, im Release neben `openem`) bearbeitet die Originaldaten: Bilder, Torszenengrafik, Paletten,
Hallenkarten und die Mannschaften (Namen, Wappen, Stärken, Trikotfarben). Er sucht die Daten wie `openem` und legt
vor dem ersten Überschreiben eine `.bak` an. Bedienung: [docs/EDITOR.md](docs/EDITOR.md).

## Quellcode

Der Quellcode ist an Ort und Stelle dokumentiert; [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) gibt den Überblick.

## Lizenz

Der Quellcode von OpenEM steht unter der GNU General Public License, Version 3 (nur diese Version,
`GPL-3.0-only`); der Text liegt in [LICENSE](LICENSE). Die Daten des Original-Spiels gehören nicht dazu und werden
nicht mitgeliefert.
