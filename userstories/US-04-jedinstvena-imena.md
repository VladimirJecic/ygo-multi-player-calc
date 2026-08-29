# US-04 — Promena imena olovčicom, bez duplikata

**Status:** ne radi   **Poslednja provera:** v230   **Popravka čeka proveru:** v231

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

      Za ZEXAL i VRAINS odnos objašnjava simptom, pa v231 uvodi donju granicu čitljivosti:
      veličina slova natpisa se meri prema brojaču na istoj tabli i podiže na 0.40 njegove
      veličine samo ako je ispod 0.34. Dizajni kod kojih je natpis već dovoljno veliki se ne
      diraju. **ARC-V time nije objašnjen** — odnos mu je isti kao kod Duel Monsters-a koji
      radi — pa je za njega dodata dijagnostika `cap where:` koja ispisuje ime čvora, njegov
      pravougaonik i položaj u svetu, po panelu.
- [ ] **Na ZEXAL-u prvo slovo imena je krupnije i drugog oblika** ("Laza" na donjem levom
      panelu). Nije objašnjeno; verovatno zaostala kopija ispod naše, pošto ZEXAL drži natpis u
      dva čvora (`TextPlayer` i `TextPlayer/01`). Čeka log.
- [ ] **Na Standardu pored imena stoji odsečen ostatak igrine oznake** ("H", "Vl", "Al", "La").
      Vidi se na `v231-*` snimcima; brisanje koje to treba da počisti poredi ceo string sa
      igrina dva imena, pa odsečenu kopiju ne prepozna.
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
