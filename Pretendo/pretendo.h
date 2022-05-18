#include "platformLibs.h"
#include "platformEndian.h"
#include "platformSystemSetup.h"
#include "platformGlobals.h"

#define PRETENDO_NTSC 0
#define PRETENDO_PAL  1

// Playback variables
const byte* music;
bool musicPlaying = false;
bool musicPaused = false;
bool musicLoop = true;
byte frameMillis = 16;
uint16_t musicBase;
uint16_t musicLen;
uint16_t musicLoopPoint;
uint16_t musicPos;
byte dmcSamples;
uint16_t dmcOffsets[16];
uint16_t dmcLengths[16];
bool sq1en = true;
bool sq2en = true;
bool trien = true;
bool noien = true;
bool dmcen = true;

// Timers
volatile uint32_t approxElapsedUs = 0;
volatile uint32_t lossyUsTimer = 0;
volatile uint32_t lossyMsTimer = 0;

// NES APU LUTs
byte pulseDutyTable[4] = { 32, 64, 128, 192 };
byte NESTriangleLUT[256];
byte NESTriangleRefTable[32] = { 
  15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};
uint32_t* NESLFSRFrequencyTable;
uint32_t NESLFSRFrequencyTable_NTSC[] = {
  447443, 223722, 111861, 55930, 27965, 18643, 13983, 11186, 8860, 7046, 4710, 3523, 2349, 1762, 880, 440
};
uint32_t NESLFSRFrequencyTable_PAL[] = {
  415652, 207826, 118758, 55420, 27710, 18893, 14090, 11234, 8844, 7045, 4697, 3522, 2348, 1761, 880, 440
};
// DMC deltas for reading 2 bits at once, halving the resolution
// Allows DMC emulation to run at 2x slower than the actual hardware
// This ensures the DMC always clocks bits slower than the sampling rate
// This allows the DMC emulation to have a worst case of 1 DMC sample per sound engine clock
// Additionally, the 4 values are loaded across the entire 256 possible combinations of one byte
// This saves the DMC emulation from having to mod 4 the bitstream constantly in order to extract the bits,
// ensuring faster operation
#ifdef DMC2
  volatile int8_t DMCDeltaMap[256] = {
    /* 00 */ -4,
    /* 01 */ 0,
    /* 10 */ 0,
    /* 11 */ 4,
    // ...
  };
// DMC deltas for reading 1 bit at once, just as the NES gods intended
// This is used for faster platforms (that is, not the megaAVR-based Uno or similar)
// Also loaded across entire 256 possible combos, saves us a mod
#else
  volatile int8_t DMCDeltaMap[256] = {
    /* 0(0) */ -2,
    /* 0(1) */ 2,
    /* 1(0) */ -2,
    /* 1(1) */ 2,
    // ...
  };
#endif
uint16_t* NESDMCFrequencyTable;
uint16_t NESDMCFrequencyTable_NTSC[] = {
  4182, 4710, 5264, 5593, 6258, 7046, 7919, 8363,
  9420, 11186, 12604, 13983, 16885, 21307, 24858, 33144
};
uint16_t NESDMCFrequencyTable_PAL[] = {
  4177, 4697, 5261, 5579, 6024, 7045, 7917, 8397,
  9447, 11234, 12596, 14090, 16965 , 21316 , 25191 , 33252
};
// APU DAC transfer function approximation LUTs
byte NESDACPulseLUT[] = {
  0, 3, 6, 9, 11, 14, 17, 19, 22, 24, 27, 29, 31, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 57, 59, 61, 62, 64, 66
};
byte NESDACTriNoiseDMCLUT [] = {
  0, 2, 3, 5, 7, 8, 10, 12, 13, 15, 16, 18, 20, 21, 23, 24, 26, 27, 29, 30, 32, 33, 35, 36, 37, 39, 40, 42, 43, 44, 46, 47,
  49, 50, 51, 52, 54, 55, 56, 58, 59, 60, 61, 63, 64, 65, 66, 68, 69, 70, 71, 72, 73, 75, 76, 77, 78, 79, 80, 81, 83, 84, 85,
  86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113,
  114, 115, 115, 116, 117, 118, 119, 120, 121, 122, 122, 123, 124, 125, 126, 127, 127, 128, 129, 130, 131, 132, 132, 133, 134, 135,
  136, 136, 137, 138, 139, 139, 140, 141, 142, 142, 143, 144, 145, 145, 146, 147, 148, 148, 149, 150, 150, 151, 152, 152, 153, 154,
  155, 155, 156, 157, 157, 158, 159, 159, 160, 160, 161, 162, 162, 163, 164, 164, 165, 166, 166, 167, 167, 168, 169, 169, 170, 170,
  171, 172, 172, 173, 173, 174, 175, 175, 176, 176, 177, 177, 178, 179, 179, 180, 180, 181, 181, 182, 182, 183, 184, 184, 185, 185,
  186, 186, 187, 187, 188, 188, 189, 189
};

#ifndef PRETENDO_NO_TRANSFER_FUNCTION
  inline byte mixPulse(byte x) { return NESDACPulseLUT[x]; }
#else
  inline byte mixPulse(byte x) { return x * 2; }
#endif

