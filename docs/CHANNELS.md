# Kanaly SUPLA

Numery sa stale dla tej wersji firmware. Nazwy kanalow mozna nadac w aplikacji
lub SUPLA Cloud po rejestracji urzadzenia.

| Kanal | Zalecana nazwa | Typ | Znaczenie |
|---:|---|---|---|
| 0 | Regulator mieszacza | HVAC/termostat | wlacz/wyłącz i bazowa nastawa temperatury |
| 1 | Temperatura za zaworem | termometr | temperatura regulowana na wyjsciu mieszacza |
| 2 | Temperatura bufora / przed zaworem | termometr | temperatura zasilania i jednoczesnie pomiar do sterowania pompa |
| 3 | Temperatura zewnetrzna | termometr | czujnik pogodowy |
| 4 | Regulacja pogodowa | wlacznik | ON: krzywa aktywna; OFF: cel = nastawa SUPLA |
| 5 | Otwarcie zaworu | GPM | wyjscie logiczne 0-100% |
| 6 | Napiecie zaworu | GPM | zadane napiecie 0-10 V |
| 7 | Temperatura docelowa | termometr | wynik nastawy i krzywej pogodowej |
| 8 | Alarm | GPM | bitowa maska alarmow |
| 9 | Sterownik wyjscia | GPM | 1=GP8403, 2=PWM, 0=brak |
| 10 | Tmin bufora / pompa | HVAC/termostat | zdalna nastawa minimalnej temperatury bufora, domyslnie 35 C |
| 11 | Pompa AUTO | wlacznik | ON=automatyka temperatury; OFF=tryb reczny |
| 12 | Pompa recznie | wlacznik | uzywany tylko gdy Pompa AUTO=OFF |
| 13 | Stan pompy | GPM | 1=GPIO26 zalacza pompe, 0=pompa wylaczona |

## Logika pompy AUTO

- pomiar temperatury: kanal 2, czyli DS18B20 `przed zaworem`,
- gdy glowny HVAC jest w STOP -> pompa OFF,
- brak poprawnego pomiaru bufora -> pompa OFF,
- `Tbuf < Tmin` -> pompa OFF,
- `Tbuf >= Tmin + histereza` -> pompa ON,
- pomiedzy progami stan jest podtrzymywany.

Histereza jest parametrem `phys` w Additional settings, domyslnie 2 C.

## Maska alarmu kanalu 8

| Bit | Wartosc | Znaczenie |
|---:|---:|---|
| 0 | 1 | blad/brak czujnika temperatury za zaworem - wyjscie zatrzymane |
| 1 | 2 | blad/brak czujnika wejscia / bufora |
| 2 | 4 | blad/brak czujnika pogodowego - przejscie na sama nastawe SUPLA |
| 3 | 8 | blad sterownika 0-10 V; program zadaje 0% |
| 4 | 16 | przekroczenie temperatury bezpiecznej - wyjscie zatrzymane |
| 5 | 32 | bledna konfiguracja adresow DS18B20 |

Niska temperatura bufora nie jest alarmem - jest normalnym warunkiem blokady
pompy.
