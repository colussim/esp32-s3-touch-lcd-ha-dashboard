# Waveshare ESP32-S3-Touch-LCD-4 — LVGL Dashboard (ESP-IDF)

Ce dépôt documente **comment faire fonctionner réellement** le board **Waveshare ESP32-S3-Touch-LCD-4 (480×480, ST7701 + RGB)** et pourquoi l’approche Arduino/ESPHome peut échouer (ou être instable), ainsi que les correctifs nécessaires côté ESP-IDF/BSP.

> Matériel : Waveshare ESP32-S3-Touch-LCD-4  
> LCD : RGB 480×480 + contrôleur **ST7701** initialisé via SPI 3-wire  
> Touch : GT911 (I²C)  
> IO Expander : TCA9554 (I²C, 0x24)  
> Flash/PSRAM : 16MB / 8MB PSRAM (selon module)  
> Wiki + schémas : Waveshare  [oai_citation:1‡Waveshare Electronics](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4?srsltid=AfmBOope1FwUCfc4iPT0jm7qCOWfa2dng7qnEUNr_7eFYw2h0RY4MIbC&utm_source=chatgpt.com)

---

## Pourquoi ça ne fonctionnait pas (ou mal) sous Arduino IDE / ESPHome

### 1) Le pipeline LCD est **double**
Sur ce board, l’affichage passe par :
- **RGB parallèle** (PCLK/HS/VS/DE + 16 data pins) : flux vidéo
- **SPI 3-wire** (SCL/SDA/CS) : **initialisation du ST7701** au boot

Si le ST7701 n’est pas initialisé correctement, le driver RGB peut retourner `ESP_OK` sur les `draw_bitmap()`, **sans que l’écran n’affiche quoi que ce soit**.

### 2) Mémoire framebuffer (PSRAM) obligatoire
Un framebuffer 480×480 en RGB565 ≈ **460 KB**.  
Selon le nombre de buffers / alignements DMA, l’allocation en RAM interne peut échouer (`ESP_ERR_NO_MEM`).

Sous Arduino, on a dû explicitement forcer l’allocation en PSRAM (`fb_in_psram=1`).  
Côté BSP, ce flag n’était pas appliqué (voir correctif plus bas).

### 3) PSRAM mode (Quad vs Octal) côté ESP-IDF
Sous ESP-IDF, une mauvaise configuration PSRAM donne un reboot immédiat :
`PSRAM ID read error: 0x00ffffff ... wrong PSRAM line mode`
Ce symptôme est typique d’un mismatch **Quad vs Octal** sur ESP32-S3.  [oai_citation:2‡GitHub](https://github.com/espressif/esp-idf/issues/10417?utm_source=chatgpt.com)

### 4) ESP-IDF “dev” (6.x-dev) = pièges
Le BSP Waveshare cible des versions IDF stables (>= 5.3).  
Sur une branche IDF dev, on peut avoir des erreurs de build/link.

---

## Solution stable : ESP-IDF + BSP officiel Waveshare

Le plus fiable est d’utiliser le BSP officiel (ESP Component Registry).  [oai_citation:3‡components.espressif.com](https://components.espressif.com/components/waveshare/esp32_s3_touch_lcd_4/versions/2.0.0/readme?utm_source=chatgpt.com)

### Prérequis
- macOS + Xcode command line tools
- ESP-IDF **v5.4.x** recommandé (>= 5.3 requis par le BSP)

---

## Installation ESP-IDF (macOS)

```bash
xcode-select --install

mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git fetch --tags
git checkout v5.4.1
git submodule update --init --recursive
./install.sh esp32s3
. ./export.sh

idf.py --version
```

Création du projet + ajout du BSP

```bash
cd ~/esp
idf.py create-project ws_test
cd ws_test
idf.py set-target esp32s3

idf.py add-dependency "waveshare/esp32_s3_touch_lcd_4^2.0.0"

```


Correctifs BSP nécessaires (observés en pratique)

A) Ajout de <string.h> (bug de compilation)

Erreur rencontrée :
implicit declaration of function 'memcpy'

Fix (dans le composant géré) :

Fichier :
managed_components/waveshare__esp32_s3_touch_lcd_4/esp32_s3_touch_lcd_4.c

Ajouter :

```c
#include <string.h>

```
> Note : managed_components/ peut être régénéré lors d’une mise à jour des dépendances.

B) Forcer les framebuffers en PSRAM (sinon ESP_ERR_NO_MEM)

