# Bibliotheken anderer Autoren

OpenEM und `em_editor` werden mit diesen Bibliotheken gebaut (beim Konfigurieren von CMake in festen Versionen
heruntergeladen und fest in die Programme eingebunden):

| Bibliothek | Version | Lizenz | Verwendet für |
|---|---|---|---|
| [SDL2](https://github.com/libsdl-org/SDL) | 2.32.10 | zlib | Fenster, Eingabe, Ton |
| [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) | 1.2.1 | zlib | Dateidialog beim ersten Start |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.91.9b | MIT | Oberfläche des Editors (Linux, macOS) |
| [Roboto](https://github.com/googlefonts/roboto) (aus Dear ImGui) | – | Apache 2.0 | Schrift des Editors |
| [stb_image, stb_image_write](https://github.com/nothings/stb) | 2.30 / 1.16 | Public Domain oder MIT | Bildimport des Editors |

Die Lizenztexte stehen in den jeweiligen Projekten unter den angegebenen Adressen.
