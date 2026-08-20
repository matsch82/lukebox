# include "vs1053_SdFat.h"
#include "PCF8574.h"

#define NO_TRACK ((uint8_t)0xff)

#define VOLUME_MAX (2) 
PCF8574 pcf8574(0x20);
// Bezeichnung der SD-Karte
SdFat sd;
// Bezeichnung des mp3-Shields
vs1053 MP3player;
// Variable für das Lesen des Verzeichnisses
File Verzeichnis;
File Datei;

// Tracknummer/Anzahl der Tracks
uint8_t Track = 1;
uint8_t TrackMax = 0;
PCF8574::DigitalInput dio;

// Hardware-Status: wird in setup() ermittelt und in loop() geprüft,
// damit defekte/fehlende Hardware nicht zu unbemerktem Fehlverhalten führt.
bool sdReady = false;
bool playerReady = false;
bool gpioReady = false;

// --- Einfache Logging-Helfer, damit Fehler/Warnungen einheitlich ausgegeben werden ---
// Wichtig: Nachrichten werden entweder als Flash-String (F("...")) oder als
// zusammengesetzter String uebergeben. Reine Text-Literale MUESSEN mit F()
// umschlossen werden, sonst landen sie im knappen RAM statt im Flash-Speicher.
void logError(const String &msg) {
  Serial.print(F("[FEHLER] "));
  Serial.println(msg);
}
void logError(const __FlashStringHelper *msg) {
  Serial.print(F("[FEHLER] "));
  Serial.println(msg);
}

void logWarning(const String &msg) {
  Serial.print(F("[WARNUNG] "));
  Serial.println(msg);
}
void logWarning(const __FlashStringHelper *msg) {
  Serial.print(F("[WARNUNG] "));
  Serial.println(msg);
}

void logInfo(const String &msg) {
  Serial.print(F("[INFO] "));
  Serial.println(msg);
}
void logInfo(const __FlashStringHelper *msg) {
  Serial.print(F("[INFO] "));
  Serial.println(msg);
}

// Fehlercodes von vs1053::begin(), siehe Bibliotheksdokumentation (Error_Codes).
enum Mp3BeginError : uint8_t {
  MP3_BEGIN_OK = 0,
  MP3_BEGIN_SD_CONTACT_FAILED = 1,
  MP3_BEGIN_SD_VOLUME_FAILED = 2,
  MP3_BEGIN_SD_ROOT_FAILED = 3,
  MP3_BEGIN_SCI_MODE_MISMATCH = 4,
  MP3_BEGIN_SCI_CLOCKF_MISMATCH = 5,
  MP3_BEGIN_PATCH_LOAD_FAILED = 6,
};

// Fehlercodes von vs1053::playTrack()/playMP3().
enum Mp3PlayError : uint8_t {
  MP3_PLAY_OK = 0,
  MP3_PLAY_ALREADY_PLAYING = 1,
  MP3_PLAY_FILE_NOT_FOUND = 2,
  MP3_PLAY_CHIP_IN_RESET = 3,
};

void printMp3BeginError(uint8_t code) {
  switch (code) {
    case MP3_BEGIN_SD_CONTACT_FAILED:
      logError(F("MP3-Player: Kein Kontakt zur SD-Karte (Fehlercode 1)."));
      break;
    case MP3_BEGIN_SD_VOLUME_FAILED:
      logError(F("MP3-Player: SD-Karten-Volume konnte nicht gestartet werden (Fehlercode 2)."));
      break;
    case MP3_BEGIN_SD_ROOT_FAILED:
      logError(F("MP3-Player: Wurzelverzeichnis der SD-Karte konnte nicht gemountet werden (Fehlercode 3)."));
      break;
    case MP3_BEGIN_SCI_MODE_MISMATCH:
      logError(F("MP3-Player: Unerwarteter Wert im SCI_MODE-Register (Fehlercode 4). VS1053 evtl. defekt oder falsch verkabelt."));
      break;
    case MP3_BEGIN_SCI_CLOCKF_MISMATCH:
      logError(F("MP3-Player: SCI_CLOCKF-Register konnte nicht verifiziert werden (Fehlercode 5). VS1053 evtl. defekt oder falsch verkabelt."));
      break;
    case MP3_BEGIN_PATCH_LOAD_FAILED:
      logWarning(F("MP3-Player: Patch-Datei konnte nicht geladen werden (Fehlercode 6). Wiedergabe kann trotzdem funktionieren."));
      break;
    default:
      logError(String(F("MP3-Player: Start fehlgeschlagen, unbekannter Fehlercode ")) + code + F("."));
      break;
  }
}

