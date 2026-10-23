# Worldwide Weather Watcher (WWW)

Station météo pour bateaux, sur une carte STM32 NUCLEO-F401RE. Le sujet et le dépôt de départ parlent de la L476RG, mais notre groupe n'a que des F401RE. On a donc porté le code.

## Matériel et câblage

| Élément | Branchement | Broches STM32 |
|---|---|---|
| Grove Base Shield V2 | sur le shield SD, lui-même sur la Nucleo | |
| LED RGB chaînable (P9813) | port Grove D6 | PB10 (CLK), PA8 (DATA) |
| Grove Dual Button | port Grove D8 | PA9 = bouton rouge, PC7 = bouton vert |
| Capteur de lumière | port Grove A2 | PA4 (ADC1 canal 4) |
| GPS Air530Z | port Grove D2 | PA10 = USART1_RX (TX du GPS, 9600 bauds) |
| Horloge RTC DS1307 | port Grove I2C | PB8 (SCL), PB9 (SDA), adresse 0x68 |
| BME280 (WPSE335) | fils Dupont, 3,3 V uniquement | PB9 (SDA), PB8 (SCL), adresse 0x76 |
| Shield SD (SPI) | empilé + 3 fils Dupont depuis l'ICSP | ICSP 1 vers D12, ICSP 3 vers D13, ICSP 4 vers D11, CS = D4 (PB5) |
| Moniteur série | USB du ST-Link | USART2, 115200 bauds |

Le shield SD passe par le connecteur ICSP, que la Nucleo n'a pas. Sans les 3 fils, la carte SD n'est pas détectée. Il ne faut rien brancher sur les broches ICSP 2 (5 V), 5 et 6.

## Compiler et téléverser

```bash
bash build.sh upload          # compile, puis copie le firmware sur la carte
bash build.sh upload monitor  # pareil, puis ouvre le moniteur série
```

La méthode dépend de la variable `TOOL`, en haut du script :

- `copy` (par défaut) : copie `firmware.bin` sur le lecteur USB NODE_F401RE de la carte. Pas besoin de pilote.
- `stlink` : téléversement avec OpenOCD. Sous Windows, il faut installer le pilote ST-Link.

Si `src/main.c` contient `#define DEBUG 1`, les fichiers intermédiaires (`.i`, `.s`) sont copiés dans `.debug/`.

## Guide utilisateur

### Les quatre modes

| Mode | LED | Pour y entrer | Comportement |
|---|---|---|---|
| Standard | verte | démarrage normal | une mesure toutes les `LOG_INTERVAL` minutes, écrite sur la carte SD |
| Configuration | jaune | démarrer en tenant le bouton rouge | réglages au clavier, pas d'acquisition. Retour au standard après 30 min sans rien taper |
| Maintenance | orange | 5 s sur le rouge depuis le standard | mesure toutes les 2 s affichée sur le moniteur, carte SD libérée (on peut la retirer). 5 s sur le rouge pour revenir |
| Économique | bleue | 5 s sur le vert depuis le standard | intervalle doublé, GPS lu une fois sur deux. 5 s sur le rouge pour revenir au standard |

Un appui court sur le vert lance une mesure tout de suite. C'est pratique pour la démo.

### Codes d'erreur de la LED

En cas d'erreur, la LED clignote à 1 Hz.

| Couleurs | Signification |
|---|---|
| rouge / bleu | horloge RTC inaccessible |
| rouge / jaune | GPS sans réponse |
| rouge / vert (durées égales) | un capteur ne répond plus |
| rouge / vert (vert 2 fois plus long) | valeur hors des seuils, vérifier le matériel |
| rouge / blanc (durées égales) | carte SD pleine |
| rouge / blanc (blanc 2 fois plus long) | carte SD absente ou erreur d'écriture |

### Commandes du mode configuration

Ouvrir un terminal série (PuTTY, 115200 bauds), taper la commande, puis Entrée.

| Commande | Effet | Défaut |
|---|---|---|
| `SHOW` | affiche tous les paramètres | |
| `LOG_INTERVAL=10` | minutes entre deux mesures | 10 |
| `FILE_MAX_SIZE=2048` | taille en octets à partir de laquelle le fichier est archivé | 2048 |
| `TIMEOUT=30` | secondes avant de déclarer un capteur sans réponse | 30 |
| `LUMIN=1`, `LUMIN_LOW=255`, `LUMIN_HIGH=768` | luminosité : activation, seuil faible, seuil fort | 1, 255, 768 |
| `TEMP_AIR=1`, `MIN_TEMP_AIR=-10`, `MAX_TEMP_AIR=60` | température : activation, seuils d'erreur | 1, -10, 60 |
| `HYGR=1`, `HYGR_MINT=0`, `HYGR_MAXT=50` | humidité : activation, plage de température où on la garde | 1, 0, 50 |
| `PRESSURE=1`, `PRESSURE_MIN=850`, `PRESSURE_MAX=1080` | pression : activation, seuils d'erreur | 1, 850, 1080 |
| `CLOCK=14:30:00` | règle l'heure | |
| `DATE=10,18,2026` | règle la date (mois, jour, année) | |
| `DAY=SAT` | règle le jour de la semaine (MON à SUN) | |
| `RESET` | remet tous les paramètres par défaut | |
| `VERSION` | affiche la version du programme et le numéro de lot | |

