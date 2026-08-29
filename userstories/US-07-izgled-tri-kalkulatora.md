# US-07 — Izgled kalkulatora sa tri igrača

**Status:** nije počelo      **Poslednja provera:** —

## Kako je zahtev postavljen
> "On se bavi samo izgledom kalkulatora, i gotov je tek kada su svi ekrani poravnati po sredini
> po svih 8 design ekrana, okej je što sada nisu inicijalno."

Dopunjeno odgovorima na pitanja (2026-08-29):
> **Raspored:** dva gore, treći dole u sredini.
> **Preklapanje sa donjim redom dugmadi:** podigni ceo blok, zadrži veličinu panela.
> **Poravnanje:** "treba biti handlovano za svaki posebno" — misli se na **svaki od osam
> dizajna**, ne na svaki panel.

## Šta je u obimu

**Samo raspored i poravnanje tri panela u duelu.** Ni poeni, ni imena, ni Log.

## Kriterijumi prijema
- [ ] Dva panela gore, treći dole, **vodoravno centriran** između njih.
- [ ] Gornja dva su simetrična u odnosu na sredinu ekrana — leva i desna ivica jednako
      udaljene od ivica ekrana.
- [ ] **Svaki od osam dizajna se dovodi u red zasebno i zasebno se proverava.** Nije dovoljno
      da jedno pravilo prođe na Standardu i da se pretpostavi za ostale — svih osam se gleda,
      i onaj koji nije centriran dobija svoje rešenje.
- [ ] **Treći panel je stvarno na sredini**, mereno po tabli koju skin crta, ne po
      pravougaoniku. Ovo je uslov koji se najlakše previdi jer razlika ume da bude mala.
- [ ] Ceo blok je vertikalno centriran: prazan prostor iznad gornjeg reda i ispod trećeg
      panela je približno jednak.
- [ ] Nijedan panel se ne preklapa sa donjim redom dugmadi (Log, Reset, Undo, Tools),
      ni sa tajmerom, ni sa X dugmetom gore levo. Blok se za to **podiže**, paneli se ne
      smanjuju.
- [ ] Nijedan panel ne izlazi van ekrana i ne preklapa se sa drugim panelom.
- [ ] **Sve navedeno na svih osam dizajnova**: Standard, Simple, Duel Monsters, GX, 5D's,
      ZEXAL, ARC-V, VRAINS.
- [ ] Važi i pri hladnom ulasku u duel i pri drugom ulasku bez restarta aplikacije.
- [ ] Rasporedi za 4 i 5 igrača ostaju tačno kakvi jesu — ova priča ih ne dira.

## Šta **nije** u obimu

| Nije ovde | Ide u |
|---|---|
| red „3 ekrana" u podešavanjima | US-06, gotovo |
| koliko igrača se pravi, oduzimanje poena, imena | zasebna priča o funkcionalnosti |
| upis partije sa tri igrača u Log | zasebna priča o logu |
| vraćanje partije sa tri igrača iz Loga | zasebna priča o restartu iz loga |

Ako se poeni ne oduzimaju kako treba ili ime trećeg igrača fali, **ova priča je i dalje
ispunjena** — meri se samo raspored.

## Kako se proverava
1. Podešavanja → „3 ekrana". Uđi u duel.
2. Gledaj tri stvari: da li je treći panel na sredini, da li je prazan prostor gore i dole
   jednak, i da li dugmad Log/Reset/Undo/Tools stoje slobodno.
3. Izađi X-om i uđi ponovo — drugi ulazak ima drugu veličinu rect-a i ume da izgleda drugačije.
4. Promeni dizajn kalkulatora, **uđi i izađi jednom** (promena važi tek od sledećeg ulaska),
   pa gledaj. **I tako za svih osam, jedan po jedan** — svaki se prihvata ili odbija za sebe,
   a priča je gotova tek kad prođu svih osam.
5. Vrati na „4 ekrana" pa „5 ekrana" i uđi — moraju izgledati kao pre.

## Tehnički

Raspored živi u `build_four_player_layout()` u `src/native/mod.c`; pravila i sve konstante su
u `docs/DESIGN.md` §3.1. Tri igrača već koriste istu mrežu kao četiri, sa donjim redom svedenim
na jedan panel: `tx[2] = sh` i `ty[2] = -cy - topMargin`.

**Zna se zašto treći panel nije na sredini.** Usamljeni panel nema svog parnjaka sa druge
strane, pa se vidi koliko je tabla koju skin crta pomerena unutar svog pravougaonika. Za to
ispravka postoji, ali je vezana za **indeks 4**:

```c
if (i == 4) {                       /* mod.c:2895 */
    if (plate_axis(g_panel[i], &dx)) px = dx;
```

Kod pet igrača usamljeni panel jeste indeks 4. **Kod tri igrača je indeks 2**, pa ispravka
nikad ne odradi svoje i panel ostane pomeren za onoliko koliko je tabla pomerena u svom
pravougaoniku. Uslov treba da bude „ovo je usamljeni panel", ne „ovo je peti panel" — što je
isti oblik greške kao redni broj reda u US-06.

Popravka tog uslova je verovatno prvi korak, ali se **ne sme uzeti kao rešenje za svih osam**.
Korisnik traži da se svaki dizajn dovede u red i proveri zasebno: osam skinova crta tablu
različito u odnosu na svoj pravougaonik, i ono što centrira Standard ne mora ni da pomeri
ARC-V.

Redosled rada koji to poštuje: prvo mera koja važi svuda — `plate_axis()` na usamljenom panelu,
sa uslovom „ovo je usamljeni panel" umesto „indeks 4" — pa onda **obilazak svih osam** i za
svaki koji i dalje nije centriran, njegovo sopstveno rešenje. Poseban slučaj po skinu je
dozvoljen tek kad merenje ne uspe da ga centrira, i tada se u kodu piše koji je skin i zašto
(pravilo R2 u `docs/DESIGN.md` je i dalje „meri, ne predviđaj" — konstanta po skinu je
poslednje sredstvo, ne prvo).

Za vertikalno centriranje i za razmak od donjeg reda dugmadi: `topMargin` gura ceo blok
naniže, a `cy` određuje razmak redova. Meri se prema `LifeArea`, nikad u pikselima — pravilo
R1 u `docs/DESIGN.md`.

Poznato da ume da pukne, iz ranijih rasporeda: ARC-V crta štit viši od svog pravougaonika, pa
se mora meriti `max(rect, plate)`; GX-u se redovi dodiruju ako je razmak fiksan umesto izveden
iz visine table; 5D's ima X dugme u gornjem levom uglu koje ume da sedne na prvog duelista.
