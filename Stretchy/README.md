# Chronosphere – ekstremalne rozciąganie dźwięku (VST3)

Efekt w stylu "Slow Motion": zamienia dowolny dźwięk (wokal, akordy, pętlę perkusji) w ambientowy pad.
Silnik: Paulstretch (FFT, zachowane amplitudy, losowe fazy, overlap-add) + filtr LPF + maska tonacji/skali.

## Parametry
| Parametr | Zakres | Co robi |
|---|---|---|
| Stretch | 100 – 3000 % (domyślnie 500 %) | ile razy dźwięk jest spowolniony |
| LPF | 100 Hz – 20 kHz (domyślnie 1 kHz) | filtr dolnoprzepustowy 24 dB/okt. |
| Key | None, C … B | tonacja; None wyłącza maskę |
| Scale | Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Minor, Pentatonic Major/Minor, Blues | skala maski |
| Mix | 0 – 100 % | suchy / efekt |
| Output | -24 … +12 dB | głośność wyjścia |
| Reset on play | on/off | restart rozciągania przy starcie odtwarzania i przeskoku wstecz (pętla) |
| Safe clip | on/off | miękkie ograniczenie szczytów do 0 dBFS |

Uwaga: Key/Scale działa tu jako maska widmowa: zostają tylko częstotliwości blisko dźwięków skali
(poniżej 60 Hz nic się nie zmienia). To moja interpretacja, nie jest kopią oryginału.

## Jak to działa w DAW
Plugin jest efektem na kanale miksera. Nagrywa wejście do bufora (ok. 30-45 s) i odtwarza je wolniej
(1/Stretch). Dźwięk pojawia się po ok. 20 ms: pierwsze ziarna mają krótkie okna, które rosną do pełnego
okna ~0,7 s. Gdy projekt jest zapauzowany, efekt (część mokra) milknie w ok. 10 ms; sygnał suchy przechodzi
według Mix. Przy włączonym "Reset on play" rozciąganie startuje od początku przy każdym odtwarzaniu.

## Budowanie na Windows (bez instalowania czegokolwiek)
1. Załóż repozytorium na GitHubie i wrzuć do niego zawartość tego folderu (razem z folderem `.github`).
2. Zakładka **Actions** → workflow "Build Chronosphere VST3 (Windows)" → po zakończeniu pobierz artefakt
   **Chronosphere-VST3-Windows**.
3. Rozpakuj i skopiuj folder `Chronosphere.vst3` do `C:\Program Files\Common Files\VST3\`.
4. FL Studio: *Options → Manage plugins → Find installed plugins*. Plugin pojawi się jako "Chronosphere".

Jeśli kod leży w podfolderze repozytorium (np. `Stretchy/`), użyj w workflow:
`git clone ... JUCE` do `<podfolder>/JUCE`, `cmake -S <podfolder> -B build` i ścieżki artefaktu jak wyżej.

## Budowanie lokalnie (Windows, Visual Studio 2022 + CMake)
```
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build
cmake --build build --config Release
```
Wynik: `build/Chronosphere_artefacts/Release/VST3/Chronosphere.vst3`.

## Testy (Linux/macOS)
```
g++ -O2 -std=c++17 -o tests/test_engine tests/test_engine.cpp
g++ -O2 -std=c++17 -o tests/test_softclip tests/test_softclip.cpp
python3 tests/run_tests.py        # wymaga numpy i scipy
```
Testy silnika: współczynnik rozciągnięcia, poziom, LPF, maska tonacji, szybki start (~20 ms), niezależność od rozmiaru
bloku, stereo, cisza. Testy procesora i zrzut okna do PNG (bez hosta):
```
cmake -B build -DCHRONOSPHERE_BUILD_TESTS=ON && cmake --build build --target proc_test render_editor
build/proc_test
build/render_editor okno.png 500 5
```

## Ograniczenia
- Nie testowane w FL Studio (zbudowane i przetestowane headless na Linuxie).
- Plugin nie jest podpisany, co nie ma znaczenia przy użyciu prywatnym.
- Licencja JUCE: sprawdź warunki, jeśli chcesz sprzedawać plugin.
