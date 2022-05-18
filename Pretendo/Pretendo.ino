// Select target platform (uncomment one)
#define PLATFORM_ARDUINO_UNO
// #define PLATFORM_ARDUINO_DUE
// #define PLATFORM_ESP32

// Uncomment to disable Stereo
// #define PRETENDO_MONO

// Uncomment to disable non-linear mixing emulation
// #define PRETENDO_NO_TRANSFER_FUNCTION

#include "music.h"
#include "pretendo.h"

void killSoundEngine() {
  Sqr1.volume = 0;
  Sqr2.volume = 0;
  Triangle.gate = false;
  Noise.volume = 0;
  DPCM.kill();
}

void playMusic(const byte* musicFile, bool looping = true) {
  killSoundEngine();
  
  // Select music
  music = musicFile;

  // Set loop
  musicLoop = looping;

  // Start music
  musicLen = pgm_read_byte(music) * 256 + pgm_read_byte(music + 1);
  musicLoopPoint = pgm_read_byte(music + 2) * 256 + pgm_read_byte(music + 3);
  // Flags currently only used for region (0 = NTSC, 1 = PAL)
  byte flagsByte = pgm_read_byte(music + 4);
  setRegion(flagsByte);
  dmcSamples = pgm_read_byte(music + 5);
  uint16_t currentFileOffset = 6 + dmcSamples * 2;
  for (int i = 0; i < dmcSamples; i++) {
    dmcLengths[i] =  pgm_read_byte(music + 6 + i * 2) * 256 + pgm_read_byte(music + 6 + i * 2 + 1);
    dmcOffsets[i] = currentFileOffset;
    currentFileOffset += dmcLengths[i];
  }
  musicBase = currentFileOffset;
  musicPos = musicBase;
  musicPlaying = true;
  musicPaused = false;
  // Keep time for playback
  msCounter = Millis();
}

void stopMusic() {
  killSoundEngine();
  musicPlaying = false;
  musicPaused = false;
}

void rewindMusic() {
  musicPos = musicBase + musicLoopPoint;
}

void pauseMusic() {
  if (musicPlaying) {
    killSoundEngine();
    musicPlaying = false;
    musicPaused = true;
  }
}

void unpauseMusic() {
  if (musicPaused) {
    musicPlaying = true;
    musicPaused = false; 
  }
}

void playPauseMusic() {
  if (musicPaused) {
    unpauseMusic();
  } else {
    pauseMusic();
  }
}

void selectDemoMusic(char select) {
  demoMusic = NULL;
  switch (select) {
    case '0':
      demoMusic = music_smb2_underground;
    break;
    case '1':
      demoMusic = music_smb_underwater;
    break;
    case '2':
      demoMusic = music_smb_flagpole;
    break;
    case '3':
      demoMusic = music_contra_intro;
    break;
    case '4':
      demoMusic = music_contra_base;
    break;
    case '5':
      demoMusic = music_metroid_brinstar;
    break;
    case '6':
      demoMusic = music_dr_mario_pre_game;
    break;
    case '7':
      demoMusic = music_duck_hunt_level_start;
    break;
    case '8':
      demoMusic = music_duck_hunt_level_complete;
    break;
    case '9':
      // demoMusic = music_dev;
    break;
  }
  if (demoMusic != NULL) {
    playMusic(demoMusic);
  }
}