void printMp3PlayError(uint8_t code, uint8_t track) {
  switch (code) {
    case MP3_PLAY_ALREADY_PLAYING:
      logWarning(String(F("Track ")) + track + F(" konnte nicht gestartet werden: Es wird bereits ein Track abgespielt."));
      break;
    case MP3_PLAY_FILE_NOT_FOUND:
      logError(String(F("Track ")) + track + F(" konnte nicht gefunden werden (Datei fehlt auf der SD-Karte)."));
      break;
    case MP3_PLAY_CHIP_IN_RESET:
      logError(String(F("Track ")) + track + F(" konnte nicht gestartet werden: VS1053-Chip ist im Reset-Zustand."));
      break;
    default:
      logError(String(F("Track ")) + track + F(" konnte nicht gestartet werden, unbekannter Fehlercode ") + code + F("."));
      break;
  }
}

bool setupGpio(){
  pcf8574.pinMode(P0, INPUT);
  pcf8574.pinMode(P1, INPUT);
  pcf8574.pinMode(P2, INPUT);
  pcf8574.pinMode(P3, INPUT);
  pcf8574.pinMode(P4, INPUT);
  pcf8574.pinMode(P5, INPUT);
  pcf8574.pinMode(P6, INPUT);
  if (!pcf8574.begin()) {
    logError(F("PCF8574 I/O-Erweiterung nicht am I2C-Bus gefunden (Adresse 0x20). Tasten funktionieren nicht."));
    return false;
  }
  logInfo(F("GPIO-Erweiterung (PCF8574) erfolgreich initialisiert."));
  return true;
}

bool setupPlayer() {
  // Player starten
  uint8_t result = MP3player.begin();
  if (result != MP3_BEGIN_OK) {
    printMp3BeginError(result);
    // Fehlercode 6 (Patch-Datei fehlt) ist laut Bibliotheksdokumentation nicht
    // fatal - die Wiedergabe funktioniert weiterhin, nur ohne den Patch.
    // Alle anderen Fehlercodes (1-5, unbekannt) deuten auf defekte/fehlende
    // Hardware hin, dann kann der Player nicht sinnvoll genutzt werden.
    if (result != MP3_BEGIN_PATCH_LOAD_FAILED) {
      return false;
    }
  } else {
    logInfo(F("MP3-Player erfolgreich gestartet."));
  }
  // Höhen: erlaubte Werte: -8 bis 7
  MP3player.setTrebleAmplitude(0);
  // Bässe: erlaubte Werte 0 bis 15
  MP3player.setBassAmplitude(14);
  // Lautstärke setzen -> links, rechts -> 1, 1 sehr laut
  // je größer die Werte desto leiser
  MP3player.setVolume(10, 10);
  MP3player.setMonoMode(3);
  return true;
}

void beep() {
  MP3player.SendSingleMIDInote();
}

bool printSdFiles() {
  Serial.println(F("------------------------------"));
  char Dateiname[13];
  if (!sd.chdir("/")) {
    logError(F("Konnte nicht in das Wurzelverzeichnis der SD-Karte wechseln."));
    return false;
  }
  if (!Verzeichnis.open("/")) {
    logError(F("Konnte das Wurzelverzeichnis der SD-Karte nicht öffnen."));
    return false;
  }
  Serial.println(F("File Size"));
  Serial.println(F("------------------------------"));
  TrackMax = 0;
  while (Datei.openNext(&Verzeichnis, O_READ))
  {
    if (!Datei.getName(Dateiname, sizeof(Dateiname))) {
      logWarning(F("Dateiname konnte nicht gelesen werden, Datei wird übersprungen."));
      Datei.close();
      continue;
    }
    // handelt es sich um eine Musikdatei (isFnMusic)
    if (isFnMusic(Dateiname) )
    {
      Serial.print(Dateiname);
      // Dateigröße ermitteln, in MB umwandeln, Punkt durch Komma ersetzen
      float DateiGroesse = Datei.fileSize();
      String Groesse = String(DateiGroesse / 1000000);
      Groesse.replace(".", ",");
      Serial.println(String(F("\t")) + Groesse + F(" MB"));
      TrackMax ++;
    }
    Datei.close();
  }
  Verzeichnis.close();
  Serial.println(String(F("Anzahl der Tracks: ")) + TrackMax);
  Serial.println();
  if (TrackMax == 0) {
    logWarning(F("Keine abspielbaren Musikdateien auf der SD-Karte gefunden."));
  }
  return true;
}

const uint8_t sensorPin = A0; 
const uint8_t sensorValue = 0;

