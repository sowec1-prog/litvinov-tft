# Litvínov & Sparta TFT

Samostatný projekt pro barevný dotykový ILI9341 panel `320 × 240` na šířku a ESP32-S3 DevKitC-1 N16R8.

- `firmware/` — ESP32-S3 klient. Stahuje pouze malý JSON a bude vykreslovat lokální loga z LittleFS.
- `server/` — nezávislé API pro hokej a fotbal. Zatím obsahuje testovací fixture data; stávající Litvínov OLED server nemění.
- `assets/` — zdrojové assety pro desktopový náhled a později pro převod do LittleFS.
- `firmware/data/logos/` — 14 znaků ELH a 16 znaků Chance Ligy pro sezonu 2026/27; manifest uchovává kód, zdroj a SHA-256 každého souboru.
- `ui-preview.html` — klikací PC návrh obrazovek.

## Lokální test serveru

```bash
cd server
python -m unittest -v
python app.py
```

Ve druhém terminálu:

```bash
curl http://127.0.0.1:8080/healthz
curl http://127.0.0.1:8080/api/v1/sport/hockey
curl http://127.0.0.1:8080/api/v1/sport/football
```

API je zatím dostupné pouze lokálně na `127.0.0.1:8080`; neotevírá port do internetu.

## Kontrakt pro panel

`GET /api/v1/sport/{hockey|football}` vrací mimo jiné:

- `state`: `scheduled`, `live` nebo `finished`
- domácí a hostující tým, včetně krátkého kódu a klíče loga
- `match_start_epoch`, `server_epoch`, `is_match_day`, `timezone`

Panel vypočítává odpočet sám mezi poll intervaly: dny → hodiny v den zápasu → minuty v poslední hodině → `MM:SS` v posledních 10 minutách.

## Firmware

1. Zkopíruj `firmware/include/config.h.example` jako `firmware/include/config.h`.
2. Vyplň pouze lokálně Wi-Fi a adresu serveru. `config.h` je ignorován Gitem.
3. Než se připojí TFT, první upload je pouze síťový/JSON test; neobsahuje ovladač displeje ani dotyku.
4. Vyplň lokálně jedinečné `OTA_PASSWORD` (minimálně 12 znaků). První upload firmware musí proběhnout přes USB; následné firmware a LittleFS aktualizace mohou běžet přes ArduinoOTA jen v domácí LAN.

Loga budou při dokončení firmware nahrána do LittleFS v ESP32-S3. Žádné logo se nebude stahovat při běžném vykreslování. OTA port se nikde nevystavuje do internetu.

## Stav ověření

- Serverové unit testy: 3/3 úspěšné.
- Lokální `/healthz` a hokejové API: ověřené.
- Firmware sestaven pro vlastní profil ESP32-S3 DevKitC-1 N16R8 (16 MB flash / 8 MB OPI PSRAM); hardware, TFT pin map, dotyk a flash ESP32-S3: zatím neprováděny.