Erreur rencontrée :
lcd_rgb_panel_alloc_frame_buffers: no mem for frame buffer

Dans le même fichier BSP, repérer la struct :
esp_lcd_rgb_panel_config_t rgb_config = { ... };

Juste après la struct, forcer :

```c
rgb_config.flags.fb_in_psram = 1;
rgb_config.num_fbs = 1;
```
---

Configuration PSRAM (CRITIQUE)

Dans idf.py menuconfig :

Component config -> ESP PSRAM
	•	Activer PSRAM
	•	Choisir le bon mode :
	•	OCTAL Mode PSRAM (souvent requis sur ce board / module)
	•	Si erreur wrong PSRAM line mode, tester l’autre : QUAD Mode PSRAM

Référence symptôme : PSRAM ID read error: 0x00ffffff

Build / Flash / Monitor

```bash
idf.py fullclean
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

> Quitter monitor : Ctrl + ]

---

Exemple LVGL minimal (2 boutons)

Une fois bsp_display_start() OK, un écran LVGL minimal affiche 2 boutons + labels.

Le BSP initialise :

	•	IO 3-wire SPI pour ST7701
	•	panel ST7701
	•	panel RGB + timings
	•	touch GT911

---

## Mapping GPIO — Waveshare ESP32-S3-Touch-LCD-4 (480×480)

Ce tableau résume **le mapping réel et fonctionnel** du board Waveshare ESP32-S3-Touch-LCD-4, tel qu’utilisé par le BSP officiel et validé en pratique.

> ⚠️ Important  
> - Le LCD est **RGB parallèle** (flux vidéo)  
> - Le contrôleur **ST7701** est initialisé via **SPI 3-wire**  
> - Plusieurs signaux critiques passent par un **IO expander I²C (TCA9554)**

---

### 1️⃣ Alimentation & contrôle LCD (via IO Expander)

| Fonction        | Composant | GPIO ESP32-S3 | Détail |
|----------------|----------|---------------|--------|
| Backlight EN   | TCA9554  | EXIO2 (P1)    | **BL_EN** – doit être HIGH |
| LCD Reset      | TCA9554  | EXIO3 (P2)    | **LCD_RST** (pulse) |
| Touch Reset    | TCA9554  | EXIO1 (P0)    | **TP_RST** |

IO Expander :
- **Type** : TCA9554  
- **Adresse I²C** : `0x24`

---

### 2️⃣ Bus I²C (Touch, IO Expander, RTC)

| Fonction | GPIO ESP32-S3 |
|--------|---------------|
| SDA    | GPIO15 |
| SCL    | GPIO7  |

Adresses I²C observées sur le bus :
- `0x24` → **TCA9554** (IO Expander)
- `0x5D` → **GT911** (Touch)
- `0x51` → RTC (probable)

---

### 3️⃣ Touch Panel (GT911)

| Fonction | GPIO ESP32-S3 | Détail |
|--------|---------------|--------|
| I²C SDA | GPIO15 | Partagé |
| I²C SCL | GPIO7  | Partagé |
| INT     | GPIO16 | Touch interrupt |
| RESET   | EXIO1  | Via TCA9554 |

---

### 4️⃣ LCD — Signaux de synchronisation RGB

| Signal | GPIO ESP32-S3 |
|------|---------------|
| HSYNC | GPIO38 |
| VSYNC | GPIO39 |
| DE    | GPIO40 |
| PCLK  | GPIO41 |
| DISP  | (géré par BSP) |

---

### 5️⃣ LCD — Bus RGB Data (16 bits)

Le LCD utilise un bus **RGB 16-bit parallèle**.  
L’ordre exact est **critique** et dépend du PCB.

Mapping validé (D0 → D15) :

| Data bit | GPIO |
|---------|------|
| D0 | GPIO14 |
| D1 | GPIO13 |
| D2 | GPIO12 |
| D3 | GPIO11 |
| D4 | GPIO10 |
| D5 | GPIO9  |
| D6 | GPIO46 |
| D7 | GPIO3  |
| D8 | GPIO8  |
| D9 | GPIO18 |
| D10 | GPIO17 |
| D11 | GPIO5  |
| D12 | GPIO45 |
| D13 | GPIO48 |
| D14 | GPIO47 |
| D15 | GPIO21 |

> ℹ️  
> Ce mapping correspond à la configuration utilisée par le BSP Waveshare et validée avec un framebuffer RGB565 en PSRAM.

---

### 6️⃣ SPI — Initialisation ST7701 (3-wire)

Le ST7701 **n’est pas un écran SPI**, mais **il doit être initialisé via SPI** avant que le RGB fonctionne.

| Signal | GPIO ESP32-S3 |
|------|---------------|
| CS   | GPIO42 |
| SCL  | (interne BSP) |
| SDA  | (interne BSP) |

> Le bus SPI est **uniquement utilisé au boot** pour envoyer la séquence d’initialisation ST7701.  
> Ensuite, seul le RGB parallèle est actif.

---

Mapping GPIO (Waveshare ESP32-S3-Touch-LCD-4)

Source : schéma/table Waveshare (Wiki + SCH/PDF).  ￼

Signaux LCD (contrôle / synchro)
	•	GPIO38 : LCD_HSYNC
	•	GPIO39 : LCD_VSYNC
	•	GPIO40 : LCD_DE (DEN)
	•	GPIO41 : LCD_PCLK
	•	GPIO42 : LCD_CS (SPI init ST7701)

Bus I²C Touch / IO expander / RTC
	•	GPIO15 : I²C SDA (TP_SDA)
	•	GPIO7  : I²C SCL (TP_SCL)
	•	GPIO16 : Touch INT (TP_INT)

Adresses I²C observées :
	•	0x24 : TCA9554 (IO expander)
	•	0x5D : GT911 (touch)
	•	0x51 : RTC probable

IO Expander (TCA9554 @ 0x24)
	•	EXIO1 : TP_RST
	•	EXIO2 : BL_EN (Backlight enable)
	•	EXIO3 : LCD_RST

LCD RGB Data (16-bit)

Les data pins sont réparties en groupes R/G/B (voir tableau du schéma).
Exemple de set utilisé avec succès (ordre D0..D15 selon driver RGB) :

```c
[14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17, 5, 45, 48, 47, 21]