void setup()
{
  Serial.begin(57600);

  delay(100);
  logInfo(F("Lukebox startet..."));

  sdReady = sd.begin(SD_SEL, SPI_FULL_SPEED);
  if (!sdReady) {
    logError(F("SD-Karte konnte nicht initialisiert werden. Bitte eine FAT16/FAT32 formatierte SD-Karte einlegen."));
  } else if (!printSdFiles()) {
    sdReady = false;
  }

  playerReady = setupPlayer();
  gpioReady = setupGpio();

  if (!sdReady || !playerReady || !gpioReady) {
    logWarning(F("Lukebox gestartet, aber mit eingeschränkter Funktionalität (siehe Fehler oben)."));
  } else {
    logInfo(F("Lukebox bereit."));
  }
}


static uint8_t track_current;
static uint32_t tick;
static bool initDone;
void playTrack(int i){
  if(!initDone){
    return;
  }
  if(!playerReady){
    logWarning(F("Tastendruck ignoriert: MP3-Player ist nicht bereit."));
    return;
  }
  if(!sdReady || TrackMax == 0){
    logWarning(F("Tastendruck ignoriert: Keine Tracks auf der SD-Karte verfügbar."));
    return;
  }
  if(i > TrackMax) {
    logWarning(String(F("Track ")) + i + F(" angefordert, aber nur ") + TrackMax + F(" Track(s) verfügbar."));
    delay(100);
    beep();
    delay(100);
    beep();
    return;
  }
  Serial.print(F("stop track"));
  Serial.println(track_current);
  MP3player.stopTrack();
  delay(100);
  beep();
  if(MP3player.isPlaying() == false){
    track_current=NO_TRACK;
  }
  if(track_current == i){
    track_current=NO_TRACK;
    return;
  }
  uint8_t result = MP3player.playTrack(i);
  if(result != MP3_PLAY_OK){
    printMp3PlayError(result, i);
    track_current=NO_TRACK;
  } else {
    track_current=i;
  }
}

uint8_t getVolumeFromSensorValue(int sensor){
  // sensor max val 665  -> max volume 0
  // sensor min val 0 --> min volume 100
  int tmp = sensor*100;
  tmp = tmp/130;
  tmp = 100 - tmp;
  if (tmp < VOLUME_MAX) tmp = VOLUME_MAX;
  if (tmp > 100) tmp = 100;
  return tmp;
}

void loop()
{
    tick++;

    if(!initDone && tick==300){
      initDone = true;
    }

    if(gpioReady){
      PCF8574::DigitalInput di = pcf8574.digitalReadAll();

      // I2C-Bus kann sich lösen/wieder verbinden (z.B. loses Kabel);
      // fehlerhafte Lesungen sollen nicht als echte Tastendrücke interpretiert werden.
      static bool i2cWasOk = true;
      bool i2cOk = pcf8574.isLastTransmissionSuccess();
      if(!i2cOk && i2cWasOk){
        logWarning(F("I2C-Verbindung zum PCF8574 (Tasten) verloren."));
      } else if(i2cOk && !i2cWasOk){
        logInfo(F("I2C-Verbindung zum PCF8574 (Tasten) wiederhergestellt."));
      }
      i2cWasOk = i2cOk;

      if(i2cOk){
        if(di.p0 && di.p0 != dio.p0){ Serial.println(F("schwarz"));playTrack(7);}
        if(di.p1 && di.p1 != dio.p1){ Serial.println(F("orange"));playTrack(6);}
        if(di.p2 && di.p2 != dio.p2){ Serial.println(F("weiß"));playTrack(5); }
        if(di.p3 && di.p3 != dio.p3){ Serial.println(F("gruen")); playTrack(4);}
        if(di.p4 && di.p4 != dio.p4){ Serial.println(F("rot")); playTrack(3);}
        if(di.p5 && di.p5 != dio.p5){ Serial.println(F("blau")); playTrack(2);}
        if(di.p6 && di.p6 != dio.p6){ Serial.println(F("gelb")); playTrack(1);}

        dio = di;
      }
    }

    if(playerReady && tick % 10 == 0) {
     int sensorValue = analogRead(sensorPin);
     int volume = getVolumeFromSensorValue(sensorValue);
     
     if(tick % 200 == 0) {
      Serial.print(F("Tick: "));
      Serial.print(tick);
      Serial.print(F(" Sensor:"));
      Serial.print( sensorValue);
      Serial.print(F(" Volume: "));
      Serial.print(volume);
      Serial.println();
      
     }
     MP3player.setVolume(volume, volume);
    }
    
    delay(1);
}