void setup() {
  // Initialize system
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  delay(250);
  Serial.println("Starting up...");
  delay(250);
  
  #ifdef PLATFORM_ARDUINO_UNO
    pinMode(5, OUTPUT);
    pinMode(6, OUTPUT);
    setPwmFrequency(5, 1);
    setPwmFrequency(6, 1);
    analogWrite(5, 1);
    analogWrite(6, 1);
  #endif

  #ifdef PLATFORM_ARDUINO_DUE
    pinMode(DAC0, OUTPUT);
    pinMode(DAC1, OUTPUT);
    analogWriteResolution(12);
    analogWrite(DAC0, 1);
    analogWrite(DAC1, 1);
  #endif

  #ifdef PLATFORM_ESP32
    pinMode(DAC1, OUTPUT);
    pinMode(DAC2, OUTPUT);
    dacWrite(DAC1, 1);
    dacWrite(DAC2, 1);
  #endif

  // Load NES Triangle DDS table
  int kLUT = 0;
  for (int i = 0; i < 32; i++) {
    for (int j = 0; j < 8; j++) {
      NESTriangleLUT[kLUT++] = NESTriangleRefTable[i];
    }
  }
  Triangle.LUT = NESTriangleLUT;

  // Copy the DMC delta pattern for all 8-bit combinations
  // This saves a mod operation inside the DMC emulation
  // The AVR CPU is already at almost full load at this point,
  // so using some RAM for faster execution comes at little loss,
  // since there isn't any more free execution time for any other
  // logic to run that would use any significant memory
  // This does not offer any significant performance gain on other
  // dev platforms, but is left as-is for compatibility
  // If it ain't broken, don't fix it
  for (int i = 1; i < 64; i++) {
    for (int j = 0; j < 4; j++) {
      DMCDeltaMap[i * 4 + j] = DMCDeltaMap[j];
    }
  }

  // Initialize Audio Routine
  #ifdef PLATFORM_ARDUINO_UNO
    Timer1.initialize(CALLBACK_US);
    Timer1.attachInterrupt(audioCallback);
  #endif

  #ifdef PLATFORM_ARDUINO_DUE
    startTimer(TC1, 0, TC3_IRQn, SAMPLE_RATE);
  #endif
  #ifdef PLATFORM_ESP32
    timer = timerBegin(0, APB_CLOCK_PRESCALER, true);
    Serial.println("1");
    timerAttachInterrupt(timer, &audioCallback, true);
    Serial.println("2");
    timerAlarmWrite(timer, 1, true);
    Serial.println("3");
    timerAlarmEnable(timer);
    Serial.println("a");
  #endif
  Serial.println("Done!");

  // Play music
  selectDemoMusic('1');

  // Display greeting
  Serial.println();
  Serial.println(F("Welcome to Pretendo!"));
  Serial.println(F("A high-level NES APU emulator for the Arduino Uno / Due and ESP32"));
  Serial.println(F("Select a track:"));
  Serial.println(F("(0) SMB2: Underground"));
  Serial.println(F("(1) SMB: Underwater"));
  Serial.println(F("(2) SMB: Flag Pole"));
  Serial.println(F("(3) Contra: Intro"));
  Serial.println(F("(4) Contra: Base"));
  Serial.println(F("(5) Metroid: Brinstar"));
  Serial.println(F("(6) Dr. Mario: Pre-game screen"));
  Serial.println(F("(7) Duck Hunt: Level start"));
  Serial.println(F("(8) Duck Hunt: Level complete"));
  Serial.println(F("(9) Your own music"));
  Serial.println();
  Serial.println(F("'p' = Play/Pause, 'r' = Rewind, 'l' = Toggle loop"));
  Serial.println(F("Switch regions with 'q' (PAL) and 'w' (NTSC)"));
  Serial.println(F("'+' / '-' = Track speed"));
  Serial.println(F("Toggle channels: 's' (Sq 1), 'S' (Sq 2), 't' (Triangle), 'n' (Noise), 'd' (DMC)"));
  Serial.println();
  Serial.println(F("https://github.com/vlad-the-compiler/pretendo"));
  Serial.println();
}

// Music parser state
byte control;
bool sqr1FFlag;
bool sqr2FFlag;
bool triangleFFlag;
bool sqr1ParamFlag;
bool sqr2ParamFlag;
bool noiseParamFlag;
bool triangleActiveFlag;
bool extendedFlag;

