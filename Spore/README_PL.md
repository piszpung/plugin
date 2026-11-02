# Spore – teksturyzator granularny (VST3, efekt)

Wtyczka nagrywa wejście do bufora kołowego (do 4 s) i odtwarza je jako chmurę drobnych „ziaren” (grain), każde z własną obwiednią (bez kliknięć).
Wyjście jest w 100% „wet” – miksuj go suwakiem miksu w opakowaniu wtyczki w FL Studio (lub użyj jako send).

## Pokrętła
- **DENSITY** – ile ziaren gra jednocześnie: 2–64.
- **SIZE** – długość ziarna: 10–500 ms (obwiednia Hann).
- **BLUR** – losowy pitch każdego ziarna w zakresie ±1200 centów × BLUR, losowa panorama L/R każdego ziarna oraz szerszy zakres miejsc, z których ziarna czytają bufor.
- **DECAY** – „życie” ziarna: okno z szybkim atakiem i długim ogonem + filtr każdego ziarna (głównie dolnoprzepustowy, część górnoprzepustowych), którego częstotliwość przesuwa się w trakcie życia ziarna, a rezonans rośnie do połowy życia i znów opada. Wartość 0 = czyste ziarna bez filtra.
- **GERMINATE** = FREEZE: przestaje zapisywać do bufora i zapętla odczyt ziaren z zamrożonego dźwięku (nieskończona tekstura). Zdjęcie freeze’a wraca do słuchania wejścia.

Po najechaniu na pokrętło etykieta zmienia się na wartość. Dwuklik = wartość domyślna. Wskaźniki IN/OUT pokazują poziom (czerwony znacznik = peak hold). Kropki w okręgu to aktualnie grające ziarna (poziomo – panorama, pionowo – jak daleko wstecz czyta).

## Uwagi
- Zamrożenie jest zapamiętane w projekcie, ale sam bufor nie – po wczytaniu projektu z włączonym GERMINATE jest cisza, dopóki nie wyłączysz i nie włączysz go ponownie na grającym dźwięku.
- Brak własnego dry/wet (celowo, mniej opcji).
- Stereo in/out. Zero opóźnienia (latency = 0).

## Build
Jak poprzednie wtyczki: wrzuć folder `Spore/` do repo, workflow z `.github/workflows/build.yml` buduje Windows VST3 (artefakt `Spore-VST3-Windows`).
Lokalne testy: `cmake -S . -B build -DSPORE_BUILD_TESTS=ON`; test silnika bez JUCE: `g++ -O2 -std=c++17 tests/test_engine.cpp`.
