# DSH.md - Yu-Gi-Oh Neuron Modding / 5-Player Calculator

## Cilj projekta
Modifikacija zvanične Yu-Gi-Oh! Neuron aplikacije (koja koristi Unity) sa ciljem proširenja podrške za broj igrača sa inicijalnih **2 igrača na 5 igrača** (fleksibilno 4 ili 5 igrača).

## Alati za modovanje (Tools)
U okruženju i `ygo-source` folderu koriste se sledeći alati i skripte:
1. **Apktool / custom Python skripte (`mkapk.py`, `build.sh`):** Raspakivanje, smali modifikacija i rekompajliranje APK paketa.
2. **Unity IL2CPP & Metadata alati (`so.py`, `md.py`):** Mapiranje adresa i ekstrakcija `global-metadata.dat` za analizu C# klasa/metoda.
3. **Python & Bash skripte (`mod/`, `nav.sh`, `design.sh`):** Injektovanje izmena i podešavanje rasporeda ekrana.
4. **Alati za potpisivanje APK-a (`keys/`):** Kriptografsko potpisivanje modifikovanih APK paketa.

## Acceptance kriterijumi (Uslovi prihvatanja za svaku verziju)

1. **Podešavanja za broj igrača:**
   - U sekciji za podešavanja aplikacije omogućiti korisniku selekciju broja ekrana / igrača (podrška za **4 ili 5 screenova**).

2. **Prikaz kalkulatora u duelu:**
   - Klikom na sekciju za duel, unutar ekrana mora biti prikazano tačno **pet kalkulatora** (ili u skladu sa podešenim brojem igrača), koji moraju biti pravilno i ravnomerno raspoređeni.

3. **Interakcija i nezavisnost kalkulatora:**
   - Svaki kalkulator, kada se klikne, otvara prikaz trenutnog broja bodova tog specifičnog igrača, njegovo ime i opcije za promenu bodova.
   - Svako ime kalkulatora mora biti jedinstveno, u potpunosti odgovarati kalkulatoru koji je kliknut, i isto važi za broj bodova (npr. ako je kliknut kalkulator sa 7.000 bodova, otvara se kalkulator tačno sa tim bodovima i odgovarajućim identitetom).

4. **Promena imena igrača (Olovčica) i sprečavanje duplikata/klipovanja:**
   - Korisnik aplikacije na svakom ekranu/kalkulatoru ima opciju da klikom na olovčicu promeni svoje ime.
   - Nakon što je promenio ime i zatvorio kalkulator, **to isto ime se prikazuje isključivo za taj specifični kalkulator i ni za jedan drugi**.
   - **Zabranjeni su duplikati i vizuelne greške** (kao što je prikazano na dijagnostičkoj slici `1787971323627-1000202565.png`, gde se imena ponavljaju, npr. više "Aleksa" unutar 5-player prikaza, ili dolaze do polovičnog/klipovanog ispisivanja teksta i presecanja sa ivicama okvira). Svaki kalkulator mora imati jedinstveno ime i uredno centriran tekst bez preklapanja sa elementima interfejsa.

5. **Log archives (Arhiva duela i nastavak igre):**
   - Igrač iz glavnog menija može da vidi istoriju prethodnih duela (`log archives`).
   - Klikom na svaki pojedinačni duel, korisnik se uverava da su **svih pet igrača u logu prisutni**.
   - Klikom na opciju **"restart duel from log"**, igra se nastavlja tačno od mesta gde je taj duel stao.
