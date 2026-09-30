# Kompilacja i wgrywanie

## PlatformIO lokalnie

1. Zainstaluj Python 3 i pioarduino Core.
2. W katalogu projektu wykonaj:

```bash
pio run -e esp32dev
```

Plik aplikacji powstanie jako:

```text
.pio/build/esp32dev/firmware.bin
```

Przy aktualizacji samej aplikacji jest on wgrywany pod adres `0x10000`.

## Jednoplikowy obraz pierwszego uruchomienia

Workflow `.github/workflows/build.yml` tworzy:

```text
sterownik-ogrzewania-supla-esp32-merged.bin
```

Ten plik obejmuje bootloader, tablicę partycji i aplikację. Wgrywa się go pod
adres `0x0`, np.:

```bash
python -m esptool --chip esp32 --port COM5 erase_flash
python -m esptool --chip esp32 --port COM5 write_flash 0x0 sterownik-ogrzewania-supla-esp32-merged.bin
```

Na Linuksie port moze miec postac `/dev/ttyUSB0`.

## Pierwsze uruchomienie

1. Po uruchomieniu bez zapisanej konfiguracji ESP32 wejdzie w tryb konfiguracji.
2. Polacz telefon z siecia Wi-Fi utworzona przez urzadzenie SUPLA.
3. Otworz `192.168.4.1`.
4. Ustaw Wi-Fi, adres serwera SUPLA, e-mail/rejestracje oraz parametry regulatora.
5. Zapisz i zrestartuj urzadzenie.

Przycisk BOOT (GPIO0) sluzy do ponownego wejscia w konfigurację zgodnie z
mechanizmem `configureAsConfigButton` biblioteki SuplaDevice.
