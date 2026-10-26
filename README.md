# Litvínov, Sparta & Slavia TFT

Samostatný projekt pro barevný dotykový panel ILI9341 `320 × 240` na šířku a ESP32-S3 DevKitC-1 N16R8.

- `firmware/` — klient ESP32-S3, který vykresluje hokej Litvínova a fotbal Sparty nebo Slavie.
- `firmware/data/logos/` — 14 znaků ELH a 16 znaků Chance Ligy; sady jsou striktně oddělené podle sportu.
- `assets/` — zdrojové assety a náhledy.

Provozní data poskytuje veřejný server [`litvinov-server`](https://github.com/sowec1-prog/litvinov-server), nasazený na `https://litvinov-server.onrender.com`. Lokální server, bridge ani běžící PC nejsou pro provoz potřeba.

## Architektura

TFT je nezávislé na PC. Data čte pouze po HTTPS z veřejného serveru:

```text
https://litvinov-server.onrender.com/api/v1/sport/hockey
https://litvinov-server.onrender.com/api/v1/sport/football?team=SPA
https://litvinov-server.onrender.com/api/v1/sport/football?team=SKS
```

- Hokej: normalizovaná data pro Litvínov, včetně aktuálního pořadí a gólových událostí.
- Fotbal: veřejný server má dva profily: `SPA` (AC Sparta Praha) a `SKS` (SK Slavia Praha); každý vrací vlastní oficiální rozpis a pořadí z Chance Ligy.
- Veřejný server vrací cache okamžitě a data obnovuje na pozadí, aby ESP nečekalo na externí zdroje.

## Varianty firmware

Jeden společný zdroj má dva PlatformIO buildy. Hokej, Wi-Fi portál, OTA a zapojení jsou v obou shodné; mění se pouze fotbalový profil a API dotaz.

```bash
cd firmware
uvx --from platformio platformio run -e sparta
uvx --from platformio platformio run -e slavia
```

Pro nový druhý displej se nejdřív nahraje přes USB varianta `slavia`; potom lze používat OTA v domácí LAN.

## Wi-Fi a OTA

1. Zkopíruj `firmware/include/config.h.example` jako `firmware/include/config.h`.
2. Vyplň Wi-Fi, veřejnou adresu API a jedinečné `OTA_PASSWORD` pouze lokálně. `config.h` je ignorován Gitem.
3. První OTA-capable upload se provádí přes USB; další aktualizace firmware nebo LittleFS přes ArduinoOTA pouze v LAN.
4. Pokud se TFT dvě minuty nepřipojí k Wi-Fi, otevře vlastní hotspot `Litvinov-TFT-Setup`. Na displeji ukáže adresu `192.168.4.1`, kde lze uložit novou 2,4GHz Wi-Fi.

Nikdy nevystavuj OTA port do internetu a nikdy nepřipojuj 5V signál přímo na GPIO ESP32-S3.

## Ověřený hardware

- ESP32-S3 DevKitC-1 N16R8: 16 MB flash, 8 MB OPI PSRAM.
- ILI9341 + XPT2046: SPI `SCK=12`, `MOSI=11`, `MISO=13`; TFT `CS=10`, `DC=9`, `RST=14`, `BL=21`; dotyk `CS=8`, `IRQ=18`.
- TFT, dotyk, Wi-Fi konfigurační portál a OTA byly fyzicky ověřeny.

## Ověření

```bash
cd firmware
uvx --from platformio platformio run
```

Firmware používá samostatné barevné bitmapy týmů pro hokej a fotbal, loga soutěží MOL Cup / Chance Liga, český čas `Europe/Prague` a neblokující hokejovou gólovou animaci.
