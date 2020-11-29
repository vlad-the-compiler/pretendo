#include <TimerOne.h>
#include "fPWM.h"
#include "music.h"

// Uncomment for big-endian platforms
// Use appropriately when porting to different platforms
#define PLATFORM_LITTLE_ENDIAN

// Playback variables
const byte* music;
bool musicPlaying = false;
bool musicPaused = false;
bool musicLoop = true;
uint16_t musicBase;
uint16_t musicLen;
uint16_t musicLoopPoint;
uint16_t musicPos;
byte dmcSamples;
uint16_t dmcOffsets[16];
uint16_t dmcLengths[16];

#define CALLBACK_US 43 // ~22.050Hz
#define PHASE_1HZ 2.818048 // 65536 (phase accumulator length) / ~22.050Hz
#define MAX_VOL 15

// Mixing
#define SQR_VOL_AMP 3
#define TRI_VOL_AMP 2
#define NOISE_VOL_AMP 1.5

// Timers
volatile uint32_t elapsedUs = 0;
volatile uint32_t usTimer = 0;
volatile uint32_t msTimer = 0;

// NES APU LUTs
byte pulseDutyTable[4] = { 32, 64, 128, 192 };
byte NESTriangleLUT[256];
byte NESTriangleRefTable[32] = { 
  15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};
uint32_t* NESLFSRFrequencyTable;
uint32_t NESLFSRFrequencyTable_NTSC[] = {
  447500, 223750, 111875, 55938, 27969, 18646, 13984, 11188,
  8861, 7047, 4711, 3524, 2349, 1762, 880, 440
};
// DMC deltas for reading 2 bits at once, halving the resolution
// Allows DMC emulation to run at 2x slower than the actual hardware
// This ensures the DMC always clocks bits slower than the sampling rate
// This allows the DMC emulation to have a worst case of 1 DMC sample per sound engine clock
// Additionally, the 4 values are loaded across the entire 256 possible combinations of one byte
// This saves the DMC emulation from having to mod 4 the bitstream constantly in order to extract the bits,
// ensuring faster operation
volatile int8_t DMC2BitDeltas[256] = {
  /* 00 */ -4,
  /* 01 */ 0,
  /* 10 */ 0,
  /* 11 */ 4,
  // ...
};
uint16_t* NESDMCFrequencyTable;
uint16_t NESDMCFrequencyTable_NTSC[] = {
  4182, 4710, 5264, 5593, 6258, 7046, 7919, 8363,
  9420, 11186, 12604, 13983, 16885, 21307, 24858, 33144
};

// Alternative utility timing functions
inline uint32_t Micros() {
  return usTimer;
}

inline uint32_t Millis() {
  return msTimer;
}

class PulseWave {
  public:
  volatile union {
    uint16_t accumulator;
    #ifdef PLATFORM_LITTLE_ENDIAN
    struct __attribute__((packed)) {
      byte accuLowByte;
      byte value;
    };
    #else
    byte value;
    #endif
  } phase = { 0 };
  volatile uint16_t phaseDelta = 0;
  volatile byte duty = 128;
  volatile byte volume = 0;

  inline byte synthesize() {
    phase.accumulator += phaseDelta;
    return (phase.value < duty) * volume;
  }
};

class GatedLUTWave {  
  public:
  byte* LUT;
  volatile union {
    uint16_t accumulator;
    #ifdef PLATFORM_LITTLE_ENDIAN
    struct __attribute__((packed)) {
      byte accuLowByte;
      byte value;
    };
    #else
    byte value;
    #endif
  } phase = { 0 };
  volatile uint16_t phaseDelta = 0;
  volatile bool gate = false;
  volatile byte sample = 0;

  void Gate(bool setGate) {
    // Reset accumulator to sample level 0 when gating to prevent aliasing
    if ((setGate) && (!gate)) {
      phase.accumulator = 32768;
    }
    gate = setGate;
  }

  inline byte synthesize() {
    if (gate) {
      sample = LUT[phase.value];
      phase.accumulator += phaseDelta;
    } else {
      // Fall off to prevent aliasing
      if (sample > 0) {
        sample--;
      }
    }
    return sample;
  }
};

