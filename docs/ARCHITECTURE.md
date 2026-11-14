# Aufbau des Quellcodes

OpenEM ist in vier Schichten geteilt. Jede hängt nur von den darunterliegenden ab:

```
openem (SDL2)          src/ui/classic_app.cpp, classic_screens.cpp, *_input.cpp, classic_sound.cpp
   │
em_classic             src/ui/classic_*.cpp     Bildschirme als reine Zeichenfunktionen (320×240, 256 Farben)
em_frontend            src/ui/session.cpp       Sitzung: Laden, Speichern, Aktionen, Tageswechsel
   │
em_game                src/game/*.cpp           Spielregeln und Spielzustand
   │
em_formats             src/formats/*.cpp        Dateiformate des Originals (Bilder, Paletten, Klänge, Spielstände)
```

Daneben gibt es `em_editor` (`src/editor/`, Win32-Oberfläche über einer plattformunabhängigen Dokumentlogik) und
kleine Werkzeuge in `src/tools/` (`em_dump`, `em_view`, `em_world`, `em_addmanager`).

## Daten des Originals

OpenEM liefert keine Daten des Spiels mit. Beim Start liest es aus dem Ordner der Originalkopie:

- `EM.EXE`: Tabellen des Spiels (Namen, Texte, Wahrscheinlichkeiten, Kostentabellen, Spielplanvorlagen). Die
  Programmlogik selbst wird nicht ausgeführt; OpenEM liest nur die Datenbereiche (`game/schedule.cpp` beschreibt,
  wie das Datensegment in der Datei gefunden wird).
- `KB\*.vga`: Bilder, Schriften, Paletten (`formats/vga.*`), die Mannschaftsdatenbank `KB\23.vga`
  (`formats/teamdata.*`), Klänge und Musik (`formats/sound.*`).
