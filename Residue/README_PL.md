# Residue: delay w stylu EchoBoy

Interfejs: czerń z czerwono-niebieskim efektem 3D (nakładające się kolory dają biel). Wizualizacja pokazuje echa jako tunel pierścieni.

Regulatory: TIME (ms) albo NOTE (nuta, gdy SYNC włączony), FEEDBACK, SATURATION, WOBBLE, DUCKING, PING-PONG, LOW CUT, HIGH CUT, MIX.
Style: DIGITAL (czysty), TAPE (taśma, ciemnieje z każdym echem), ANALOG (BBD, ciemniejszy), LO-FI (obniżona rozdzielczość).
Sync: 1/32 ... 1/1 (w tym trioli i z kropką), tempo bierze z hosta (bez hosta: 120 BPM).

Domyślnie: SYNC 1/8., TAPE, Feedback 35%, Mix 30%.
Brak opóźnienia wtyczki (latencja 0), ogon 12 s.

Budowa: folder Residue/ do repo, build_all.yml jako .github/workflows/build.yml, artefakt Residue-VST3-Windows.