```

> Remarque : sur ce type de panel RGB, l’ordre exact D0..D15 (et la correspondance R/G/B) dépend du mapping du PCB. Se fier au schéma et/ou au BSP qui encapsule la config.


---

Checklist de dépannage

Écran noir mais pas de crash
	•	Backlight ON ? (BL_EN via TCA9554)
	•	ST7701 initialisé ? (logs st7701: version ...)
	•	draw_bitmap ESP_OK mais rien ne change => ST7701 pas configuré ou RGB timings/polarité incorrects

Crash ESP_ERR_NO_MEM lors de esp_lcd_new_rgb_panel
	•	Activer PSRAM dans menuconfig
	•	Forcer rgb_config.flags.fb_in_psram = 1
	•	Réduire à num_fbs = 1

Reboot avec PSRAM ID read error ... wrong PSRAM line mode
	•	Changer Quad Mode PSRAM ↔ Octal Mode PSRAM 

---

	•	Le BSP exige ESP-IDF >= 5.3.  ￼
	•	Éviter les versions IDF dev (6.x-dev) pour ce board tant que le BSP n’annonce pas explicitement le support.


---

🔚 Conclusion

Ce board fonctionne parfaitement, mais :
	•	la doc Arduino est insuffisante
	•	la PSRAM doit être correctement configurée
	•	le ST7701 doit être initialisé correctement
	•	le BSP ESP-IDF est aujourd’hui la solution la plus fiable

---

Ressources

waveshare/esp32_s3_touch_lcd_4
https://components.espressif.com/components/waveshare/esp32_s3_touch_lcd_4/versions/1.0.3/dependencies?language=en&utm_source=chatgpt.com

