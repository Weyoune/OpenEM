# OpenEM

OpenEM rekonstruiert den Eishockey Manager von 1993. Die Oberfläche folgt dem DOS-Original: 320×240 Pixel, ursprüngliche Grafiken, Schriften, Farben, Fenster und Bedienwege. Die Spielregeln werden zugexakt gegen das Original geprüft (siehe unten). Weitere Originalseiten und Teile der Spielsteuerung fehlen noch.

## Was man braucht

OpenEM enthält keine Daten des Originals. Man braucht eine eigene Kopie des DOS-Spiels: den Ordner mit `EM.EXE` und den Unterordnern `KB` (Bilder) und `TORE` (Torszenen).

## Bauen

Voraussetzungen: CMake ab 3.24 und ein C++20-Compiler (Visual Studio 2022 oder neuer, GCC, Clang). SDL2 lädt CMake beim Konfigurieren selbst herunter; mit `-DEM_USE_SYSTEM_SDL2=ON` wird stattdessen ein installiertes SDL2 benutzt.

```bash
cmake -S . -B build -DEM_DATA="<Pfad zum EM-Ordner>"
cmake --build build --config Release
ctest --test-dir build -C Release
```

`EM_DATA` braucht nur, wer die Tests laufen lässt. `-DEM_REALCODE=ON` baut zusätzlich die REAL-CODE-Vergleichstests (Unicorn-Emulator, siehe [docs/REALCODE.md](docs/REALCODE.md)).

## Starten

```bash
openem --data "<Pfad zum EM-Ordner>"
```

Ohne `--data` sucht OpenEM die Originaldaten in dieser Reihenfolge:

1. die Umgebungsvariable `OPENEM_DATA`;
2. in einem Quellcode-Checkout `work/EM` oberhalb des Programms;
3. der Programmordner selbst, dann seine Unterordner `EM` und `data`;
4. der aktuelle Ordner, dann `EM` und `data` darin.

Findet OpenEM nichts, erscheint eine Meldung mit diesen Möglichkeiten. Liegt im Datenordner ein `START.HOK`, wird er geladen; sonst beginnt OpenEM mit dem Startmenü. `--new-game` öffnet immer das Startmenü eines neuen Spiels.

Spielstände: im Quellcode-Checkout unter `work/saves`, sonst im Benutzer-Datenordner (`%APPDATA%\OpenEM\OpenEM\saves` unter Windows, `~/.local/share/OpenEM/OpenEM/saves` unter Linux). Im ursprünglichen Dateidialog sind sie über `D:` erreichbar.

- `.oem` erhält den OpenEM-Fortsetzungszustand einschließlich beider Zufallsfolgen.
- `.hok` exportiert für das DOS-Original; dessen Format enthält diese Zufallsfolgen nicht.

Bestehende Speicherdateien werden nicht überschrieben.

## Editor

`em_editor` (Windows, liegt im Release neben `openem`) bearbeitet die Originaldaten: Bilder, Torszenen-Grafik, Paletten, Hallenkarten und die Mannschaften (Namen, Wappen, Stärken, Trikotfarben). Er sucht die Daten wie `openem` und legt vor dem ersten Überschreiben eine `.bak` an. Bedienung: [docs/EDITOR.md](docs/EDITOR.md). Ein Release-Ordner entsteht mit `cmake --install build --config Release --prefix <Ordner>`.

## Stand und Belege

[Bedienung und Grenzen](docs/CLASSIC_UI.md), [Projektstand](docs/STATUS.md), [Prüfungen](docs/TESTING.md), [Tagesvergleiche mit dem Original](docs/DAYDIFF.md), [Abweichungen](docs/DEVIATIONS.md) und [Checkpointformat](docs/formats/OPENEM_CHECKPOINT.md) beschreiben den aktuellen Umfang.

**Maßstab:** Spielregeln sind zugexakt: gleiche Zufallszüge, gleiche Ergebnisse, gleiche für den Spieler sichtbare Zahlen. Fehler und interne Puffer des DOS-Originals werden nur nachgebaut, wenn das mit vertretbarem Aufwand geht; sonst stehen sie mit Beleg in [docs/DEVIATIONS.md](docs/DEVIATIONS.md).

**Prüfung:** Das Original läuft im Emulator ab einem Spielstand bis zum nächsten Menü; OpenEM rechnet dieselben Tage und muss alle 47 761 Zustandsbytes und beide Zufallsfolgen gleich treffen. Inzwischen sind zwei Ketten über insgesamt mehr als zehn Spielzeiten abgedeckt (Liga, Pokal, EuroCup, Playoffs, Abstiegsrunde, WM, Saisonwechsel, Sommer; 387 Zeiträume, alle als Regressionstest, dazu ein ganzes Jahr im Modus „Nur WM“; Restabweichungen in docs/DEVIATIONS.md).

Eine vollständige Karriere mit bis zu vier Managern, die Live-Steuerung während des Spiels und einige Seiten fehlen noch (siehe [docs/STATUS.md](docs/STATUS.md)).

## Lizenz

Der Quellcode von OpenEM steht unter der GNU General Public License, Version 3 (nur diese Version, `GPL-3.0-only`); der Text liegt in [LICENSE](LICENSE). Die Daten des Original-Spiels gehören nicht dazu und werden nicht mitgeliefert.
