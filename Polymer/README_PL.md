# Polymer (VST3)

Generatywny sampler: wrzucasz sampla, wtyczka sama robi z niego chmurę wariacji.

## Jak używać
1. Przeciągnij plik (wav, aif, flac, mp3, ogg; do 8 s) w dowolne miejsce okna albo kliknij pole „Drag & drop sample". Kolba się napełnia, a sampel od razu gra jako odsłuch.
2. Ustaw **Chaos** (jak daleko wariacje mogą odejść od oryginału) i kliknij **Transmute**. Z kolby unosi się chmura 6 bąbli, każdy to inna wersja dźwięku.
3. **Kliknij bąbel**: słyszysz go i staje się dźwiękiem, który grają klawisze MIDI. Nic się nie generuje samo; kolejne wariacje powstają dopiero po ponownym Transmute (mutuje zaznaczony bąbel).
4. **Grab sample**: przeciągnij to pole do FL (Channel rack, Playlist, inny sampler) albo do folderu. Dostajesz zaznaczony dźwięk jako 24-bitowy .wav. Pliki lądują w Dokumentach, w folderze „Polymer exports", z datą w nazwie. Polymer ich nigdy nie nadpisuje ani nie kasuje.
4. Kliknięcie kolby wraca do oryginału. Transmute zawsze mutuje aktualnie zaznaczony dźwięk.
5. W FL Studio nuta **C5** (MIDI 60) gra dźwięk w oryginalnej wysokości.

Opcja „Note-off cut": gdy włączona, puszczenie klawisza ucina dźwięk. Domyślnie wyłączona, więc sampel gra do końca.

Chmura ma limit 36 bąbli, najstarsze „liście" rozpuszczają się same. Projekt FL zapisuje sampla i drzewo wariacji (nie same audio), po otwarciu projektu chmura odtwarza się sama w tle.

## Budowanie
Folder `Polymer` + plik `.github/workflows/build.yml` w repozytorium, zakładka Actions, pobierz artefakt `Polymer-VST3-Windows`.
