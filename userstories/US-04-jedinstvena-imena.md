# US-04 — Promena imena olovčicom, bez duplikata

**Status:** radi na 1-7; ostaje samo VRAINS (8)   **Poslednja provera:** v234

## Kako je zahtev postavljen
> "korisnik aplikacije na svakom ekranu ima opciju da klikom na olovčicu promeni svoje ime.
> nakon što je promenio svoje ime i zatvorio kalkulator isto to ime se prikazuje za taj
> kalkulator i to samo to ime, ne sme biti duplikata. na slici je primer toga kako
> kalkulator ne sme da izgleda: na svakom kalkulatoru postoji nekoliko imena koja su
> polovično zapisana, to jest došlo je do greške"

Dokaz greške: `reference/screenshots/bug-duplicate-names-5p.png`

## Kriterijumi prijema
- [ ] Olovčica na svakom od pet ekrana otvara polje za unos imena.
- [ ] Uneto ime se posle zatvaranja kalkulatora prikazuje **samo** na tom panelu.
- [ ] Nijedan drugi panel ne promeni ime.
- [ ] Nema duplikata: pet različitih imena ostaje pet različitih imena.
- [ ] Nema polovično ispisanog, presečenog ili preklopljenog teksta; ime je uredno
      centrirano i ne izlazi iz okvira.
- [ ] Duža imena se skupljaju da stanu, umesto da se seku.
- [ ] Imena prežive izlazak iz duela i ponovni ulazak, i restart aplikacije.
- [ ] Ista imena se vide i u Log dijalogu (US-05).
- [ ] Sve navedeno na svih osam dizajnova.

### Otvoreno na v230 (prijava korisnika, 2026-08-29)
- [ ] **Imena su precrtana na svim ekranima.**
      *Popravljeno u v231, još neprovereno na telefonu.* `label_panel` je pomerao naše polje sa
      imenom na centar originalnog natpisa, i po x i po y. Tri skina crtaju tanku liniju ispod
      natpisa (`Duelist/ImageLine` 7.78 x 0.04 na Standardu, 6.71 x 0.03 na Simple,
      `Duelist/line` 4.00 x 0.02 na GX), a originalni natpis stoji tačno na toj liniji — pa je
      poravnanje po y parkiralo ime na crtu. To pomeranje postoji samo zbog VRAINS-a, kod koga
      polje sa imenom visi desno od table, a to je vodoravni problem, pa se sada pomera **samo
      po x**.
- [ ] **Natpis je pretih na ZEXAL-u, nevidljiv na ARC-V i VRAINS-u.** (prijava korisnika uz
      snimke, v231, 2026-08-29 — `reference/screenshots/v231-*.png`) Od osam dizajna korisnik
      je kao ispravne prihvatio Duel Monsters i 5D's. Prazno polje `Duelist/PlayerName` koje
      skin ostavlja nije svuda iste veličine u odnosu na brojač pored kog stoji:

      | dizajn | PlayerName | LifePoints | odnos | kako izgleda |
      |---|---|---|---|---|
      | Standard | 3.76 x 0.44 | 5.11 x 1.11 | 0.40 | čita se |
      | Duel Monsters | 3.16 x 0.64 | 5.33 x 1.44 | 0.44 | u redu |
      | ARC-V | 3.44 x 0.56 | 4.78 x 1.22 | 0.46 | **ne vidi se** |
      | ZEXAL | 3.19 x 0.28 | 5.14 x 0.96 | 0.29 | presitno |
      | VRAINS | 2.22 x 0.24 | 5.11 x 1.11 | 0.22 | **ne vidi se** |

      Za ZEXAL i VRAINS odnos objašnjava simptom. Pokušana popravka (podizanje veličine slova
      na 0.40 brojačeve kad padne ispod 0.34) je **povučena** — vidi "Treperenje" ispod.
      **ARC-V odnosom nije objašnjen** — isti mu je kao kod Duel Monsters-a koji radi — pa je
      ostavljena samo dijagnostika `cap where:`, koja po panelu ispisuje ime čvora, njegov
      pravougaonik, položaj u svetu i veličinu slova naspram brojača.
- [ ] **Na ZEXAL-u prvo slovo imena je krupnije i drugog oblika** ("Laza" na donjem levom
      panelu). Nije objašnjeno; verovatno zaostala kopija ispod naše, pošto ZEXAL drži natpis u
      dva čvora (`TextPlayer` i `TextPlayer/01`). Čeka log.
