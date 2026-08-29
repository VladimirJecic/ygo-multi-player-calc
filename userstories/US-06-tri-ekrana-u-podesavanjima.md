# US-06 — Opcija „3 ekrana" u podešavanjima kalkulatora

**Status:** nije počelo      **Poslednja provera:** —

## Kako je zahtev postavljen
> "napisi novi za dodavanje 3 player opcije u calculator settings, ali budi veoma strog sta je
> scope tog storija dakle bitno je da se ne dupliciraju stanja i da se prikaze odmah iznad
> 4 screens"

## Šta je u obimu

**Samo red u podešavanjima i stanje koje on nosi.** Ništa drugo.

## Kriterijumi prijema
- [ ] U podešavanjima kalkulatora postoji red **„3 ekrana"**.
- [ ] Stoji **tačno iznad reda „4 ekrana"**, dakle četvrti po redu, posle tri koja igra
      ima (2 ekrana, jedan vertikalno, jedan horizontalno).
- [ ] Izgleda kao ostali redovi — isti font, ista visina, isto radio dugme, isti razmak.
      Ne sme se videti da je dodat naknadno.
- [ ] Tapkanjem se pali samo taj red, a svi ostali se gase.
- [ ] **U svakom trenutku je upaljen tačno jedan red.** Nikada dva, nikada nijedan.
- [ ] Izbor preživi izlazak iz podešavanja i povratak.
- [ ] Izbor preživi gašenje i ponovno pokretanje aplikacije.
- [ ] Ulazak u podešavanja dva puta zaredom **ne dodaje drugi red „3 ekrana"**.
- [ ] Postojećih pet režima (3 igrina + „4 ekrana" + „5 ekrana") radi tačno kao pre.

### Bez dupliranja stanja
- [ ] Izabrani režim se pamti na **jednom mestu**. Ne postoji druga promenljiva, drugi fajl
      ni drugo polje koje takođe tvrdi koji je režim izabran.
- [ ] Ono što je upaljeno na ekranu i ono što je zapamćeno **uvek se poklapaju**, i odmah
      posle tapa i posle restarta aplikacije.
- [ ] Nijedan red se ne klonira dvaput, ni pri ponovnom ulasku u podešavanja, ni pri
      promeni dizajna kalkulatora.

## Šta **nije** u obimu

Za svako od ovoga ide zasebna priča i ne sme se rešavati ovde:

| Nije ovde | Ide u |
|---|---|
| kako tri kalkulatora izgledaju i kako su raspoređeni | zasebna priča o izgledu |
| oduzimanje i dodavanje poena trećem igraču, i njegovo ime | zasebna priča o funkcionalnosti |
| upis partije sa tri igrača u Log | zasebna priča o logu |
| vraćanje partije sa tri igrača iz Loga | zasebna priča o restartu iz loga |

Ako ulazak u duel sa izabrana „3 ekrana" pokaže loš raspored, **ova priča je i dalje
ispunjena** — to je posao one koja dolazi.

## Kako se proverava
1. Glavni meni → podešavanja kalkulatora. Red „3 ekrana" je četvrti, tačno iznad „4 ekrana".
2. Tapni „3 ekrana" → upali se samo on.
3. Tapni „5 ekrana" pa opet „3 ekrana" → i dalje tačno jedan upaljen.
4. Nazad iz podešavanja, pa opet u njih → „3 ekrana" i dalje upaljen, i redova je i dalje
      šest, ne sedam.
5. `scripts/device/shell.sh 'am force-stop jp.konami.YugiohOcgSupports'`, pokreni aplikaciju,
      opet u podešavanja → i dalje upaljen, i dalje šest redova.
6. Vrati na „2 ekrana" i uđi u duel → stock ekran, nepromenjen.

## Tehnički

Mapiranje već postoji: `players_mode()` / `mode_players()` u `src/native/mod.c` vezuju
**režim 5 za tri igrača**, pored 3 za četiri i 4 za pet. Broj je rezervisan, red nije dodat.

Redovi se kloniraju u `my_OnEnable` (hook na `Calculator.OnEnable`), iz reda „2 ekrana" kao
uzorka: `clone_row(srcGo, node, "Four", "4 screens", 3)` pa `"Five", "5 screens", 4`. Novi red
ide **pre** ta dva, sa režimom 5.

Dve stvari koje će pući ako se ne paze:

- **Zaštita od dupliranja redova** je `if (n != 3 && n != 5)` — broji koliko redova kontejner
  već ima i odustaje ako broj nije očekivan. Sa novim redom očekivani brojevi postaju 3 i 6.
  Ako se to ne izmeni, mod će pri drugom ulasku ili odustati ili klonirati redove ponovo.
- **`FixLayout` korutina prefarba stock radio dugmad jedan frejm kasnije**, pa se naša
  selekcija mora ponovo naneti posle nje — inače se upali stari red i na ekranu su dva
  upaljena. To je isti mehanizam opisan u US-01.

Trajno pamćenje ide kroz `files/neuronmod.mode` i kroz originalni `OnClickCalcMode`, koji svoj
upis radi bezuslovno. Detalji ekrana podešavanja: `docs/DESIGN.md` §2.
