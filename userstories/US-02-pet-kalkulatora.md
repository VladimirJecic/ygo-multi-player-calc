# US-02 — Pet kalkulatora, pravilno raspoređenih

**Status:** delimično   **Poslednja provera:** v230

## Kako je zahtev postavljen
> "treba omogućiti klikom na duel da se unutra vidi pet kalkulatora pravilno raspoređenih,
> postoji osam različitih dizajnova kalkulatora i svaki ima posebna podešavanja kojima se
> uređuje izgled kalkulatora i raspored ekrana, posle svake izmene raspored ekrana mora da
> bude zadovoljavajući na svih osam ekrana"

## Kriterijumi prijema
- [x] Ulaskom u duel vidi se tačno onoliko kalkulatora koliko je podešeno (4 ili 5).
- [x] Raspoređeni su ravnomerno; peti je na sredini.
- [x] Nijedan ne izlazi van ekrana i nijedan se ne preklapa sa drugim.
- [x] Ne preklapaju se ni sa X dugmetom (gore levo), tajmerom, ni sa donjim redom dugmadi.
- [x] Isto važi na **svih osam dizajnova**: Standard, Simple, Duel Monsters, GX, 5D's,
      ZEXAL, ARC-V, VRAINS.
- [x] Važi i pri hladnom ulasku i pri drugom ulasku bez restarta aplikacije.
- [ ] **3 igrača: raspored nije centriran.** Korisnik traži povratak na raspored iz v224.

## Kako se proverava
1. Podesi 5 ekrana, uđi u duel — pet panela, peti na sredini.
2. Izađi X-om i uđi ponovo — isti raspored (drugi ulazak ima drugu veličinu rect-a).
3. Promeni dizajn kalkulatora, **uđi i izađi jednom** (promena dizajna važi tek od sledećeg
   ulaska), pa gledaj — i tako za svih osam.
4. Ponovi za 4 ekrana i za 3.

## Tehnički
`build_four_player_layout()` u `src/native/mod.c`. Kompletna pravila rasporeda i sve
konstante su u **`docs/DESIGN.md` §3.1** — tabela pozicija, budžeti širine/visine,
klampovi i razlog za svaki od njih.

Tri igrača koriste istu mrežu kao pet, samo bez donjeg reda: dva gore i treći na sredini.
