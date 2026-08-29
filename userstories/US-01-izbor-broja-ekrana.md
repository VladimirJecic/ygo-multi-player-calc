# US-01 — Izbor broja ekrana u podešavanjima

**Status:** radi   **Poslednja provera:** v230

## Kako je zahtev postavljen
> "pod jedan treba u podešavanjima omogućiti selekciju četiri ili pet screenova"

## Kriterijumi prijema
- [x] U sekciji za podešavanja kalkulatora postoje redovi za **4 ekrana** i **5 ekrana**,
      pored tri koja igra već ima.
- [x] Klikom na red, taj red se upali (radio dugme), a ostali se ugase.
- [x] Izbor preživi izlazak iz podešavanja i povratak u njih.
- [x] Izbor preživi gašenje i ponovno pokretanje aplikacije.
- [x] Postojeća tri režima igre (2 ekrana, jedan ekran vertikalno, jedan horizontalno)
      rade tačno kao pre — mod ih ne dira.

## Kako se proverava
1. Glavni meni → podešavanja kalkulatora.
2. Tapni "5 ekrana" → mora se upaliti samo taj red.
3. Nazad, pa opet u podešavanja → i dalje upaljen.
4. Ubij aplikaciju (`adb shell am force-stop jp.konami.YugiohOcgSupports`), pokreni je,
   opet u podešavanja → i dalje upaljen.
5. Vrati na "2 ekrana" i uđi u duel → stock ekran, nepromenjen.

## Tehnički
`Calculator.OnEnable` klonira redove, `OnClickCalcMode` prihvata režime 3 i 4,
`FixLayout` (korutina) prefarbava stock radio dugmad jedan frejm kasnije pa se naša
selekcija mora ponovo naneti posle nje. Trajno se pamti u
`files/neuronmod.mode` **i** tako što se pusti da originalni `OnClickCalcMode` odradi
svoj (bezuslovni) upis. Detalji: `docs/DESIGN.md` §2.

Poznato da ume da pukne: `BindingText` komponenta na klonu vraća originalni natpis;
`SetParent(parent)` sa jednim argumentom ostavlja klon nevidljiv ali on i dalje zauzima
mesto u layoutu.