class LFSR {
  public:
  volatile union {
    uint32_t accumulator;
    #ifdef PLATFORM_LITTLE_ENDIAN
    struct __attribute__((packed)) {
      uint16_t accuLow;
      uint16_t value;
    };
    #else
    uint16_t value;
    #endif
  } bitClock = { 0 };
  volatile uint16_t cmpBitClock = 0;
  volatile uint32_t bitClockDelta = 0;
  volatile bool shortMode = false;
  volatile uint16_t seed = 1;
  volatile byte feedback;
  volatile bool output;
  volatile byte volume = 0;

  inline byte synthesize() {
    bitClock.accumulator += bitClockDelta;
    if (bitClock.value != cmpBitClock) {
      output = seed & 1;
      if (shortMode) {
        feedback = output ^ ((seed & 64) >> 6);
      } else {
        feedback = output ^ ((seed & 2) >> 1);
      }
      seed >>= 1;
      seed |= (feedback << 14);
      cmpBitClock = bitClock.value;
    }
    return output * volume;
  }
};

class DMC {
  public:
  volatile union {
    uint32_t accumulator;
    #ifdef PLATFORM_LITTLE_ENDIAN
    struct __attribute__((packed)) {
      uint16_t accuLow;
      uint16_t value;
    };
    #else
    uint16_t value;
    #endif
  } bitClock = { 0 };
  volatile uint16_t cmpBitClock = 0;
  volatile uint32_t bitClockDelta = 0;
  volatile uint16_t offset = 0;
  volatile uint16_t bitOffset = 0;
  volatile uint16_t* addr;
  volatile uint16_t len;
  volatile bool looping;
  volatile byte latch;
  volatile byte seed = 60;
  volatile byte output = 60;

  void reset(bool playback = false) {
    offset = 0;
    if (playback) {
      output = seed;
    }
    bitClock = { 0 };
    cmpBitClock = 0;
  }

  void play(uint16_t sampleAddress, uint16_t sampleLength, byte playbackSpeed, byte initialSeed, bool isLooping) {
    addr = sampleAddress;
    len = sampleLength;
    bitClockDelta = (NESDMCFrequencyTable[playbackSpeed] >> 2/* APU is 1/2 of CPU, then emulated DMC is 1/2 of that*/) * PHASE_1HZ;
    seed = initialSeed;
    looping = isLooping;
    reset(true);
  }

  void kill() {
    reset();
    bitClockDelta = 0;
  }

  byte synthesize() {
    bitClock.accumulator += bitClockDelta;
    if (bitClock.value != cmpBitClock) {
      if (bitOffset == 0) {
        latch = pgm_read_byte(addr + offset);
      }
      output += DMC2BitDeltas[latch >> bitOffset];
      if (output > 200) {
        output = 0;
      } else if (output > 127) {
        output = 127;
      }
      if (bitOffset == 6) {
        bitOffset = 0;
        offset++;
        if (offset == len) {
          reset(looping);
          if (!looping) {
            bitClockDelta = 0;
          }
        }
      } else {
        bitOffset += 2;
      }
      cmpBitClock = bitClock.value;
    }
    return output;
  }
};

PulseWave Sqr1;
PulseWave Sqr2;
GatedLUTWave Triangle;
LFSR Noise;
DMC DPCM;

// Used for updating the channel state inside the main loop
uint32_t ms;

// Used for selecting tracks
const byte* demoMusic;

// Oscillator outputs
volatile byte s1, s2, tri;
volatile uint16_t noise;
volatile uint16_t dpcm;

