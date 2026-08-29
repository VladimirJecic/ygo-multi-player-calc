# userstories/ — šta korisnik traži

Jedan fajl po zahtevu. Ovo je **definicija "gotovog"** za svaku novu verziju moda — ne
predlozi, nego uslovi prijema koje je korisnik izdiktirao.

## Pravila

1. **Ne prepričavaj korisnika.** Sekcija "Kako je zahtev postavljen" čuva njegove reči.
   Tehničko tumačenje ide posebno, ispod, i jasno je odvojeno.
2. **Svaki kriterijum mora biti proverljiv na telefonu.** Ako se ne može otkucati, tapnuti i
   videti, nije kriterijum prijema.
3. **Nijedna priča nije "gotova" dok ne prođe na svih osam dizajnova kalkulatora**
   (Standard, Simple, Duel Monsters, GX, 5D's, ZEXAL, ARC-V, VRAINS) i za 4 i za 5 igrača.
4. **Status se menja samo posle provere sa korisnikom.** On je test harness — nema
   automatskih testova. Ako ti nije potvrdio, status je `nije potvrđeno`, ne `radi`.
5. Novi zahtev = novi fajl `US-NN-kratak-naziv.md`, po istom šablonu. Ne prepravljaj stare
   priče kad stigne nov zahtev; stara priča je istorija onoga što je jednom radilo.

## Šablon

```markdown
# US-NN — naslov

**Status:** radi | delimično | ne radi | nije potvrđeno      **Poslednja provera:** vNNN

## Kako je zahtev postavljen
> korisnikove reči

## Kriterijumi prijema
- [ ] ...

## Kako se proverava
1. ...

## Tehnički
gde u kodu ovo živi, i šta je poznato da ume da pukne
```

## Pregled

| # | Priča | Status na v240 |
|---|---|---|
| 01 | Izbor broja ekrana u podešavanjima | radi |
| 02 | Pet kalkulatora, pravilno raspoređenih | delimično — raspored za 3 igrača nije potvrđen |
| 03 | Klik na kalkulator otvara tačno tog igrača | radi |
| 04 | Promena imena olovčicom, bez duplikata | radi na 1-7; ostaje VRAINS (8) |
| 05 | Log arhive i nastavak duela | radi |
| 06 | Opcija „3 ekrana" u podešavanjima | nije počelo |

Za tri igrača se piše više odvojenih priča, ne jedna: **06** je samo red u podešavanjima, a
izgled kalkulatora, funkcionalnost (poeni i ime), upis u Log i vraćanje iz Loga dobijaju svaka
svoju. Kad neka od njih bude naručena, dodaj je ovde.
