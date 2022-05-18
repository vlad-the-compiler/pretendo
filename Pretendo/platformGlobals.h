#ifdef PLATFORM_ESP32
  hw_timer_t* timer = NULL;
  portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#endif
