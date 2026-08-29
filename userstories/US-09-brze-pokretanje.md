# US-09 — Brže pokretanje aplikacije

**Status:** nije počelo, kriterijumi još nisu dogovoreni      **Poslednja provera:** —

## Kako je zahtev postavljen
> "bilo bi sjajno ako bi moglo da se ubrza pokretanje aplikacije"

Dogovoreno da ide kao zasebna priča i da se ne dira dok korisnik ne kaže.

## Šta je u obimu

Vreme od tapa na ikonu do trenutka kad se može ući u duel.

**Kriterijumi prijema nisu napisani** — treba prvo izmeriti koliko sad traje i dogovoriti šta
je „dovoljno brzo". Bez izmerenog polazišta ovo bi bio zahtev bez definicije gotovog, a to je
protiv pravila ovog foldera.

## Šta se već zna

Verovatno je isti uzrok kao i gigabajt u memoriji, pa ove dve stvari možda idu zajedno:

- Igra u radu zauzima **TOTAL PSS 1070 MB**, od čega **474 MB grafike**, na telefonu sa oko
  1.5 GB slobodnog. To je i razlog zašto je Android ubija u pozadini (`docs/HISTORY.md`).
- Na disku je aplikacija smanjena sa **699 MB na 190 MB** izbacivanjem OCR-a i baze karata.
  Taj posao je stao na disku i nije nastavljen na ono što se učitava u memoriju.
- Mod svoje radi kasno i jeftino: čeka da se runtime digne prateći `/proc`, i tek onda kači
  hookove (`docs/ARCHITECTURE.md`). Malo je verovatno da on nosi merljiv deo vremena
  pokretanja, ali to treba izmeriti pre nego što se tvrdi.

## Prvi korak, kad se krene

Izmeriti, ne pretpostaviti. `ActivityTaskManager: Displayed ... +NNNms` u logcat-u daje vreme
do prvog iscrtavanja, a `=== neuronmod loaded ===` i `libil2cpp base` daju trenutke koji su
naši. Razlika između njih pokazuje da li uopšte ima šta da se traži na našoj strani.
