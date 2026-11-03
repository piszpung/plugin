# Poltergeist – vibrato (VST3, efekt)

Dwa pokrętła w oczach ducha:
- **RATE** – szybkość falowania: 0,5–12 Hz (środek pokrętła ≈ 3 Hz).
- **DEPTH** – jak mocno ton odchyla się od podstawowej wysokości: 0–±200 centów (100 ct = półton). Wartość to szczyt wychylenia w górę i w dół. 0 = brak efektu.

Po najechaniu na pokrętło podpis zmienia się na wartość. Dwuklik = wartość domyślna (5 Hz, ±30 ct).

Nad duchem jest wizualizacja: słaba szara fala to ton bez efektu, jasna fala to ten sam ton z vibrato (zagęszcza się, gdy ton idzie w górę, i rozciąga, gdy w dół). Wysokość fali zależy od głośności wyjścia, a mały napis pod spodem pokazuje aktualne odchylenie w centach. Oczy ducha świecą z głośnością.

Działanie: modulowana linia opóźniająca (sinus LFO, interpolacja Hermite’a), czysto „wet” – dry/wet ustaw w opakowaniu wtyczki w FL Studio (przy vibrato z domieszką dry robi się chorus). Opóźnienie stałe 3 próbki, więc nie zgłaszam latencji. Mono i stereo.

## Build
Folder `Poltergeist/` do repo, workflow `build_all.yml` jako `.github/workflows/build.yml` (buduje Polymer, Spore i Poltergeist, artefakt `Poltergeist-VST3-Windows`).
Testy: `cmake -S . -B build -DPOLTERGEIST_BUILD_TESTS=ON`; test silnika bez JUCE: `g++ -O2 -std=c++17 tests/test_engine.cpp`.