- [ ] **Na Standardu pored imena stoji odsečen ostatak igrine oznake** ("H", "Vl", "Al", "La").
      *Dijagnostikovano, popravka POVUČENA — vidi "Treperenje" ispod.* Korisnikova dijagnoza (2026-08-29): "ako postoji jedno
      polje u koje upišeš slova po kalkulatoru... trebalo bi da se prikaže samo jednom, a to što
      se prikazuje drugi put znači da postoji referenca na to polje negde drugde." Referenca
      postoji i zove se `UISystem.LocalizeText.BindingText` — vezana za `Duel.Player.Name`, pa
      se polje samo prepisuje iz modela. Na Standardu je to `Duelist/Text_obj`, 0.56 jedinica
      široko naspram 3.76 koliko ima natpis — taman za dva slova, odatle "H" i "Vl".
      Brisanje je poredilo samo sa igrina dva imena (`DuelistName1/2`), a na klonovima to polje
      drži **ime tog panela**, pa se "Laza" na trećem panelu nije poklopilo ni sa jednim i
      kopija je ostajala. Sada se poredi i sa natpisom koji smo upravo upisali. `set_tmp` pre
      brisanja skida `BindingText`, pa se polje ne može ponovo popuniti iz modela.
- [ ] **Na Standardu ime nestaje kad se sačuva, za igrače 1 i 2.** *Popravljeno u v231, čeka
      proveru.* Korisnikov test: preimenovao gornje desno polje u "Goran", tekst je po čuvanju
      nestao; isto se posle desilo gornjem levom; kad je isto polje izmenio **drugi put**, ime
      se pojavilo. Igra ne zna da paneli 3-5 postoje, pa tamo ono što upišemo ostaje. Panele 1 i
      2 igra drži i prepisuje njihov natpis iz svog stanja, a mod je natpis ponavljao samo
      dok traje prozor smirivanja — pa ga je igra vratila na svoje čim se prozor zatvori.
      Drugi pokušaj radi zato što preimenovanje otvori prozor na još 30 frejmova. Sada se
      paneli 1 i 2 ponavljaju sve dok je ekran otvoren.
- [ ] **Na VRAINS dizajnu ime uopšte ne može da se postavi.** *I dalje otvoreno.* Čitanje koda
      ovo nije rešilo: putanje do natpisa se resetuju pri svakoj promeni dizajna, pa zastarela
      putanja otpada, a na papiru VRAINS treba da nađe `Duelist/PlayerName` i da proradi.
      Treba jedan logcat na VRAINS dizajnu — spisak linija koje razdvajaju preostale
      mogućnosti je u `docs/HISTORY.md`, "Current state".

## Kako se proverava
1. Pet igrača. Preimenuj igrača 2 u "Aleksa", zatvori kalkulator.
2. Otvori igrača 3 → **polje mora biti prazno / njegovo ime**, ne "Aleksa".
   (Ovo je tačno bug sa slike: polje pamti poslednji unos i deli ga sledećem.)
3. Daj svima pet različitih imena, izađi iz duela, vrati se → svih pet i dalje na svom mestu.
4. Ubij aplikaciju i pokreni je → imena i dalje tu.
5. Ponovi na svih osam dizajnova, posebno na 5D's, ARC-V i VRAINS (ta tri crtaju ime kao
   sliku, ne kao tekst).

## Tehnički
Ovo je kriterijum koji se najčešće vraća. Dva nezavisna razloga:

**1. Igra ima tačno dva imena.** `StartDuel.DuelistName1/2` su statici, ništa nije indeksirano
po igraču, pa je ime otkucano za igrača 4 završavalo u slotu igrača 2 ili nestajalo. Mod drži
svojih pet (`g_pname[5][40]`, fajl `files/neuronmod.names`) i vraća igri njena dva posle svakog
preimenovanja koje nije bilo za njih. `OnDuelistnameSubmit` je direktan C# poziv koji hook ne
vidi, pa `watch_rename()` prati `TMP_InputField` — i **mora** da napuni polje imenom izabranog
igrača pri svakoj promeni selekcije, inače polje prosledi prethodno ime sledećem igraču.

**2. Skinovi crtaju ime na dva različita načina.** Neki kao TMP tekst, a 5D's, ARC-V i VRAINS
kao **sliku** (`Duelist/TextPlayer`, sprite `lp_5ds_bg_1p`/`_2p`) za koju igra nema art za
igrače 3–5. Tamo se ta slika krije a piše se u prazan TMP sused `Duelist/PlayerName`.
Kompletna procedura (kako se pronalazi pravi čvor diff-om dva stock panela, zašto se bira
*prazan* sused, zašto se ne sme kopirati rect) je u **`docs/DESIGN.md` §3.2**.

Predlog prethodnog agenta za ovaj kriterijum: `docs/STRATEGY-unique-names.md` — tretirati kao
predlog, ne kao utvrđeno stanje.

### Treperenje — regresija koju smo sami uveli (prijava korisnika, 2026-08-29)

> "kada uđem u aplikaciju ona tri ekrana zajedno sa druga dva svi trepere i posle nekog vremena
> ova tri skroz izgube tekst a ona dva nastavljaju da trepere... zatim sam ušao drugi put i
> tada sam opet video treperenje... a zatim sam treći put ušao i tad su imena bila zamrznuta,
> izgledala baš kako treba... kao da se baca kockica"