// Timer1 Compare Interrupt Routine
// Performs sound synthesis
// Also keeps track of time (lossy)
void audioCallback() {
  s1 = Sqr1.synthesize();
  s2 = Sqr2.synthesize();
  tri = Triangle.synthesize();
  noise = Noise.synthesize();
  dpcm = (DPCM.synthesize() * 5) >> 2;
  // Left channel
  OCR0A = dpcm + tri + (s2 >> 1) + s1 + noise;
  // Right channel
  OCR0B = dpcm + tri + (s1 >> 1) + s2 + noise;
  
  usTimer += CALLBACK_US;
  elapsedUs += CALLBACK_US;
  if (elapsedUs >= 1000) {
    elapsedUs -= 1000;
    msTimer++;
  }
}

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
  // Flags byte used for region and other potential future features, currently unused
  byte unusedFlagsByte = pgm_read_byte(music + 4);
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
  // Keep time for playback
  ms = Millis();
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
    case '1':
      demoMusic = music_smb3_map;
    break;
    case '2':
      demoMusic = music_smb3_pswitch;
    break;
    case '3':
      demoMusic = music_smb3_lost_life;
    break;
    case '4':
      demoMusic = music_kirby_boss;
    break;
    case '5':
      demoMusic = music_kirby_cloud_tops;
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
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  setPwmFrequency(5, 1);
  setPwmFrequency(6, 1);
  analogWrite(5, 1);
  analogWrite(6, 1);
  
  // Load NES Triangle DDS table
  int kLUT = 0;
  for (int i = 0; i < 32; i++) {
    for (int j = 0; j < 8; j++) {
      NESTriangleLUT[kLUT++] = NESTriangleRefTable[i] * TRI_VOL_AMP;
    }
  }
  Triangle.LUT = NESTriangleLUT;

  // Copy the DMC 2-bit delta pattern for all 8-bit combinations
  // This saves a mod 4 (& 0x03) operation inside the DMC emulation
  // The AVR CPU is already at almost full load at this point,
  // so using some RAM for faster execution comes at little loss,
  // since there isn't any more free execution time for any other
  // logic to run that would use any significant memory
  for (int i = 1; i < 64; i++) {
    for (int j = 0; j < 4; j++) {
      DMC2BitDeltas[i * 4 + j] = DMC2BitDeltas[j];
    }
  }

  // Region-specifics
  NESLFSRFrequencyTable = NESLFSRFrequencyTable_NTSC;
  NESDMCFrequencyTable = NESDMCFrequencyTable_NTSC;

  // Initialize Audio ISR
  Timer1.initialize(CALLBACK_US);
  Timer1.attachInterrupt(audioCallback);

  // Play music
  selectDemoMusic('1');
  // Or use this for your own music:
  // playMusic(music_dev);

  // Display greeting
  Serial.println("Welcome to Pretendo!");
  Serial.println("A high-level NES APU emulator for the Arduino Uno");
  Serial.println("Select a track:");
  Serial.println("    (1) SMB3: Map");
  Serial.println("    (2) SMB3: P-Switch");
  Serial.println("    (3) SMB3: Life Lost");
  Serial.println("    (4) Kirby's Adventure: Boss");
  Serial.println("    (5) Kirby's Adventure: Cloud Tops (Intro ver.)");
  Serial.println();
  Serial.println("Type 'p' for Play/Pause, 'r' to Rewind track, 'l' to toggle Loop (resets on track change)");
  Serial.println();
  Serial.println("https://github.com/vlad-the-compiler/pretendo");
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
    }
  }
  if (musicPlaying) {
    if (Millis() - ms >= 17) {
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
        Sqr1.volume = (data & 0x0F) * SQR_VOL_AMP;
        Sqr1.duty = pulseDutyTable[data >> 4];
        musicPos++;
      }
      if (sqr2ParamFlag) {
        byte data = pgm_read_byte(music + musicPos);
        Sqr2.volume = (data & 0x0F) * SQR_VOL_AMP;
        Sqr2.duty = pulseDutyTable[data >> 4];
        musicPos++;
      }
      if (noiseParamFlag) {
        byte data = pgm_read_byte(music + musicPos);
        Noise.volume = (data & 0x0F) * NOISE_VOL_AMP;
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
          DPCM.play((byte*)(music + dmcOffsets[dmcIndex]), dmcLengths[dmcIndex], fDMCIndex, dmcSeed, dmcLoop);
          musicPos += 3;
        } else {
          musicPos++;
        }
        if (dmcKillFlag) {
          DPCM.kill();
        }
      }
      ms = Millis();
    }
  }
}
