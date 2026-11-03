# Spore – teksturyzator granularny (VST3, efekt)

Wtyczka nagrywa wejście do bufora kołowego (do 4 s) i odtwarza je jako chmurę drobnych „ziaren” (grain), każde z własną obwiednią (bez kliknięć).
Wyjście jest w 100% „wet” – miksuj go suwakiem miksu w opakowaniu wtyczki w FL Studio (lub użyj jako send).

## Pokrętła
- **DENSITY** – ile ziaren gra jednocześnie: 2–64.
- **SIZE** – długość ziarna: 10–500 ms (obwiednia Hann). Dłuższe ziarna = gładszy pad, krótkie = bardziej „ziarnisty” szum.
- **BLUR** – rozmycie: losowa panorama L/R każdego ziarna, delikatny detune i szerszy zakres miejsc, z których ziarna czytają bufor. Od ok. 20% dochodzą przesunięcia wysokości o oktawę, od ok. 40% o kwartę, od ok. 55% o kwintę (maks. ±1200 centów). Przesunięcia są muzyczne, a nie ciągłe losowe – ciągłe losowanie wysokości zamienia każdy ton w szum („wiatr”).
- **DECAY** – „życie” ziarna: okno z szybkim atakiem i długim ogonem + łagodny filtr każdego ziarna (głównie dolnoprzepustowy, część górnoprzepustowych), którego częstotliwość przesuwa się w trakcie życia ziarna, a rezonans lekko rośnie w połowie życia. 0 = czyste ziarna bez filtra.
- **GERMINATE** = FREEZE: przestaje zapisywać do bufora i zapętla odczyt ziaren z zamrożonego dźwięku (nieskończona tekstura).

Ziarna są ustawiane w fazie z tym, co już gra (dopasowanie kształtu fali, do 20 ms), dzięki czemu trzymany akord zostaje czystym, stałym padem. Głośność wyjścia jest dopasowana do głośności materiału.

Po najechaniu na pokrętło etykieta zmienia się na wartość. Dwuklik = wartość domyślna. Zarodniki (kropki) pod kapeluszem to aktualnie grające ziarna; kapelusz świeci z głośnością, a przy GERMINATE zarodniki prawie stają.

## Uwagi
- Zamrożenie jest zapamiętane w projekcie, ale sam bufor nie – po wczytaniu projektu z włączonym GERMINATE jest cisza, dopóki nie wyłączysz i nie włączysz go ponownie na grającym dźwięku.
- Brak własnego dry/wet (celowo, mniej opcji).
- Stereo in/out. Zero opóźnienia (latency = 0).

## Build
Jak poprzednie wtyczki: wrzuć folder `Spore/` do repo, workflow z `.github/workflows/build.yml` buduje Windows VST3 (artefakt `Spore-VST3-Windows`).
Lokalne testy: `cmake -S . -B build -DSPORE_BUILD_TESTS=ON`; test silnika bez JUCE: `g++ -O2 -std=c++17 tests/test_engine.cpp`.
