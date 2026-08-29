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
| 01 | Izbor 4 ili 5 ekrana u podešavanjima | radi |
| 02 | Raspored kalkulatora za 4 i 5 igrača | radi |
| 03 | Klik na kalkulator otvara tačno tog igrača (4 i 5) | radi |
| 04 | Promena imena olovčicom, bez duplikata (4 i 5) | radi na 1-7; ostaje VRAINS (8) |
| 05 | Log arhive i nastavak duela (4 i 5) | radi |
| 06 | Opcija „3 ekrana" u podešavanjima | radi (v241) |
| 07 | Izgled kalkulatora sa tri igrača | nije počelo |

**Priče 01-05 su za 4 i 5 ekrana.** Tri igrača su zaseban niz, i piše se više odvojenih
priča, ne jedna: **06** je samo red u podešavanjima, **07**
samo izgled, a funkcionalnost (poeni i ime), upis u Log i vraćanje iz Loga dobijaju svaka svoju. Kad neka od njih bude naručena, dodaj je ovde.