void loop() {
  // Control via Serial Monitor
  while (Serial.available()) {
    char rd = Serial.read();
    switch (rd) {
      default:
        selectDemoMusic(rd);
      break;
      case 'p':
        playPauseMusic();
      break;
      case 'r':
        rewindMusic();
      break;
      case 'l':
        musicLoop = !musicLoop;
        Serial.print("Loop: ");
        Serial.println(musicLoop);
      break;
      case 's':
        sq1en = !sq1en;
      break;
      case 'S':
        sq2en = !sq2en;
      break;
      case 't':
        trien = !trien;
      break;
      case 'n':
        noien = !noien;
      break;
      case 'd':
        dmcen = !dmcen;
      break;
      case 'q':
        setRegion(PRETENDO_NTSC);
      break;
      case 'w':
        setRegion(PRETENDO_PAL);
      break;
      case '-':
        frameMillis++;
      break;
      case '+':
        frameMillis--;
      break;
    }
  }
  if (musicPlaying) {
    if (Millis() - msCounter >= frameMillis) {
      digitalWrite(LED_BUILTIN, LOW);
      if (musicPos - musicBase >= musicLen) {
        if (musicLoop) {
          rewindMusic();
        } else {
          stopMusic();
        }
      }
      byte control = pgm_read_byte(music + musicPos);
      musicPos++;
      sqr1FFlag = (control & 1);
      sqr2FFlag = (control & 2);
      triangleFFlag = (control & 4);
      sqr1ParamFlag = (control & 8);
      sqr2ParamFlag = (control & 16);
      noiseParamFlag = (control & 32);
      triangleActiveFlag = (control & 64);
      extendedFlag = (control & 128);
  
      if (sqr1FFlag) {
        Sqr1.phaseDelta = (pgm_read_byte(music + musicPos) * 256 + pgm_read_byte(music + musicPos + 1)) * PHASE_1HZ;
        musicPos += 2;
      }
      if (sqr2FFlag) {
        Sqr2.phaseDelta = (pgm_read_byte(music + musicPos) * 256 + pgm_read_byte(music + musicPos + 1)) * PHASE_1HZ;
        musicPos += 2;
      }
      if (triangleFFlag) {
        Triangle.phaseDelta = (pgm_read_byte(music + musicPos) * 256 + pgm_read_byte(music + musicPos + 1)) * PHASE_1HZ;
        if (Triangle.phaseDelta > 10000) {
          Triangle.phaseDelta = 0;
        }
        musicPos += 2;
      }
      if (sqr1ParamFlag) {
        byte data = pgm_read_byte(music + musicPos);
        Sqr1.volume = data & 0x0F;
        Sqr1.duty = pulseDutyTable[data >> 4];
        musicPos++;
      }
      if (sqr2ParamFlag) {
        byte data = pgm_read_byte(music + musicPos);
        Sqr2.volume = data & 0x0F;
        Sqr2.duty = pulseDutyTable[data >> 4];
        musicPos++;
      }
      if (noiseParamFlag) {
        byte data = pgm_read_byte(music + musicPos);
        Noise.volume = data & 0x0F;
        Noise.bitClockDelta = NESLFSRFrequencyTable[data >> 4] * PHASE_1HZ;
        musicPos++;
        if (Noise.volume > 0) {
          digitalWrite(LED_BUILTIN, HIGH);
        }
      }
      Triangle.Gate(triangleActiveFlag);
      if (extendedFlag) {
        byte data = pgm_read_byte(music + musicPos);
        Noise.shortMode = (data >> 7);
        bool dmcFlag = ((data & 64) >> 6);
        bool dmcKillFlag = ((data & 32) >> 5);
        if (dmcFlag) {
          data = pgm_read_byte(music + musicPos + 1);
          byte dmcIndex = data & 0x0F;
          byte fDMCIndex = data >> 4;
          data = pgm_read_byte(music + musicPos + 2);
          byte dmcSeed = data & 127;
          bool dmcLoop = data >> 7;
          byte* dmcAddr = (byte*)(music + dmcOffsets[dmcIndex]);
          DPCM.play(dmcAddr, dmcLengths[dmcIndex], fDMCIndex, dmcSeed, dmcLoop);
          musicPos += 3;
        } else {
          musicPos++;
        }
        if (dmcKillFlag) {
          DPCM.kill();
        }
      }
      msCounter = Millis();
    }
  }
}
