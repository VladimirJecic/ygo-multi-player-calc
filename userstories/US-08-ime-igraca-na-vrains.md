# US-08 — Ime igrača na VRAINS dizajnu

**Status:** nije počelo      **Poslednja provera:** —

## Kako je zahtev postavljen
> "sledeći user story Vrains ekran, dakle kako prikazati imena na njemu a da se ostali ekrani ne
> pokvare... Nije isključeno čak i da su greškom lejeri za ime obrisani jer se ne vide ni na
> 2,3,4,5 screens ni vertical ni horizontal... nisam uspeo da nađem originalni layer za ime, da
> li bi bilo komplikovano rekonstruisati ga ako ne postoji, da li postoji — to je tema
> istraživanja za user story"

Obim potvrđen odgovorom: **svi režimi na VRAINS-u** — 2, 3, 4 i 5 ekrana, vertikalno i
horizontalno.

## Šta je u obimu

Ime igrača da se vidi na VRAINS dizajnu, u svim režimima, **bez kvarenja preostalih sedam
dizajna**.

## Istraživanje pre koda

Ovo je prva priča koja počinje pitanjem, ne izmenom. Odgovoriti pre nego što se dira kod:

- [ ] **Da li polje za ime uopšte postoji na VRAINS-u, ili je izgubljeno?** Korisnikova sumnja
      je da je lejer obrisan pri smanjivanju aplikacije.
- [ ] **Ako ne postoji — koliko je teško rekonstruisati ga?**
- [ ] Ako postoji a ne crta se — **šta tačno nedostaje**: font, materijal, atlas, ili je čvor
      ugašen.

### Šta se već zna, da se ne istražuje ponovo

**Ovo nije kvar moda.** Korisnik javlja da se ime ne vidi ni u igrinim sopstvenim režimima —
2 ekrana, vertikalno, horizontalno — u koje mod uopšte ne dira. Znači stock igra na VRAINS-u
ne ume da prikaže ime, a mod to samo nasleđuje.

**Čvor postoji.** Iz `reference/gfx_inventory.txt`:

```
/Life01/Duelist/Background      8.67x3.89   img
/Life01/Duelist/TextPlayer      2.22x0.24   img
/Life01/Duelist/TextPlayer/01   2.22x0.24   img
/Life01/Duelist/PlayerName      2.22x0.24   hidden
```

`PlayerName` je tu, kao i na svih osam dizajna. Kutija mu je najmanja od svih —
2.22 x 0.24 naspram 3.76 x 0.44 na Standardu.

**Postoji ranija dijagnoza, zapisana u kodu** (`src/native/mod.c`, oko `label_panel`):

> VRAINS' PlayerName is active, opaque, the right size and in the right place, and holds the
> name — and shows nothing, because the font it was built with is not in this trimmed-down APK
> any more.

Dakle sumnja ide na **font, ne na lejer**. Mod već ima rezervu za to: izmeri koliko je string
širok, i ako izađe kao ništa, pozajmi font kojim panel crta svoj brojač. Ta rezerva očigledno
ili ne opali ili nije dovoljna — to je prvo što treba proveriti u logu.

**Lokalno nema dokaza da su asseti obrisani.** Trenutni apk ima **1795** asset ulaza naspram
**1785** u dekodiranom stablu, jer je `src/tools/addassets.py` već vratio 977 izgubljenih
bundle-ova. Netaknuti Neuron apk za poređenje **više ne postoji**, pa se gubitak ne može
dokazati poređenjem — samo posredno, po tome šta u runtime-u nedostaje.

**Dva skina istog oblika rade.** 5D's i ARC-V takođe crtaju natpis kao sliku i nemaju art za
igrače 3-5, i na njima ime radi. Šta VRAINS ima drugačije od njih je najkraći put do odgovora.

## Kriterijumi prijema
- [ ] Ime igrača se vidi na VRAINS dizajnu, u svih šest režima: **2, 3, 4 i 5 ekrana,
      vertikalno i horizontalno**.
- [ ] Ime je ono koje je korisnik uneo, za tog igrača, i ostaje posle čuvanja.
- [ ] Čita se — nije presitno, nije odsečeno, ne izlazi iz table.
- [ ] **Preostalih sedam dizajna ostaje tačno kako je sada**, u svim režimima.
- [ ] Ako se ispostavi da nešto nedostaje u apk-u, u `docs/BUILD.md` piše **šta** je dodato i
      **odakle**, da se sledeći build ne napravi bez toga.

## Kako se proverava
1. Podešavanja → VRAINS dizajn. Uđi u duel na 2 ekrana (igrin režim) — ime mora da se vidi.
2. Isto za vertikalno i horizontalno.
3. Isto za 3, 4 i 5 ekrana.
4. Preimenuj igrača, izađi iz duela, vrati se — ime je i dalje tu.
5. Prođi preostalih sedam dizajna i potvrdi da su nepromenjeni.

## Tehnički

Prvo log, pa kod. `scripts/device/logcat.sh` na VRAINS-u daje:

| Linija | Šta znači |
|---|---|
| `label: borrowed the score's font - the caption's own draws nothing` | rezerva je opalila, a ime se i dalje ne vidi → problem nije samo font |
| `label: caption font is unusable and there is nothing to borrow` | ni brojač nema upotrebljiv font na toj tabli |
| `cap where: 'PlayerName' rect ... fs X vs score Y` | gde je čvor završio i koliko mu je font velik |
| nijedne od ove tri | natpis se uopšte ne piše — problem je u pretrazi, ne u crtanju |

Provera koja razdvaja „font nedostaje" od „čvor se ne crta": mod pita TMP koliko je string
širok (`get_preferredWidth`) i da li materijal uopšte ima teksturu (`get_mainTexture`).
Metrika fonta preživi i kad atlas ne preživi, pa string može da se izmeri savršeno a da se ne
nacrta ništa — zato se gleda tekstura, ne samo širina.

Ako se potvrdi da nedostaje asset: `src/tools/addassets.py` je već jednom vratio 977 bundle-ova
iz donorskog apk-a i to je put kojim bi se išlo — ali donor više ne postoji, pa ga treba nabaviti.
