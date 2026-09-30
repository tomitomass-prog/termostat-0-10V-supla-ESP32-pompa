# Regulator pogodowy SUPLA 0-10 V dla ESP32

Projekt firmware dla **ESP32 DevKit V1**. Zastępuje firmware ESPHome i laczy
sie bezposrednio z SUPLA Cloud za pomoca biblioteki `SuplaDevice`. MQTT ani
Home Assistant nie sa wymagane.

## Najwazniejsze funkcje

- termostat/nastawa w aplikacji SUPLA jako nadrzedne polecenie,
- regulator pogodowy z regulowana krzywa,
- mozliwosc wylaczenia regulacji pogodowej w SUPLA lub dlugim nacisnieciem
  lokalnego przycisku,
- lokalny regulator PI/PID z anti-windup, filtrem czlonu D i ograniczeniem
  szybkosci zmiany wyjscia,
- sterowanie proporcjonalne 0-10 V:
  - automatycznie wykrywany GP8403 na I2C — wariant zalecany,
  - PWM na GPIO25 dla aktywnego konwertera PWM→0-10 V,
- trzy DS18B20: przed zaworem, za zaworem i na zewnatrz,
- sterowanie pompa obiegowa na GPIO26 z minimalna temperatura bufora; jako
  temperatura bufora wykorzystywany jest czujnik **przed zaworem**,
- Tmin bufora regulowana z aplikacji SUPLA jako dodatkowy kanal HVAC,
- tryb pompy AUTO oraz reczne ON/OFF z SUPLA; histereza i polaryzacja
  przekaznika ustawiane w Additional settings,
- OLED SSD1306 128x64 z szescioma ekranami,
- wszystkie pomiary, cel, procent, napięcie, alarm i aktywny sterownik jako
  kanaly SUPLA,
- praca lokalna po zaniku Internetu i zachowanie ostatniej nastawy,
- zapasowy zapis nastawy, trybu, regulacji pogodowej oraz ustawien pompy w NVS,
- portal konfiguracji Wi-Fi, serwera SUPLA, PID, krzywej, wyjscia i adresow
  czujnikow,
- przejscie regulatora do zadania 0% przy awarii czujnika za zaworem,
  przegrzaniu albo bledzie sterownika 0-10 V; gwarantowane fizyczne odciecie
  przy utracie lacznosci z DAC wymaga dodatkowego przekaznika bezpieczenstwa,
- lagodna degradacja przy awarii czujnika zewnetrznego: regulator przechodzi na
  sama nastawę SUPLA zamiast zatrzymywac ogrzewanie,
- skan adresow DS18B20 wypisywany do portu szeregowego przy starcie.

## Jak interpretowana jest nastawa SUPLA

Nastawa kanalu HVAC jest **bazowa temperatura wody za zaworem**, a nie
temperatura pokojowa. Ma najwyzszy priorytet i przesuwa cala krzywa 1:1.

Przy aktywnej regulacji pogodowej:

```text
T_docelowa = T_SUPLA
             + nachylenie * max(0, punkt_pogodowy - T_zewnetrzna)
             + korekta
```

Wynik jest ograniczony parametrami `tmin` i `tmax`.

Przy wylaczonej regulacji pogodowej:

```text
T_docelowa = T_SUPLA
```

Przyklad ustawien domyslnych: nastawa SUPLA 30 C, nachylenie 0,60, punkt
pogodowy 20 C. Dla 10 C na zewnatrz celem jest 36 C; dla 0 C celem jest 42 C.

## Adresy czujnikow przyjete z dostarczonego YAML

| Rola w nowym firmware | Adres Dallas | Pochodzenie |
|---|---|---|
| temperatura za zaworem | `286B577B0F000082` | `Temperatura Mieszacza1` |
| temperatura zewnetrzna | `281F5F7A0F000077` | `Temperatura Zewnetrzna` |
| temperatura przed zaworem | `28C4C17A0F00005E` | dawny czujnik `Temperatura Kotla` |

**Trzeci adres trzeba zweryfikowac fizycznie.** Oryginalny YAML nie wskazywal
wprost czujnika na wejsciu zaworu 3-drogowego, dlatego jako domyslny zostal
przyjety czujnik kotla. Kazdy adres mozna zmienic w lokalnym portalu SUPLA.

## Wyjscie 0-10 V

Domyslnie `outmode=0` (Auto):

