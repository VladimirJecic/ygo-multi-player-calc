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

## Utvrđena činjenica: polje postoji

**`Life01/Duelist/PlayerName` postoji na VRAINS-u. Ne treba praviti nov lejer.**

Ovo nije zaključak nego snimak žive aplikacije. `scripts/device/gfxsweep.sh` prošeta kroz
dizajne i zapiše šta panel stvarno crta; rezultat je `reference/gfx_inventory.txt`:

| dizajn | čvor | kutija |
|---|---|---|
| standard | `/Life01/Duelist/PlayerName` | 3.76 x 0.44 |
| simple | `/Life01/Duelist/PlayerName` | 6.44 x 0.44 |
| duelmonsters | `/Life01/Duelist/PlayerName` | 3.16 x 0.64 |
| gx | `/Life01/Duelist/PlayerName` | 4.44 x 1.22 |
| 5ds | `/Life01/Duelist/PlayerName` | 6.44 x 1.42 |
| zexal | `/Life01/Duelist/PlayerName` | 3.19 x 0.28 |
| arcv | `/Life01/Duelist/PlayerName` | 3.44 x 0.56 |
| **vrains** | **`/Life01/Duelist/PlayerName`** | **2.22 x 0.24** |

Čvor je na **svih osam**, istog imena i na istom mestu u stablu — deo je zajedničkog prefaba
panela, ne nečega što VRAINS ima ili nema. Na svih osam je i **ugašen** (`hidden`), jer ga skin
isporučuje isključenog i pali ga tek kad korisnik unese svoje ime; to je normalno stanje, ne
kvar, i mod ga svuda pali sa `set_active`.

Dakle pitanje nije „da li postoji" nego **„zašto ne crta"**.

## Šta ostaje da se utvrdi

- [ ] **Zašto `PlayerName` na VRAINS-u ne crta ništa**, iako je aktivan, prave veličine, na
      pravom mestu i sadrži ime.

Ranija sesija je taj odgovor već zapisala u kod (`src/native/mod.c`, oko `label_panel`):

> VRAINS' PlayerName is active, opaque, the right size and in the right place, and holds the
> name — and shows nothing, because the font it was built with is not in this trimmed-down APK
> any more.

Sumnja dakle ide na **font, ne na lejer**. Mod već ima rezervu: pita TMP koliko je string širok
i da li materijal uopšte ima teksturu, pa ako ne, pozajmi font kojim panel crta svoj brojač.
Ta rezerva ili ne opali ili nije dovoljna — **to je prvo što treba videti u logu**, i to je
jedina stvar koja još nije potvrđena merenjem.

### Šta se ne sme raditi

**Ne porediti sa dekodiranim stablom** (`apk/decoded/`). To je verzija kojoj su lejeri greškom
obrisani i po tome je manja; poređenje sa njom vodi na pogrešan zaključak. Netaknut Neuron apk
koji bi presudio šta je originalno postojalo **više ne postoji**.

**Ovo nije kvar moda.** Ime se ne vidi ni u igrinim sopstvenim režimima — 2 ekrana, vertikalno,
horizontalno — u koje mod ne dira. Stock igra na VRAINS-u ne ume da prikaže ime, a mod to samo
nasleđuje.

**Dva skina istog oblika rade.** 5D's i ARC-V takođe crtaju natpis kao sliku i nemaju art za
igrače 3-5, pa je razlika između njih i VRAINS-a najkraći put do odgovora.

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
Pre toga vredi probati jeftiniju stvar: font se već pozajmljuje od brojača na istoj tabli, pa
ako to proradi, nikakav asset ne treba dodavati.
