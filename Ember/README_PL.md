# Ember – wielopasmowy saturator (VST3)

Wtyczka w stylu FabFilter Saturn 2: dzieli sygnał na do 6 pasm, każde pasmo ma własny styl zniekształcenia i własne ustawienia. Nazwa, wygląd i algorytmy są własne (nie kopiuję niczego z FabFilter), więc **brzmienie nie jest identyczne z Saturn 2**. Zestaw funkcji jest zbliżony, różnice wypisane niżej.

## Co jest w środku
- **1–6 pasm**, zwrotnice przeciągasz myszką na wykresie widma (dwuklik = reset). **Nachylenie** 6 / 12 / 24 / 36 / 48 dB/okt (Linkwitz-Riley, suma pasm płaska co do 0,00 dB).
- **28 stylów** w 6 grupach: Tube (4), Tape (4), Saturation (5), Transformer (3), Amp (5), FX (7: Foldback, Sine Fold, dwa prostowniki, Bit Crusher, Breakdown, Smear).
- Na pasmo: **Drive, Level, Mix, Dynamics** (– kompresja / + ekspandowanie), **Tone** (tilt), **Feedback + Fb Freq** (rezonans), **Pan**, ON, SOLO. Pasmo wybierasz klikając je na wykresie.
- **Globalnie:** Input, Output, Mix (dry/wet z kompensacją opóźnienia), M/S, Limit, **Oversampling** Off / 2× / 4× / 8× / 16× / 32× (latencja zgłaszana do hosta).
- **Modulacje** (przycisk MOD): 2 LFO 16-krokowe (swobodne w Hz lub zsynchronizowane z tempem, glide), 2 followery obwiedni, 2 suwaki ręczne i **8 tras** źródło → cel (cel: Drive/Level/Mix/Dynamics/Tone/Feedback/Fb Freq/Pan dowolnego pasma, zwrotnice, Input/Output/Mix). Pokrętła pokazują ruch modulacji białym znacznikiem.
- Widmo wejścia i wyjścia na żywo, krzywa przenoszenia aktualnego stylu.

## Czego nie ma w porównaniu z Saturn 2 (uczciwie)
- Tryb liniowej fazy zwrotnic.
- 50 slotów modulacji (tu 8), generator obwiedni ADSR, XY, źródło MIDI.
- Presety fabryczne, A/B, undo/redo, MIDI learn, zmiana wielkości okna.
- 28 stylów to moje własne algorytmy o nazwach w tych samych kategoriach, nie kopie FabFilter.

## Obciążenie CPU (pomiar, 6 pasm stereo, jeden rdzeń)
24 dB/okt: 1× 6 %, 8× 25 %, 32× 90 %. 48 dB/okt: 1× 5 %, 8× 44 %, 32× 170 %.
Praktycznie: 8× przy sześciu pasmach, 32× tylko przy kilku pasmach. Domyślnie 2×.

## Jak zbudować
Folder `Ember/` do repo `piszpung/plugin`, workflow `build_all.yml` jako `.github/workflows/build.yml`. Artefakt: `Ember-VST3-Windows`.