Treperenje znači da se u jednom frejmu ne vidi nijedno ime, a u sledećem svih pet. Nije
postojalo pre nekoliko verzija — uveli smo ga izmenama rađenim zbog VRAINS-a.

**Sve neproverene izmene su vraćene** na stanje builda koji je korisnik fotografisao
(`reference/screenshots/v231-*.png`): povučena je donja granica čitljivosti, povučeno je
proširenje brisanja na sopstveni natpis, i povučeno je stalno ponavljanje natpisa na panelima
1 i 2. Ostala je samo dijagnostika, koja ništa ne crta.

Razlog za povlačenje umesto još jedne popravke: nijedna od te tri izmene nije bila proverena na
telefonu (bežično otklanjanje grešaka je bilo isključeno), a korisnik je prijavio regresiju na
glavnim ekranima. Sledeći korak je **log, ne još jedna pretpostavka** — treperenje ostavlja
tragove koje mod već ispisuje:

| Linija | Šta znači |
|---|---|
| `cap: rejected - the search landed on LifePoints` koja se ponavlja | pretraga natpisa svaki frejm sleti na brojač, resetuje se i pokušava ponovo — to je "bacanje kockice" |
| `cap: len=…` sa različitim vrednostima kroz vreme | pretraga se ne slaže sama sa sobom između frejmova |
| `cap where:` sa različitim čvorom po ulasku | natpis završava u različitim čvorovima pri hladnom i toplom ulasku |

Sumnja: `panel_ready()` propušta pretragu pre nego što je skin popunio panel, a kad se dva
brojača razlikuju (7500 naspram 8000 na snimcima) diff sleti na `LifePoints`,
`caption_is_the_score()` ga odbije i sve se resetuje — pa naizmenično ima i nema natpisa.
Treći ulazak "zamrzne" jer se zatekne slučaj u kom se brojači poklapaju.


---

## Stanje na v232 — potvrdio korisnik 2026-08-29

**Rade svi osim VRAINS-a: Standard (1), Simple (2), Duel Monsters (3), GX (4), 5D's (5),
ZEXAL (6) i ARC-V (7).** Imena stoje na svom mestu,
nema precrtavanja, nema odsečene druge kopije, nema treperenja, preimenovanje ostaje posle
čuvanja. ARC-V u potpunosti zadovoljava ovaj kriterijum — `reference/screenshots/v232-arcv-ok.png`.

**Duel Monsters (3) je pao na v232 i popravljen je u v233.** Igrin natpis `DUELIST 01` se crtao
preko imena — `reference/screenshots/v232-duelmonsters-overlap.png`. Uzrok: v232 je čvorove
tražio po imenu lista, a taj skin ima dva čvora zvana `img`
(`Duelist/Background/line/img` i `Duelist/TextPlayer/img`), pa je mod skrivao ukrasnu liniju
umesto natpisa. Od v233 se pamti ceo lanac imena od korena panela.

Rešio ih je jedan kvar, ne tri: natpis se pamtio kao **redni broj deteta**, a `label_panel`
pomera natpis na kraj roditelja, čime prenumeriše svu decu iza njega. Ime je zato svaki frejm
odlazilo u sledeći čvor - `TextPlayer` → `PlayerName` → `Text_obj` → `Image` - upisujući se u
nov i brišući prethodni, i završavajući u spriteu koji tekst ne može ni da nacrta. Odatle
istovremeno i treperenje, i nestajanje, i odsečena kopija: `Text_obj` je 50x50 naspram 338
koliko ima natpis, taman za dva slova. Čvorovi se sada pamte po imenu (`panel_node()`).

### Edge case-ovi koje ovaj user story ne pokriva

Dizajni 6-8. Nisu deo onoga što je ovde prihvaćeno; uzeti u nekom trenutku:

- [x] **ZEXAL (6)** — ~~igrina reč i ime su se slagali jedno na drugo~~ **rešeno u v234.**
      Natpis je završavao u `Duelist/TextPlayer/01`, brojčanoj polovini podeljenog natpisa, dok
      je roditelj `TextPlayer` zadržavao reč `DUELIST`. Sada se, kada čvor koji se razlikuje
      sadrži samo cifre, bira prazno polje za ime.
- [ ] **VRAINS (8)** — nije provereno na v232. Na v231 se ime nije videlo; odnos 0.22,
      najmanji od svih.

Pokušaj podizanja veličine slova prema brojaču je jednom pisan i povučen jer nije bio proveren.
Sada se može uraditi kako treba: `scripts/device/logcat.sh` daje `cap where:` liniju sa imenom
čvora, njegovim pravougaonikom, položajem i veličinom slova naspram brojača, po panelu - dakle
prvo pogledati log na ta tri dizajna, pa tek onda menjati kod.
