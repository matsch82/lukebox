# include "vs1053_SdFat.h"
#include "PCF8574.h"

#define NO_TRACK ((uint8_t)0xff)
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


void setupGpio(){
  pcf8574.pinMode(P0, INPUT);
  pcf8574.pinMode(P1, INPUT);
  pcf8574.pinMode(P2, INPUT);
  pcf8574.pinMode(P3, INPUT);
  pcf8574.pinMode(P4, INPUT);
  pcf8574.pinMode(P5, INPUT);
  pcf8574.pinMode(P6, INPUT);
  pcf8574.begin();
  
}

void setupPlayer() {
  // Player starten
  MP3player.begin();
  // Höhen: erlaubte Werte: -8 bis 7
  MP3player.setTrebleAmplitude(0);
  // Bässe: erlaubte Werte 0 bis 15
  MP3player.setBassAmplitude(14);
  // Status des Players ermitteln
  if (MP3player.getState() == 1) Serial.println("Player erfolgreich gestartet");
  // Lautstärke setzen -> links, rechts -> 1, 1 sehr laut
  // je größer die Werte desto leiser
  MP3player.setVolume(10, 10);
  MP3player.setMonoMode(3);
}

void beep() {
  MP3player.SendSingleMIDInote();
}

void printSdFiles() {
  Serial.println("------------------------------");
  char Dateiname[13];
  if (!sd.chdir("/")) sd.errorHalt("keine SD-Karte vorhanden");
  Verzeichnis.open("/");
  Serial.println("File Size");
  Serial.println("------------------------------");
  while (Datei.openNext(&Verzeichnis, O_READ))
  {
    Datei.getName(Dateiname, sizeof(Dateiname));
    // handelt es sich um eine Musikdatei (isFnMusic)
    if (isFnMusic(Dateiname) )
    {
      Serial.print(Dateiname);
      // Dateigröße ermitteln, in MB umwandeln, Punkt durch Komma ersetzen
      float DateiGroesse = Datei.fileSize();
      String Groesse = String(DateiGroesse / 1000000);
      Groesse.replace(".", ",");
      Serial.println("\t" + Groesse + " MB");
      TrackMax ++;
    }
    Datei.close();
    Serial.println("Anzahl der Tracks: " + String(TrackMax));
  }
  Serial.println();
}

const uint8_t sensorPin = A0; 
const uint8_t sensorValue = 0;

void setup()
{
  Serial.begin(57600);

  delay(100);
  sd.begin(SD_SEL, SPI_FULL_SPEED);
  printSdFiles();
  setupPlayer() ;
  setupGpio();
}


static uint8_t track_current;
static uint32_t tick;
static bool initDone;
void playTrack(int i){
  if(!initDone){
    return;
  }
  Serial.print("stop track");
  Serial.println(track_current);
  MP3player.stopTrack();
  delay(100);
  beep();
  if(MP3player.isPlaying() == false){
    track_current=NO_TRACK;
  }
  if(i <= TrackMax) {
    if(track_current != i){
 
      MP3player.playTrack(i);
     track_current=i;}
     else {
      track_current=NO_TRACK;
     }
  }
  else {
     delay(100);
     beep();
     delay(100);
     beep();    
  }
}

uint8_t getVolumeFromSensorValue(int sensor){
  // sensor max val 665  -> max volume 0
  // sensor min val 0 --> min volume 100
  int tmp = 680 - sensor;
  tmp = tmp*10;
  tmp = tmp/ 75;
  if(tmp < 0) tmp = 0;
  if(tmp > 100) tmp = 100;
  return tmp+10;
   
}

void loop()
{
    tick++;

    if(!initDone && tick==300){
      initDone = true;
    }
    
    PCF8574::DigitalInput di = pcf8574.digitalReadAll();
    
    if(di.p0 && di.p0 != dio.p0){ Serial.println("schwarz");playTrack(7);}
    if(di.p1 && di.p1 != dio.p1){ Serial.println("orange");playTrack(6);}
    if(di.p2 && di.p2 != dio.p2){ Serial.println("weiß");playTrack(5); }
    if(di.p3 && di.p3 != dio.p3){ Serial.println("gruen"); playTrack(4);}
    if(di.p4 && di.p4 != dio.p4){ Serial.println("rot"); playTrack(3);}
    if(di.p5 && di.p5 != dio.p5){ Serial.println("blau"); playTrack(2);}
    if(di.p6 && di.p6 != dio.p6){ Serial.println("gelb"); playTrack(1);}
    
    dio = di;
    
    if(tick % 10 == 0) {
     int sensorValue = analogRead(sensorPin);
     int volume = getVolumeFromSensorValue(sensorValue);
     
     if(tick % 200 == 0) {
      Serial.print("Tick: ");
      Serial.print(tick);
      Serial.print(" Sensor:");
      Serial.print( sensorValue);
      Serial.print(" Volume: ");
      Serial.print(volume);
      Serial.println();
      
     }
     MP3player.setVolume(volume, volume);
    }
    
    delay(1);
}