- `TORE\`: die Torszenen (`formats/tore.*`).
- `*.HOK`: Spielstände des Originals (`formats/hok.*`), darunter `START.HOK` als Ausgangszustand eines neuen Spiels.

## Spiellogik (`src/game`)

Der Spielzustand ist das Abbild eines HOK-Spielstands: ein Byteblock, dessen Felder an festen Versätzen liegen
(`formats/hok.hpp`, `game/manager_layout.hpp`). Die Regeln lesen und schreiben diese Felder direkt. Dadurch kann
OpenEM Spielstände des Originals laden und wieder im Originalformat schreiben, und jeder Zustand ist byteweise
vergleichbar.

- `game.hpp/.cpp`: die Klasse `Game`. Sie hält den Zustand, die Zufallsgeneratoren und die Manager und treibt den
  Kalender: `advance_day()` spielt einen Tag (Liga, Pokal, EuroCup, Play-offs, Abstiegsrunde, WM, Monats- und
  Saisonwechsel) und führt danach die Tagesarbeiten der Computervereine aus.
- `match.*`: das Spiel selbst, Minute für Minute (`MatchRun`), mit Reihenwechseln, Strafen, Torszenen und den
  Eingriffen des Menschen während des Spiels. `penalties.*`, `scorers.*`, `injuries.*`, `lineup.*`, `tactics.*`
  liefern die Einzelregeln.
- `season.*`, `competitions.*`, `schedule.*`, `calendar.*`: Spielpläne, Tabellen, Pokalauslosungen,
  Turniermodi und das Datum.
- `finances.*`, `sponsors.*`, `stadium*`, `contracts.*`, `transfers.*`, `market.*`, `loan.cpp`, `talent.cpp`,
  `youth*`, `training.*`: die Bereiche, die der Manager in den Menüs bedient, samt der Entscheidungen der
  Computervereine.
- `season_end.*`, `annual_*`, `redistribution.*`, `team_refresh.*`: Saisonwechsel, Auf- und Abstieg,
  Spielerentwicklung über das Jahr.
- `national_team.*`, `wm_mode.*`: Nationalmannschaft, Weltmeisterschaft und der Modus „Nur WM“.
- `new_game.*`, `world_generator.*`: der Aufbau eines neuen Spiels aus `START.HOK` und der Datenbank.
- `highscore.*`, `messages.*`, `reporting.*`, `result_books.*`, `save_history.*`: Ranglisten, Meldungen,
  Statistiken.

### Zufall

Das Original benutzt zwei Zufallsgeneratoren: den der Laufzeitbibliothek und einen eigenen. Beide sind
nachgebaut (`BorlandRandom` in `scorers.hpp`, `GameRandom` in `match.hpp`), und jede Regel zieht ihre Zufallszahlen in derselben
Reihenfolge wie das Original. Aus demselben Spielstand mit demselben Zufallszustand entstehen deshalb dieselben
Ergebnisse. Der Zufallszustand wird in OpenEMs eigenem Spielstandformat (`.oem`, `game/checkpoint.*`) mitgespeichert.

### Eigenheiten des Originals

OpenEM bildet auch die Eigenheiten des Originals nach, soweit sie das Spielgeschehen beeinflussen: zum Beispiel
Werte, die erst am nächsten Tag aktualisiert werden, oder Zähler, die mehrere Zwecke teilen. Die Kommentare an der
jeweiligen Stelle erklären, was das Original dort tut und warum OpenEM es genauso macht.

### Version 1.00 und 1.30

OpenEM bildet Version 1.00 nach. Der Patch auf 1.30 tauscht nur `EM.EXE` aus und ändert wenige Regeln:
den Stärkezuwachs eines Computervereins, der einem Manager einen Spieler abkauft (`sale_quote.cpp`), die
Neubewertung der Teams, die in 1.00 nebenbei die Kraft der ersten Abwehrreihe überschreibt und dafür eine
Zufallszahl mehr zieht (`season_end.*`, `world_generator.cpp`), und den Zuschauerbonus der EuroCup-Endrunde
(`finances.*`, `attendance_kind`). `game/rules.hpp` erkennt die Version einmal am Versionstext der EXE
(`detect_game_rules`); `Game` hält das Ergebnis (`Game::rules()`) und reicht es an diese Regeln weiter. Ohne
1.30-EXE bleibt alles bei 1.00.

## Oberfläche (`src/ui`)

- `session.*` ist die Schnittstelle zwischen Oberfläche und Spiel: Laden und Speichern (`save_io.cpp`), die
  Aktionen der Menüs, Rückfragen (zum Beispiel Vertragsangebote) und Meldungen.
- `classic_canvas.*`, `classic_font.*`, `classic_graph.*` zeichnen in einen 320×240-Puffer mit der Palette des
  Originals. Jeder Bildschirm hat einen eigenen Renderer (`classic_<bereich>.cpp`), der nur aus dem Zustand zeichnet
  und keine Eingaben kennt. Dadurch lassen sich die Bildschirme ohne Fenster pixelweise testen.
- `classic_app.cpp` und `classic_screens.cpp` verbinden die Renderer mit SDL2: Fenster, Maus, Tastatur, das Menü
  und die Wechsel zwischen den Bildschirmen. Die Eingaben der einzelnen Bildschirme liegen in `*_input.cpp`.
- `classic_match_view.*` und `classic_goal_scene.cpp` zeigen das laufende Spiel mit Anzeigetafeln und Torszenen.
- `classic_sound.cpp` gibt Klänge und Musik über SDL-Audio aus; dekodiert werden sie in `formats/sound.*`, die Musik
  läuft auf dem nachgebildeten OPL2-Chip in `formats/opl2.*`.
- `classic_script.*` führt Skripte ohne Fenster aus (`openem --script <Datei>`): Klicks und Tasten, wie sie
  `--input-log` aufzeichnet, und Befehle, die ganze Abschnitte einer Karriere durchklicken. Die Uhr ist dabei
  virtuell und jeder Lauf reproduzierbar; so laufen die Oberflächentests, etwa eine ganze Saison mit Saisonwechsel
  (`tests/scripts/classic_season_run.txt`).

## Tests

Die Tests in `tests/` prüfen Formate, Regeln und Bildschirme. Die meisten brauchen die Originaldaten und
aufgezeichnete Vergleichsdaten; sie werden nur registriert, wenn CMake diese über `OPENEM_PRIVATE_DIR` findet.