1. firmware sprawdza GP8403 pod adresem `0x58`,
2. gdy odpowiada, ustawia zakres 0-10 V i uzywa kanalu 0,
3. gdy go nie ma, uruchamia PWM na GPIO25.

Wariant PWM wymaga **aktywnego konwertera PWM→0-10 V**, ktory akceptuje sygnal
3,3 V. Filtr RC sam w sobie da jedynie 0-3,3 V i nie jest zamiennikiem takiego
modulu. Dla instalacji grzewczej GP8403 jest bardziej przewidywalny i jest
wariantem referencyjnym.

Parametry `vmin`, `vmax` i `outrev` pozwalaja obsluzyc 0-10 V, 2-10 V oraz
odwrotny kierunek pracy siłownika.

## Sterowanie pompa obiegowa

Pompa jest sterowana przez **GPIO26**. Czujnik `DS18B20 wejscie` /
`Temperatura przed zaworem` jest jednoczesnie traktowany jako temperatura
bufora. Nie trzeba dodawac czwartego czujnika.

W trybie `Pompa AUTO = ON`:

```text
regulator HVAC STOP lub brak poprawnego pomiaru bufora -> pompa OFF
T_bufora < Tmin_bufora                                -> pompa OFF
T_bufora >= Tmin_bufora + histereza                  -> pompa ON
pomiedzy progami                                     -> zachowaj poprzedni stan
```

Domyslnie `Tmin_bufora = 35 C`, a histereza = `2 C`, wiec po wylaczeniu
ponizej 35 C pompa ponownie dostaje zezwolenie dopiero od 37 C.

`Tmin_bufora` jest wystawione jako osobny kanal HVAC w SUPLA, dlatego mozna
zmieniac prog zdalnie bez rekompilacji firmware. Po rejestracji warto nazwac
ten kanal np. **Tmin bufora / pompa**.

- `Pompa AUTO = ON` - sterowanie automatyczne wg temperatury bufora.
- `Pompa AUTO = OFF` - przejscie na sterowanie reczne.
- W trybie recznym kanal `Pompa recznie` bezposrednio ustawia ON/OFF.
- Kanal `Stan pompy` pokazuje faktyczny stan wyjscia GPIO26 jako 0/1.

Polaryzacja przekaznika jest konfigurowalna. Domyslnie przyjeto **aktywny LOW**
(`ppol=0`), zgodnie z popularnymi modulami przekaznikow.

## Praca bez Internetu

Regulator, odczyt DS18B20, PID, wyjscie 0-10 V, ekran i zabezpieczenia pracuja
w glownej petli ESP32, a nie w chmurze. Po utracie polaczenia uzywana jest
ostatnia nastawa i ostatni tryb. Dodatkowo firmware zapisuje je lokalnie po
5 sekundach stabilnego stanu, dzieki czemu sa dostepne rowniez po restarcie bez
Internetu.

Harmonogramy SUPLA zalezne od aktualnego czasu nie sa podstawowym mechanizmem
tego projektu. Dla autonomicznych harmonogramow po zaniku zasilania nalezy
rozbudowac uklad o RTC z bateria, np. DS3231.

## Obsluga przyciskow

- **GPIO27, krotko:** nastepny ekran OLED.
- **GPIO27, przytrzymanie ok. 1,8 s:** wlaczenie/wyłączenie pogody.
- **GPIO0/BOOT:** wejscie w tryb konfiguracji SUPLA.

## Ekrany OLED

1. procent otwarcia, napięcie, aktywny sterownik i stan pracy,
2. nastawa SUPLA, cel po krzywej, temperatura rzeczywista,
3. temperatura wejscia/bufora, wyjscia i roznica,
4. temperatura zewnetrzna i parametry krzywej,
5. pompa: temperatura bufora, Tmin, histereza, tryb i stan,
6. skladowe PID i kod alarmu.

## Parametry portalu

