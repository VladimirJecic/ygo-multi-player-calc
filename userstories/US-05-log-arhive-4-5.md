# US-05 — Log arhive i nastavak duela (4 i 5 ekrana)

**Status:** radi   **Poslednja provera:** v230

## Kako je zahtev postavljen
> "igrač iz glavnog menija treba u log archives da može da vidi istoriju prethodnih duela, da
> klikne na svaki pojedinačno, uveri se da su svih pet igrača u logu prisutni i klikom na
> opciju restart duel from log da nastavi gde je stao taj duel"

## Kriterijumi prijema
- [x] Log dijalog unutar duela prikazuje **onoliko kolona koliko ima igrača** (4 ili 5), ne dve.
- [x] Zaglavlja kolona nose imena igrača, sva iste veličine slova.
- [x] Svaki red pokazuje ko je i za koliko promenio bodove.
- [x] Poništeni (undo) red i dalje nosi svoju strelicu unazad.
- [x] Iz glavnog menija, Log Archives lista prikazuje prethodne duele, sa imenima i vremenom.
- [x] Otvaranje jednog duela pokazuje svih pet igrača.
- [x] "Restart duel from log" nastavlja duel tačno tamo gde je stao — bodovi i imena.

## Kako se proverava
1. Pet igrača, tri promene bodova od tri različita igrača.
2. Otvori Log → pet kolona, imena u zaglavljima, tri reda sa ispravnim iznosima.
3. Jednu promenu poništi (Undo) → red ostaje sa strelicom.
4. Izađi iz duela. Glavni meni → Log Archives → taj duel je u listi, sa imenima i vremenom.
5. Otvori ga → svih pet igrača.
6. "Restart duel from log" → bodovi i imena su tačno kao pre izlaska.
7. `scripts/device/logtest.sh` radi korake 1–2 automatski.

## Tehnički
`LifeLog` u igri snima samo `LifeOfPlayer1/2`, pa mod vodi svoju tabelu
(`g_logStart[5]`, `g_logLife[128][5]`) i piše je u `files/neuronmod.logdb` (poslednjih 40
duela). Isti `table_rows()` crta i dijalog u duelu i sačuvani log ekran.

Raspored kolona, pinovanje veličine fonta i obrada Log Archives liste (imena, vremenska
oznaka, sečenje footera koji vraća svoje tabove na četiri različita događaja) su u
**`docs/DESIGN.md` §5**. Vraćanje duela ide preko `player_set_life()` / `player_set_name()`
direktno, a ne preko `AddLife`, jer `AddLife` upisuje i istoriju — a vraćanje duela nije potez.

## Razvojna napomena (v237)

Od v237 svaki bild briše modovu bazu sačuvanih partija (`files/neuronmod.logdb`), da Log ne bi
kretao pun testnih duela iz prethodne sesije. `build.sh` ostavi marker pored igrinih eksternih
fajlova, mod ga na sledećem startu vidi, obriše bazu i ukloni marker — jednokratno po bildu.

Briše se modova tabela životnih poena po duelu, a od v239 i **igrina sopstvena lista Log
Archives** — isprazni se i upiše nazad kroz `SaveData.SaveLogArchives`, pa ostane prazna.
Lista se prazni pri prvom otvaranju Loga posle bilda, ne pri učitavanju: runtime tada još nije
prikačen.
