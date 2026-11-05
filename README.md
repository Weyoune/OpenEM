# OpenEM

OpenEM rekonstruiert den Eishockey Manager von 1993. Die Oberfläche folgt dem DOS-Original: 320×240 Pixel, ursprüngliche Grafiken, Schriften, Farben, Fenster und Bedienwege. Weitere Originalseiten und Teile der Spielsteuerung fehlen noch.

## Starten

Aus diesem Arbeitsverzeichnis:

```powershell
& ./build/menu-current/Release/openem.exe
```

Die lokalen Originaldaten unter `work/EM` werden benötigt. Der Start lädt `START.HOK`. Eigene Sicherungen liegen unter `work/saves`; sie sind im ursprünglichen Dateidialog über `D:` erreichbar.

- `.oem` erhält den OpenEM-Fortsetzungszustand einschließlich beider Zufallsfolgen.
- `.hok` exportiert für das DOS-Original; dessen Format enthält diese Zufallsfolgen nicht.

Bestehende Speicherdateien werden nicht überschrieben.

## Stand und Belege

[Bedienung und Grenzen](docs/CLASSIC_UI.md), [Projektstand](docs/STATUS.md), [Prüfungen](docs/TESTING.md) und [Checkpointformat](docs/formats/OPENEM_CHECKPOINT.md) beschreiben den aktuellen Umfang.

59 konkrete Ansichten stimmen vollständig mit DOSBox-Aufnahmen überein. Manager → Stadion und Jugend sowie die beiden Verkaufsdialoge verwenden die ursprünglichen Grafiken und Bedienwege. Angebote werden erst beim Öffnen bewertet; Behalten und Verkaufen folgen den beobachteten Zustands- und Zufallsänderungen. Release und AddressSanitizer bestehen jeweils 39/39 Tests.

OEM-Version 5 erhält zusätzlich die jährlichen Verteilungscursor, die native 50er-Kennungshistory und die bisherigen flüchtigen Fortsetzungsdaten. Versionen 1–4 bleiben lesbar.

Eine vollständige Originalkarriere mit identischer Zufallsfolge ist weiterhin offen. Dazu fehlen unter anderem Teile der Menü-/Stimmungsphase, weitere Managerseiten, mehrere Dialoge und Live-Steuerung. Aktuelle Verkaufsbelege liegen unter `work/exp/menu_20261104`; ältere eingefrorene Prüfstände bleiben erhalten.