| Parametr | Domyslnie | Znaczenie |
|---|---:|---|
| `curve` | 0.60 | nachylenie krzywej |
| `balance` | 20.0 C | temperatura zewnetrzna, od ktorej krzywa nie podnosi celu |
| `corr` | 0.0 C | stala korekta krzywej |
| `tmin` / `tmax` | 20 / 70 C | granice celu |
| `kp` / `ki` / `kd` | 10 / 0.02 / 0 | PID w procentach wyjscia |
| `dfilt` | 0.20 | filtr czlonu D |
| `slew` | 100 %/min | maksymalna predkosc zmiany wyjscia |
| `safeout` | 75 C | blokada od temperatury za zaworem |
| `safein` | 90 C | blokada od temperatury przed zaworem |
| `outmode` | 0 | 0 Auto, 1 tylko GP8403, 2 tylko PWM |
| `outrev` | 0 | odwrocenie charakterystyki |
| `vmin` / `vmax` | 0 / 10 V | kalibracja wyjscia |
| `pwmhz` | 1000 Hz | czestotliwosc PWM |
| `phys` | 2.0 C | histereza ponownego zalaczenia pompy |
| `ppol` | 0 | polaryzacja pompy: 0 aktywny LOW, 1 aktywny HIGH |
| `addr_out` | wg tabeli | czujnik za zaworem |
| `addr_in` | wg tabeli | czujnik przed zaworem |
| `addr_ext` | wg tabeli | czujnik zewnetrzny |

Wspolczynniki PID sa punktem startowym. Odpowiadaja w przyblizeniu
wczesniejszemu ustawieniu ESPHome `kp=0.10`, `ki=0.0002` dla wyjscia 0-1,
przeliczonemu na skale 0-100%. Strojenie trzeba przeprowadzic na rzeczywistej
instalacji, zaczynajac od `kd=0`.

## Zabezpieczenia

- Brak lub nieaktualny pomiar za zaworem: 0% i alarm.
- Przegrzanie wejscia lub wyjscia: zatrzask blokady i 0%; odblokowanie po
  spadku o 3 C.
- Brak czujnika zewnetrznego: alarm i praca wedlug samej nastawy SUPLA.
- Brak czujnika wejscia: alarm; PID nadal pracuje na czujniku wyjscia, lecz
  zabezpieczenie wejscia jest niedostepne.
- Bledny adres DS18B20: blokada automatyki.
- Wymuszony GP8403 bez odpowiedzi: blokada automatyki. W trybie Auto wybor GP8403/PWM odbywa sie tylko przy starcie;
  utrata komunikacji z wybranym GP8403 zatrzymuje regulator i zglasza alarm.

Uklad wykonawczy powinien miec dodatkowe zabezpieczenia niezalezne od programu:
ogranicznik temperatury, prawidlowe bezpieczniki, separację zasilania i stan
bezpieczny siłownika po zaniku sterowania.

## Kompilacja

Projekt uzywa PlatformIO:

```bash
./scripts/test_native.sh
pio run -e esp32dev
```

Szczegoly i adresy flashowania sa w [docs/FLASHING.md](docs/FLASHING.md).
Automatyczny workflow GitHub Actions tworzy zarówno `firmware.bin`, jak i
jednoplikowy obraz `*-merged.bin` do pierwszego wgrania pod adres `0x0`.

W tej paczce nie ma nieskompilowanego ani pozornego pliku `.bin`. Aktualny zakres
weryfikacji, ograniczenia srodowiska budowania i obowiazkowe testy sprzetowe sa
opisane w [BUILD_STATUS.md](BUILD_STATUS.md).

## Dodatkowe funkcjonalnosci warte rozbudowy

Nastepne sensowne rozszerzenia, niewlaczone jeszcze do tej wersji:

- sprzezenie zwrotne 0-10 V z potencjometru/wyjscia U siłownika przez izolowany
  ADC, aby pokazywany procent byl pomiarem, a nie tylko poleceniem,
- opoznione wylaczenie pompy i funkcja antyzastaniowa,
- ochrona temperatury powrotu kotla,
- dwa zestawy krzywych dzien/noc,
- RTC DS3231 dla autonomicznych harmonogramow po zaniku sieci i zasilania,
- lokalny enkoder do zmiany nastawy bez telefonu,
- przekaznik bezpieczenstwa odcinajacy zasilanie siłownika przy alarmie
  sterownika 0-10 V,
- wejscie awaryjne termostatu mechanicznego,
- kontrola roznicy temperatur wejscie/wyjscie i alarm hydrauliczny,
- podpisane OTA po wykonaniu testow i zabezpieczeniu procesu aktualizacji.

## Pliki

- `src/main.cpp` — sprzet, SUPLA, OLED, DS18B20 i wyjscie 0-10 V,
- `src/HeatingCore.h` — niezalezny regulator pogodowy i PID,
- `test/native/test_heating_core.cpp` — testy logiki,
- `docs/WIRING.md` — polaczenia,
- `docs/CHANNELS.md` — mapa kanalow i alarmow,
- `.github/workflows/build.yml` — automatyczna kompilacja i scalanie obrazu.
