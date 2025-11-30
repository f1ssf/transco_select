//------------------------------------------------------
// Sélecteur d'antennes F1SSF
// Arduino MEGA 2560 + Nextion NX4832K035-011
// - Nextion sur Serial3 : TX3 = D14, RX3 = D15
// - 8 boutons Dual State : bt0..bt7 (ANT1..ANT8)
// - Carte relais ACTIVE HIGH : LOW = OFF, HIGH = ON
// - Une seule antenne rouge à la fois
// - Relais collé 3 s à chaque appui, puis OFF
//   (le bouton reste ROUGE = antenne sélectionnée)
// - Rappel automatique de l’antenne sélectionnée au démarrage
//------------------------------------------------------

#include <Arduino.h>
#include <EEPROM.h>

const uint8_t NB_ANTENNES = 8;

// IMPORTANT : mettre ici l'ID de bt0 (ANT1) tel qu'affiché dans Nextion Editor
// Exemple typique : page0.id = 0, titre t0.id = 1, bt0.id = 2 => FIRST_BTN_ID = 2
const uint8_t FIRST_BTN_ID = 2;   // ADAPTE cette valeur si besoin

// EEPROM : on stocke un "magic" + l'index d'antenne
const uint8_t EEPROM_MAGIC_ADDR    = 0;
const uint8_t EEPROM_SELECTED_ADDR = 1;
const uint8_t EEPROM_MAGIC_VALUE   = 0xA5;

// Relais associés aux 8 antennes
const uint8_t relayPins[NB_ANTENNES] = {
  22, 23, 24, 25, 26, 27, 28, 29
};

// Timer OFF relais (1 par antenne)
unsigned long relayOffTime[NB_ANTENNES] = {0};

// Aucune antenne sélectionnée par défaut
int8_t antenneSelectionnee = -1;

//------------------------------------------------------
// Force un bouton à VERT (OFF) → .val = 0
//------------------------------------------------------
void setBoutonOff(uint8_t index) {
  Serial3.print("bt");
  Serial3.print(index);
  Serial3.print(".val=0");
  Serial3.write(0xFF); Serial3.write(0xFF); Serial3.write(0xFF);
}

//------------------------------------------------------
// Force un bouton à ROUGE (ON) → .val = 1
//------------------------------------------------------
void setBoutonOn(uint8_t index) {
  Serial3.print("bt");
  Serial3.print(index);
  Serial3.print(".val=1");
  Serial3.write(0xFF); Serial3.write(0xFF); Serial3.write(0xFF);
}

//------------------------------------------------------
// Exclusivité : coupe tous les autres relais et
// met leurs boutons en vert
//------------------------------------------------------
void exclusivite(uint8_t actif) {
  for (uint8_t i = 0; i < NB_ANTENNES; i++) {
    if (i != actif) {
      digitalWrite(relayPins[i], LOW);   // relais OFF (ACTIVE HIGH)
      relayOffTime[i] = 0;              // annule temporisation
      setBoutonOff(i);                  // bouton vert
    }
  }
}

//------------------------------------------------------
// Temporisation OFF automatique (3 secondes)
// Le bouton RESTE ROUGE : pas de setBoutonOff()
//------------------------------------------------------
void gererTemporisations() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < NB_ANTENNES; i++) {
    if (relayOffTime[i] != 0 && now >= relayOffTime[i]) {
      digitalWrite(relayPins[i], LOW);   // relais OFF
      relayOffTime[i] = 0;
      // On laisse le bouton rouge (sélection persistante)
    }
  }
}

//------------------------------------------------------
// Sauvegarde de l’antenne sélectionnée en EEPROM
//------------------------------------------------------
void sauvegarderSelection(uint8_t idx) {
  EEPROM.update(EEPROM_MAGIC_ADDR,    EEPROM_MAGIC_VALUE);
  EEPROM.update(EEPROM_SELECTED_ADDR, idx);
}

//------------------------------------------------------
// Lecture de la dernière antenne sélectionnée depuis EEPROM
// Retourne -1 si aucune sélection valide
//------------------------------------------------------
int8_t lireSelectionEEPROM() {
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  if (magic != EEPROM_MAGIC_VALUE) {
    return -1;
  }

  uint8_t val = EEPROM.read(EEPROM_SELECTED_ADDR);
  if (val >= NB_ANTENNES) {
    return -1;
  }

  return (int8_t)val;
}

//------------------------------------------------------
// Réception des commandes Touch Event du Nextion
// Trame : 0x65, page, id, state, 0xFF, 0xFF, 0xFF
// - page = 0 (page0)
// - id   = ID Nextion brut (converti avec FIRST_BTN_ID)
//------------------------------------------------------
void recevoirEvenementsNextion() {
  static uint8_t pos = 0;
  static uint8_t buffer[7];

  while (Serial3.available()) {
    uint8_t c = (uint8_t) Serial3.read();

    if (pos == 0 && c != 0x65) {
      continue;
    }

    buffer[pos++] = c;

    if (pos >= 7) {
      if (buffer[0] == 0x65 &&
          buffer[4] == 0xFF &&
          buffer[5] == 0xFF &&
          buffer[6] == 0xFF) {

        uint8_t page   = buffer[1];
        uint8_t compId = buffer[2];  // ID Nextion brut

        if (page == 0) {
          // On ne garde que les IDs qui correspondent aux boutons bt0..bt7
          if (compId >= FIRST_BTN_ID &&
              compId < FIRST_BTN_ID + NB_ANTENNES) {

            // Conversion ID Nextion -> index 0..7
            uint8_t idx = compId - FIRST_BTN_ID;

            // 1) Exclusivité (autres verts, relais coupés)
            exclusivite(idx);

            // 2) Bouton appuyé en ROUGE
            setBoutonOn(idx);

            // 3) On mémorise cette antenne comme sélectionnée
            antenneSelectionnee = (int8_t)idx;
            sauvegarderSelection(idx);

            // 4) Relais ON pour 3 s
            digitalWrite(relayPins[idx], HIGH);         // ON (ACTIVE HIGH)
            relayOffTime[idx] = millis() + 2000UL;      // tempo 3 s
          }
        }
      }

      pos = 0; // reset réception
    }
  }
}

//------------------------------------------------------
// Initialisation
//------------------------------------------------------
void setup() {
  // Relais : tous OFF au démarrage
  for (uint8_t i = 0; i < NB_ANTENNES; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);   // OFF
  }

  Serial3.begin(9600);  // Nextion sur D14/D15
  delay(500);           // laisser le temps au Nextion de démarrer

  // Lecture de la dernière antenne sélectionnée
  antenneSelectionnee = lireSelectionEEPROM();

  if (antenneSelectionnee >= 0 && antenneSelectionnee < (int8_t)NB_ANTENNES) {
    // Exclusivité (toutes les autres en vert)
    exclusivite((uint8_t)antenneSelectionnee);
    // Bouton sélectionné en rouge
    setBoutonOn((uint8_t)antenneSelectionnee);
    // On NE colle PAS le relais ici : il restera OFF tant qu'on n'appuie pas
  }
}

//------------------------------------------------------
// Boucle principale
//------------------------------------------------------
void loop() {
  recevoirEvenementsNextion();
  gererTemporisations();
}
