# US-04 — Promena imena olovčicom, bez duplikata

**Status:** ne radi   **Poslednja provera:** v230

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
- [ ] **Na VRAINS dizajnu ime uopšte ne može da se postavi.**

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
