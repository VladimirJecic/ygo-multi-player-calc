# US-03 — Klik na kalkulator otvara tačno tog igrača (4 i 5 ekrana)

**Status:** radi   **Poslednja provera:** v230

## Kako je zahtev postavljen
> "svaki od kalkulatora kada se klikne otvara prikaz trenutnog broja bodova igrača, njegovo
> ime i opcije za promenu bodova. ovaj kriterijum se mora zadovoljiti za svaki kalkulator
> posebno tako da svako ime kalkulatora mora biti posebno i odgovarati kalkulatoru koje je
> kliknuto, isto važi za broj bodova — ako je kliknut kalkulator sa 7.000 bodova mora da se
> otvori kalkulator sa 7.000 bodova"

## Kriterijumi prijema
- [x] Tap na bilo koji panel otvara tastaturu za **tog** igrača.
- [x] U zaglavlju piše ime **tog** igrača.
- [x] Prikazani broj bodova je **tačno** onaj sa panela koji je kliknut.
- [x] Sabiranje/oduzimanje menja bodove samo tom igraču; ostali se ne pomere.
- [x] Animacija promene broja ide nad kliknutim panelom, a ne nad tuđim.
- [x] Važi i za prva dva igrača (koje igra "zna") i za igrače 3–5.

## Kako se proverava
1. Pet igrača, svima 8000.
2. Igraču 3 oduzmi 1000 → samo on ima 7000.
3. Tapni igrača 3 → tastatura mora pokazati 7000 i njegovo ime.
4. Ponovi za igrače 4, 5, 1 i 2.

## Tehnički
`my_ClickLife()`, `retarget_panel_click()`, `fix_keypad_header()`, `aim_animation()`,
`scrub_panels()`. `StartDuel.SelectedPlayerIndex` je na `self + 44`.

Model igre je ispravan za svih pet igrača — pogrešan je samo **prikaz** u zaglavlju, jer je
zakačen za keš od dva igrača: ime dolazi iz placeholder-a `DuelistNameIf` (`self + 256`), a
bodovi su vodeći broj u `EquationText` (`self + 248`). Piše se za **svaki** indeks, ne samo
za 3–5. Vidi `docs/DESIGN.md` §4.
