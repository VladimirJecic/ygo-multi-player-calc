> **Zastarelo.** Ovo je predlog prethodnog agenta, napisan pre nego što je US-04 rešen, i
> njegova analiza nije bila tačna — govori o XML layoutima kojih u Unity/IL2CPP build-u nema.
> Kriterijum je rešen sasvim drugačije; kako, piše u `userstories/US-04-jedinstvena-imena-4-5.md`
> i u `docs/DESIGN.md` §3.2. Zadržano samo kao zapis šta je jednom bilo predloženo.

# Alati za modovanje Yu-Gi-Oh! Neuron aplikacije (Unity / Android)

U ovom okruženju i `ygo-source` folderu koriste se sledeći alati i skripte za disasemblovanje, modovanje, rekompajliranje i potpisivanje APK fajlova:

1. **Apktool / custom Python skripte (`mkapk.py`, `build.sh`):**
   - Za raspakivanje (`decode`) APK paketa, manipulaciju smali kodom i srednjim resursima, kao i ponovno pakovanje (`build`).
2. **Unity IL2CPP & Metadata alati / disasembleri (`so.py`, `md.py` u `work/out/`):**
   - S obzirom na to da je aplikacija napisana u Unity-ju (C# kompajliran kroz IL2CPP u `libil2cpp.so`), ovi alati služe za mapiranje adresa, ekstrakciju `global-metadata.dat` i analizu C# klasa/metoda.
3. **Python & Bash skripte za automatizaciju (`mod/`, `nav.sh`, `design.sh`):**
   - Skripte za injektovanje izmena, podešavanje rasporeda ekrana (layout designs) i testiranje logova (`modlog*.txt`).
4. **Alati za potpisivanje APK-a (`keys/`):**
   - Kriptografsko potpisivanje modifikovanih APK paketa (npr. `neuron-mod-v230.apk`) kako bi bili spremni za instalaciju na Android uređaj.

---

# Strategija za rešavanje 4. Acceptance Kriterijuma (Jedinstvena imena igrača i sprečavanje duplikata)

## Trenutni problem
Kao što je uočeno na dijagnostičkoj slici (`1787971323627-1000202565.png`), nakon unosa imena preko opcije sa olovčicom i zatvaranja kalkulatora, dolazi do pojave dupliranih imena (npr. više instanci sa imenom "Aleksa") i polovičnog/klipovanog ispisivanja teksta na pojedinim od 8 dizajnova kalkulatora.

## Analiza uzroka (Root Cause)
1. **Upravljanje state-om / ID-jevima igrača u Unity/C# kodu:**
   - Originalna aplikacija je hardkodovana za 2 igrača (npr. Player 1 i Player 2, sa indeksima `0` i `1`). Kada se modifikacijom proširi na 5 igrača, logika za dodelu ID-jeva u UI skriptama (npr. u `PlayerManager` ili `CalculatorView`) verovatno koristi deljeni state ili pogrešno mapira indekse `3`, `4` i `5` nazad na postojeće string ključeve, što dovodi do delimičnog prebrisavanja i dupliranja imena (npr. centar i donji desni ugao dele isti referentni string buffer).
2. **Layout UI ograničenja na 8 različitih dizajnova:**
   - Neki dizajni iz tačke 3 (od 8 ukupno) koriste fiksne širine i pozicije tekstualnih polja za imena. Kada se doda 5. igrač ili promeni ime duže od predviđenog, tekst se preseca (klipuje) ili izlazi iz okvira (glowing blue borders).

## Strategija rešavanja (Action Plan)
1. **Izolacija indeksa igrača u C# / Smali kodu:**
   - Identifikovati metode zadužene za čuvanje i učitavanje imena igrača na osnovu njihovog jedinstvenog indeksa (`playerIndex` od 0 do 4, umesto da se oslanjaju na deljene indekse 0 i 1).
   - Obezbediti da svaka instanca kalkulatora poziva jedinstveni getter/setter za ime (npr. `GetPlayerName(int index)` i `SetPlayerName(int index, string name)`).
2. **Korekcija UI šablona za svih 8 dizajnova:**
   - Pregledati XML layout fajlove / prefab komponente u `res/` i Unity assetima za svih 8 dizajnova kalkulatora.
   - Prilagoditi veličinu fonta (auto-fit/shrink-to-fit) i padding polja za unos imena kako nijedno ime ne bi izlazilo iz okvira ili se preklapalo sa ivicama.
3. **Validacija i Vibe-coding testiranje:**
   - Primeniti zakrpu, rekompajlirati u `neuron-mod-v231.apk`, testirati sa korisnikom i verifikovati u logovima da su svih 5 imena unikatna i ispravno prikazana na svih 8 dizajnova.
