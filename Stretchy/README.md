# Stretchy – ekstremalne rozciąganie dźwięku (VST3)

Efekt w stylu "Slow Motion": zamienia dowolny dźwięk (wokal, akordy, pętlę perkusji) w ambientowy pad.
Silnik: Paulstretch (FFT, zachowane amplitudy, losowe fazy, overlap-add) + filtr LPF + maska tonacji/skali.

## Parametry
| Parametr | Zakres | Co robi |
|---|---|---|
| Stretch | 100 – 20000 % (domyślnie 500 %) | ile razy dźwięk jest spowolniony |
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
(1/Stretch). Pierwszy dźwięk pojawia się po ok. 0,7 s (tyle trwa okno analizy). Przy włączonym
"Reset on play" każdy start odtwarzania zaczyna rozciąganie od początku.

## Budowanie na Windows (bez instalowania czegokolwiek)
1. Załóż repozytorium na GitHubie i wrzuć do niego cały ten folder (bez JUCE/ i build/ – zignoruje je .gitignore).
2. Zakładka **Actions** → workflow "Build Stretchy VST3 (Windows)" → po zakończeniu pobierz artefakt
   **Stretchy-VST3-Windows**.
3. Rozpakuj i skopiuj folder `Stretchy.vst3` do `C:\Program Files\Common Files\VST3\`.
4. FL Studio: *Options → Manage plugins → Find installed plugins* (Verify plugins). Plugin pojawi się jako "Stretchy".

## Budowanie lokalnie (Windows, Visual Studio 2022 + CMake)
```
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build
cmake --build build --config Release
```
Wynik: `build/Stretchy_artefacts/Release/VST3/Stretchy.vst3`.

## Testy (Linux/macOS)
```
g++ -O2 -std=c++17 -o tests/test_engine tests/test_engine.cpp
g++ -O2 -std=c++17 -o tests/test_softclip tests/test_softclip.cpp
python3 tests/run_tests.py        # wymaga numpy i scipy
```
Testy sprawdzają: współczynnik rozciągnięcia, poziom, LPF, maskę tonacji, niezależność od rozmiaru bloku,
stereo, ciszę. Test procesora: `cmake -B build -DSTRETCHY_BUILD_TESTS=ON && cmake --build build --target proc_test`.

## Ograniczenia
- Nie testowane w FL Studio (zbudowane i przetestowane headless na Linuxie).
- Plugin nie jest podpisany, co nie ma znaczenia przy użyciu prywatnym.
- Licencja JUCE: sprawdź warunki, jeśli chcesz sprzedawać plugin.