void setRegion(byte region) {
  byte prevRegion = (NESDMCFrequencyTable == NESDMCFrequencyTable_NTSC ? PRETENDO_NTSC : PRETENDO_PAL);
  bool changedRegion = region != prevRegion;
  if (region == PRETENDO_NTSC) {
    frameMillis = 17;
    NESLFSRFrequencyTable = NESLFSRFrequencyTable_NTSC;
    NESDMCFrequencyTable = NESDMCFrequencyTable_NTSC;
    if (changedRegion) Serial.println("Region set to NTSC.");
  } else {
    frameMillis = 20;
    NESLFSRFrequencyTable = NESLFSRFrequencyTable_PAL;
    NESDMCFrequencyTable = NESDMCFrequencyTable_PAL;
    if (changedRegion) Serial.println("Region set to PAL.");
  }
}

// Alternative utility timing functions
inline uint32_t Micros() {
  return lossyUsTimer;
}

inline uint32_t Millis() {
  return lossyMsTimer;
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
  volatile byte* addr;
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
    bitClock.accumulator = 0;
    cmpBitClock = 0;
  }

  void play(byte* sampleAddress, uint16_t sampleLength, byte playbackSpeed, byte initialSeed, bool isLooping) {
    addr = sampleAddress;
    len = sampleLength;
    #ifdef DMC2
      bitClockDelta = (NESDMCFrequencyTable[playbackSpeed] >> 1/* Emulated DMC is running at 1/2 the temporal resolution */) * PHASE_1HZ;
    #else
      bitClockDelta = NESDMCFrequencyTable[playbackSpeed] * PHASE_1HZ;
    #endif
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
    #ifdef DMC2
      if (bitClock.value != cmpBitClock) {
    #else
      while (bitClock.value > cmpBitClock) {
    #endif
      if (bitOffset == 0) {
        latch = pgm_read_byte(addr + offset);
      }
      output += DMCDeltaMap[latch >> bitOffset];
      if (output > 200) {
        output = 0;
      } else if (output > 127) {
        output = 127;
      }
      #ifdef DMC2
        if (bitOffset == 6) {
      #else
        if (bitOffset == 7) {
      #endif
        bitOffset = 0;
        offset++;
        if (offset == len) {
          reset(looping);
          if (!looping) {
            bitClockDelta = 0;
          }
        }
      } else {
        #ifdef DMC2
          bitOffset += 2;
        #else
          bitOffset++;
        #endif
      }
      #ifdef DMC2
        cmpBitClock = bitClock.value;
      #else
        cmpBitClock++;
      #endif
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
uint32_t msCounter;

// Used for selecting tracks
const byte* demoMusic;

// Oscillator outputs
volatile byte s1, s2, tri, tnd;
volatile uint16_t noise;
volatile uint16_t dpcm;

// Performs sound synthesis
// Also keeps track of time (approximate)
#ifdef PLATFORM_ESP32
void IRAM_ATTR audioCallback() {
  portENTER_CRITICAL_ISR(&timerMux);
#else
void audioCallback() {
#endif
  s1 = Sqr1.synthesize() * sq1en;
  s2 = Sqr2.synthesize() * sq2en;
  tri = Triangle.synthesize() * trien;
  noise = Noise.synthesize() * noien;
  dpcm = DPCM.synthesize() * dmcen;

  #ifndef PRETENDO_NO_TRANSFER_FUNCTION
    tnd = NESDACTriNoiseDMCLUT[tri * 3 + noise * 2 + dpcm];
  #else
    tnd = ((tri * 3 + noise * 2 + dpcm) * 11) >> 4;
  #endif

  #ifndef PRETENDO_MONO
    #define LMIX tnd + mixPulse(((s1 * 10) >> 4) + s2)
    #define RMIX tnd + mixPulse(s1 + ((s2 * 10) >> 4))
  #else
    #define LMIX tnd + mixPulse(s1 + s2)
    #define RMIX LMIX
  #endif

  #ifdef PLATFORM_ARDUINO_UNO
    // Left channel
    OCR0A = LMIX;
    // Right channel
    OCR0B = RMIX;
  #endif
  
  #ifdef PLATFORM_ARDUINO_DUE
    // Left channel
    analogWrite(DAC0, LMIX << 4);
    // Right channel
    analogWrite(DAC1, RMIX << 4);
  #endif

  #ifdef PLATFORM_ESP32
    // Left channel
    dacWrite(DAC1, LMIX);
    // Right channel
    dacWrite(DAC2, RMIX);
  #endif
  
  lossyUsTimer += CALLBACK_US;
  approxElapsedUs += CALLBACK_US;
  if (approxElapsedUs >= 1000) {
    approxElapsedUs -= 1000;
    lossyMsTimer++;
  }
#ifdef PLATFORM_ESP32
  portEXIT_CRITICAL_ISR(&timerMux);
#endif
}

#ifdef PLATFORM_ARDUINO_DUE
  void TC3_Handler() {
    audioCallback();
    TC_GetStatus(TC1, 0);
  }

  void startTimer(Tc *tc, uint32_t channel, IRQn_Type irq, uint32_t frequency) {
    pmc_set_writeprotect(false);
    pmc_enable_periph_clk((uint32_t)irq);
    TC_Configure(tc, channel, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | TC_CMR_TCCLKS_TIMER_CLOCK4);
    uint32_t rc = VARIANT_MCK/128/frequency; // 128 because we selected TIMER_CLOCK4 above
    TC_SetRA(tc, channel, rc/2); // 50% high, 50% low
    TC_SetRC(tc, channel, rc);
    TC_Start(tc, channel);
    tc->TC_CHANNEL[channel].TC_IER = TC_IER_CPCS;
    tc->TC_CHANNEL[channel].TC_IDR =~ TC_IER_CPCS;
    NVIC_EnableIRQ(irq);
  }
#endif