Les paramètres sont sauvegardés en flash, donc ils restent après une coupure de courant.

### Données enregistrées

Une ligne par mesure, avec `;` entre les champs :

```
2026-10-18 14:30:00;21.38;46.8;1000.4;124;faible;48.85661;2.35222
date heure;température °C;humidité %;pression hPa;lumière 0-1023;classe;latitude;longitude
```

`NA` veut dire que le capteur n'a pas répondu, ou que la mesure n'est pas retenue (humidité hors plage, GPS sans position). `OFF` veut dire que le capteur est désactivé dans la configuration.

Sur la carte SD (FAT32), il y a un fichier par jour : `AAMMJJ_0.LOG`, par exemple `261018_0.LOG`. Quand il dépasse `FILE_MAX_SIZE`, il est renommé `AAMMJJ_1.LOG` (puis `_2`, jusqu'à `_9`) et on repart sur un nouveau `_0` vide. Avant de retirer la carte, passer d'abord en mode maintenance (LED orange).

## Documentation technique

### Flux d'information

```
 Boutons (EXTI) ──► drapeaux + horodatage ──► boucle principale ──► machine à états (4 modes)
                                                     │
 RTC, BME280 (I2C) ─┐                                ▼
 Lumière (ADC)  ────┼──► Acquisition_Run ──► ligne horodatée ──┬──► moniteur série (USART2)
 GPS (USART1)  ─────┘   (timeouts, seuils,                     └──► carte SD (SPI + FatFs)
                         compteurs d'erreurs)                        sauf en maintenance
                                  │
                                  └──► code d'erreur ──► LED RGB (couleur du mode ou clignotement)
 Port série ──► Config_HandleCommand ──► paramètres en flash (mode configuration)
```

### Organisation du code

| Fichier | Rôle |
|---|---|
| `src/main.c` | machine à états des modes, cadence des mesures, choix du code LED |
| `src/acquisition.c` | lecture des capteurs, `NA` / `OFF`, TIMEOUT, seuils, compteurs d'erreurs |
| `src/buttons.c` | interruptions EXTI des boutons, anti-rebond, appui court et long (5 s) |
| `src/status_led.c` | couleur du mode et clignotements d'erreur, sans `HAL_Delay` |
| `src/config.c` | paramètres, sauvegarde en flash (secteur 7), commandes série |
| `src/port_f401re.c` | initialisation de la F401RE (horloge, I2C, SPI, UART, ADC), console |
| `lib/cesi_grove/` | pilotes des modules (BME280, RTC, LED, lumière, SD, GPS fourni par le dépôt) |
| `lib/fatfs/` | système de fichiers FatFs R0.12c (ST / ChaN) |

### Choix techniques

Pas d'EEPROM sur la F401RE. Les paramètres sont donc écrits dans le dernier secteur de la flash (0x08060000), avec un nombre magique devant pour savoir s'ils sont valides. S'il n'est pas là, on prend les valeurs par défaut.

Le GPS est sur l'USART1, avec la bibliothèque fournie. Sur la L476RG il utilisait l'UART4 sur PA0 (port A0), qui n'existe pas sur la F401RE. On l'a mis sur le port D2 (PA10 = USART1_RX), comme le propose le prosit « Interruptions ». Le Dual Button est sur le port D8. On a modifié la bibliothèque le moins possible : l'UART se choisit dans `board_pins.h`, et le découpage des trames garde maintenant les champs vides.

Les boutons fonctionnent par interruptions, pour ne jamais rater un appui pendant une acquisition longue. L'interruption note seulement l'instant du front. Le reste est traité dans la boucle principale.

Au démarrage, on envoie 9 impulsions sur SCL. Si la carte a redémarré en pleine communication I2C, un module peut garder SDA bloqué, et ça le libère.

Un capteur qui ne répond pas a TIMEOUT secondes pour le faire. Après 2 mesures sans réponse, il passe en erreur et n'a plus qu'un seul essai par mesure, pour ne pas bloquer la station.

## Scénario de démonstration

1. On branche la carte. La LED est verte et une première mesure s'affiche sur le moniteur série.
2. Appui court sur le vert : nouvelle mesure. On cache le capteur de lumière : la valeur et la classe baissent.
3. On débranche le BME280 : on voit `NA` sur 3 champs, puis la LED passe rouge / vert après 2 mesures. On le rebranche et tout revient à la normale.
4. 5 s sur le vert : LED bleue (économique). 5 s sur le rouge : retour au vert.
5. 5 s sur le rouge : LED orange (maintenance), mesures toutes les 2 s. On retire la carte SD, on montre le fichier `.LOG` sur un PC, on la remet. 5 s sur le rouge : retour au vert.
6. On redémarre en tenant le rouge : LED jaune (configuration). On tape `SHOW`, `LOG_INTERVAL=1`, `CLOCK=...`, `VERSION`.

## Limites connues

- Depuis le mode économique, 5 s sur le rouge ramène au mode standard. Le sujet dit aussi que la maintenance est accessible depuis l'économique avec le même geste, ce qui est contradictoire. On a gardé le retour au standard.
- `FILE_MAX_SIZE` vaut 2048 par défaut (c'est la valeur du descriptif du mode standard, alors que l'exemple de commande du sujet montre 4096).
- 9 archives par jour au maximum (noms courts 8.3, pas de noms longs).
- La position GPS n'a pas été testée en extérieur au moment où on a écrit ce README.
