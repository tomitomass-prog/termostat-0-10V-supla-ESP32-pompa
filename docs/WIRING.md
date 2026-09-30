# Schemat polaczen

## ESP32 DevKit V1

| Element | Zacisk modulu | ESP32 | Uwagi |
|---|---|---:|---|
| DS18B20 x3 | DATA | GPIO4 | rezystor 4,7 kOhm DATA–3,3 V |
| DS18B20 x3 | VCC | 3,3 V | nie stosowac zasilania pasozytniczego |
| DS18B20 x3 | GND | GND | wspolna masa |
| OLED SSD1306 | SDA | GPIO21 | I2C, adres 0x3C |
| OLED SSD1306 | SCL | GPIO22 | I2C |
| OLED SSD1306 | VCC | 3,3 V | sprawdz modul |
| GP8403 | SDA | GPIO21 | I2C, adres 0x58 |
| GP8403 | SCL | GPIO22 | I2C |
| GP8403 | VCC/GND | wg modulu | masa sygnalowa musi byc wspolna |
| GP8403 | OUT0 | Y siłownika | 0–10 V |
| przekaznik pompy | IN | GPIO26 | wyjscie cyfrowe; polaryzacja `ppol` |
| przekaznik pompy | VCC/GND | wg modulu | masa wspolna z ESP32; cewke zasil zgodnie z oznaczeniem |
| przycisk ekranow | styk | GPIO27–GND | INPUT_PULLUP |
| przycisk konfiguracji | BOOT | GPIO0–GND | zwykle jest na plytce |
| konwerter PWM (wariant) | PWM IN | GPIO25 | tylko wejscie zgodne z 3,3 V |
| konwerter PWM (wariant) | GND | GND | wspolna masa |
| przekaznik bezpieczenstwa (opcjonalny) | styk zasilania siłownika | osobny tor | gwarantuje odciecie przy awarii DAC/ESP |

## Wazne zasady

- Nie podawac 10 V na GPIO ESP32.
- Siłownik musi miec wejscie analogowe 0–10 V albo 2–10 V. Siłownika
  trzy-punktowego nie da sie przeksztalcic samym programem.
- Zasilanie silownika 24 V AC/DC wykonac zgodnie z jego dokumentacja.
- Dla wyjscia 2–10 V ustawic `vmin=2.00`, `vmax=10.00`.
- Gdy kierunek jest przeciwny, ustawic `outrev=1`.
- W trybie Auto obecny GP8403 ma pierwszenstwo. Gdy nie odpowiada juz przy
  starcie, firmware uruchamia PWM na GPIO25. Sterownik nie przelacza toru
  samoczynnie podczas pracy.
- Zwykly filtr RC nie tworzy poprawnego sygnalu 0–10 V z 3,3 V. Wariant PWM
  wymaga aktywnego konwertera PWM→0–10 V z odpowiednim zasilaniem i zgodnym
  poziomem logicznym wejscia.

## Stan bezpieczny

Przy bledzie odczytu temperatury algorytm zadaje 0%. Jezeli jednak GP8403
straci lacznosc I2C, moze fizycznie utrzymac ostatnie napiecie. Do instalacji,
w ktorej wymagane jest pewne zamkniecie zaworu, nalezy dodac niezalezny
przekaznik odcinajacy zasilanie siłownika albo tor sygnalowy, sterowany rowniez
przez termostat graniczny.

## Pompa

Domyslnie `ppol=0`, czyli przekaźnik jest traktowany jako **aktywny LOW**:
GPIO26=LOW oznacza ON, GPIO26=HIGH oznacza OFF. Dla modulu aktywnego HIGH
ustaw `ppol=1` w Additional settings.

Czujnik temperatury bufora to ten sam DS18B20, ktory jest skonfigurowany jako
`DS18B20 wejscie` / temperatura przed zaworem.
