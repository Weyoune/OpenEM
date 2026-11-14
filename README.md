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

## Herunterladen

Fertige Pakete für Windows (x64), Linux (x86_64) und macOS (Apple Silicon) liegen bei den
[Releases](https://github.com/Weyoune/OpenEM/releases). Entpacken, starten, fertig.

- **macOS:** Die Programme sind nicht signiert. Beim ersten Start im Finder mit Rechtsklick → „Öffnen“ bestätigen
  oder im Terminal `xattr -dr com.apple.quarantine <entpackter Ordner>` ausführen.
- **Linux:** Gebaut unter Ubuntu 22.04; gebraucht werden X11 oder Wayland, ALSA/PulseAudio/PipeWire und GTK 3 (für
  den Ordnerdialog beim ersten Start).

## Bauen

Voraussetzungen: CMake ab 3.24 und ein C++20-Compiler (Visual Studio 2022 oder neuer, GCC, Clang). SDL2,
nativefiledialog-extended, Dear ImGui und stb lädt CMake beim Konfigurieren selbst herunter; mit
`-DEM_USE_SYSTEM_SDL2=ON` wird stattdessen ein installiertes SDL2 benutzt. Unter Linux braucht der SDL-Build die
Entwicklerpakete für X11/Wayland und Audio sowie `libgtk-3-dev` (die genaue Liste steht in
`.github/workflows/build.yml`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cpack --config build/CPackConfig.cmake -C Release   # Paket wie bei den Releases
```

## Starten

Liegt der Ordner des Originalspiels neben `openem` (als `EM` oder `data`, oder das Programm direkt im Spielordner),
startet OpenEM sofort. Sonst fragt es beim ersten Start mit dem Dateidialog des Systems nach `EM.EXE` und merkt sich
den Ordner. Die Schreibweise der Dateien ist gleichgültig (`EM.EXE`, `EM.exe`, `kb/21.VGA` …).

Weitere Wege: `openem --data "<Pfad zum EM-Ordner>"` oder die Umgebungsvariable `OPENEM_DATA`. Liegt im Spielordner
ein `START.HOK`, wird er geladen; sonst beginnt OpenEM mit dem Startmenü. `--new-game` öffnet immer das Startmenü
eines neuen Spiels.

## Spielstände

OpenEM speichert im Format des Originals (`.HOK`): Spielstände lassen sich zwischen OpenEM und dem DOS-Spiel in
beide Richtungen austauschen. Nur die Dateinamen muss man für DOS kurz halten (acht Zeichen plus `.HOK`); OpenEM
selbst kann lange Datei- und Ordnernamen mit Leerzeichen und Umlauten. Wer den Namen mit `.oem` enden lässt,
bekommt OpenEMs eigenes Format, das zusätzlich den Zufallszustand enthält (exakt gleiche Fortsetzung); die
automatischen Sicherungen (`AUTOnnnn.OEM`) nutzen es.

Spielstände liegen im Benutzer-Datenordner (`%APPDATA%\OpenEM\OpenEM\saves` unter Windows,
`~/.local/share/OpenEM/OpenEM/saves` unter Linux, `~/Library/Application Support/OpenEM/OpenEM/saves` unter macOS).
Im Dateidialog des Spiels sind sie über `D:` erreichbar, der Spielordner über `C:`. Bestehende Dateien werden nie
überschrieben.

## Tests

Die Tests vergleichen OpenEM mit dem Original und brauchen deshalb die Originaldaten und aufgezeichnete
Vergleichsdaten, die nicht zu diesem Repository gehören. Wer Zugang zum privaten Testarchiv hat, konfiguriert mit
`-DOPENEM_PRIVATE_DIR=<Checkout>`; `ctest --test-dir build -C Release` führt die Tests dann aus.

## Editor

`em_editor` (im Release neben `openem`; Windows, Linux und macOS) bearbeitet die Originaldaten: Bilder,
Torszenengrafik, Paletten, Hallenkarten und die Mannschaften (Namen, Wappen, Stärken, Trikotfarben). Er sucht die
Daten wie `openem` und legt vor dem ersten Überschreiben eine `.bak` an. Bedienung: [docs/EDITOR.md](docs/EDITOR.md).

## Quellcode

Der Quellcode ist an Ort und Stelle dokumentiert; [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) gibt den Überblick.

## Lizenz

Der Quellcode von OpenEM steht unter der GNU General Public License, Version 3 (nur diese Version,
`GPL-3.0-only`); der Text liegt in [LICENSE](LICENSE). Die Daten des Original-Spiels gehören nicht dazu und werden
nicht mitgeliefert. Mitgebaute Bibliotheken anderer Autoren und ihre Lizenzen:
[THIRD_PARTY.md](THIRD_PARTY.md).
