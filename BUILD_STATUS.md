# Status przygotowania firmware

Data kontroli: **2026-08-12**

## Wykonane kontrole

- testy regulatora pogodowego, PID, anti-windup, ograniczenia narastania i
  zabezpieczen: **zaliczone** (`./scripts/test_native.sh`),
- kontrola skladni `src/main.cpp` ze stubami interfejsow Arduino/SUPLA:
  **zaliczona** (`./scripts/syntax_check_main.sh`),
- porownanie uzytych metod z naglowkami `SuplaDevice` v26.4
  (uruchomienie protokolu SUPLA v23),
- porownanie protokolu GP8403 z oficjalna biblioteka DFRobot.

## Czego nie wykonano w tej paczce

Nie dolaczono pliku `.bin`, poniewaz srodowisko, w ktorym przygotowano projekt,
nie mialo kompilatora Xtensa/ESP32 ani PlatformIO i nie pozwalalo pobrac
pakietow kompilatora. Plik udajacy firmware bylby niebezpieczny i bezuzyteczny.

Rzeczywiste linkowanie dla ESP32 jest wykonywane przez dolaczony workflow
`.github/workflows/build.yml` albo lokalnie poleceniem:

```bash
pio run -e esp32dev
```

Workflow tworzy jednoplikowy obraz:

```text
sterownik-ogrzewania-supla-esp32-merged.bin
```

przeznaczony do wgrania pod adres `0x0` na **ESP32 DevKit V1 / ESP32-WROOM-32,
4 MB flash**.

## Obowiazkowa kontrola na rzeczywistym sprzecie

Przed podlaczeniem silownika zmierzyc multimetrem 0 V, ok. 5 V i ok. 10 V,
sprawdzic role wszystkich trzech DS18B20 oraz kierunek pracy siłownika.
Pokazywany procent jest wartoscia zadana, a nie pomiarem mechanicznego polozenia,
dopoki nie zostanie dodane niezalezne sprzezenie zwrotne. Utrata lacznosci I2C
z GP8403 moze pozostawic ostatnie napiecie na wyjsciu; pewny stan bezpieczny
wymaga niezaleznego przekaznika lub termostatu granicznego.

## Zmiana 1.1.0-pump

Dodano sterowanie pompa na GPIO26, z Tmin bufora ustawiana z SUPLA, histereza,
trybem AUTO/recznym oraz podgladem stanu. Czujnikiem bufora jest istniejacy
DS18B20 przed zaworem. Po zmianie kontrola skladni i testy HeatingCore przechodza
lokalnie; pelny firmware nalezy zbudowac w GitHub Actions jak poprzednio.
