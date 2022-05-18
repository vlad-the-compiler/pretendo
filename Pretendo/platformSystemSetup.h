#ifdef PLATFORM_ARDUINO_UNO
  #define CALLBACK_US 50 // Sampling Period => 20KHz Sampling Rate
  #define PHASE_1HZ 3.2768 // 65536 (phase accumulator length) / Sampling Rate
  #define DMC2 // Half DMC resolution
#endif

#ifdef PLATFORM_ARDUINO_DUE
  #define SAMPLE_RATE 65536
  #define CALLBACK_US 15
  #define PHASE_1HZ 1
#endif

#ifdef PLATFORM_ESP32
  #define SAMPLE_RATE 65536
  #define APB_CLOCK_PRESCALER 1221
  #define CALLBACK_US 15
  #define PHASE_1HZ 1
  #define LED_BUILTIN 2
  #define DAC1 25
  #define DAC2 26
#endif
