<img src="https://i.imgur.com/sTk7vtS.png" width="35%">

Turn an Arduino Uno (or any ATmega328-based dev board) into an NES APU emulator!

# Demos

Captured via Line In from an Arduino Uno
- [Kirby's Adventure](https://soundcloud.com/user-358261734/pretendo-kirby-test)
- [RECCA (Summer Carnival '92 - Naxatsoft / KID)](https://soundcloud.com/user-358261734/pretendo-recca-test)

# Features

- Full support for all 5 channels (2 x Square, Triangle, Noise, DPCM)
- Stereo sound
- Oneshot / Looping / Looping with Intro support
- Logged format with rudimentary compression

# Dependencies

- TimerOne by Paul Stoffregen (requires download)

# Setup

- Download [TimerOne](https://github.com/PaulStoffregen/TimerOne) and add to your Arduino /Libraries folder
- See wiring diagram below
- Open Serial Monitor
- Upload Sketch
- Instructions appear inside Serial Monitor
- Enjoy!
- *See 'Advanced Use' below for how to convert your own files*

# Wiring Diagram

Use 220Ω resistors

This uses PWM to simulate a DAC, I wouldn't wire this to anything other than simple, analog, cheap headphones

<img src="https://i.imgur.com/4uJwjzQ.png" width="80%%">

# Advanced Use

Converting your own files is Easy™

It is done via a Lua script and the FCEUX NES emulator
- Download [FCEUX](http://fceux.com/web/download.html)
- Open FCEUX, load up your favourite ROM in the form of an NSF
- Make sure you have the NES controller SELECT button bound in Settings
- File > Lua > New Lua Script Window > Browse... *fceux-apu-capture.lua*
- You're now ready to convert your own music

### Recording NES music via FCEUX

Using the Lua script, we can capture music data for Pretendo to play. After selecting a ROM, you are free to nagivate inside FCEUX and find the track you wish to record.
Once you have found a track, write down its number inside the *Arguments* field in the Lua script window in FCEUX.
If the track should not loop, also write down *--oneshot* after the track number.
After entering the track number and the optional *--oneshot* argument, press Run in the Lua script window. The capture process will start, and the emulator will start playing back the music.
- **If the selected music does not loop** (i.e. it stops when it ends), press the **SELECT** key when you wish to end the capture, and the data will be written into the Output Console.
- **If the selected music loops**, you need to **let it play at least once, and then at least another half of the looping part's length**. This is because the conversion script looks for similar parts in the music and then trims the excess, leaving a perfectly looping track. Once the music has played through once, and then at least another half of the looping part, press the **SELECT** button and the converter will dump the data to the Output Console.

In the case of looping music, it is also important to **not** let it loop two full times, as the trimming will fail. Imagine the automatic music trimmer as an algorithm that looks at two loaves of bread: One is full, one has been partially eaten - the algorithm sees the "partially eaten loaf" and goes "hey, I've seen this, this is a part that's identical to the full one, I'll just trim this out". It only needs part of a second loaf of bread to figure out that it looks like the full one. This is what the music trimmer does in our case - it only needs one full playthrough, and then enough of a second one to figure out that it needs to trim it.

When you want to convert again, press Stop, change your arguments, and tap Run again in the Lua script window.

Once the music is captured, the Output Console displays a long list of numbers.
- Select the music data from the Output Console
- Copy it
- Paste it inside music.h under music_dev
- Inside Setup in Pretendo.ino, around line 430, comment selectDemoMusic call
- Uncomment playMusic(music_dev)
- Upload the code
- Done!

# Background

Emulating the NES APU on the Arduino Uno is more or less impossible, if we're talking about actual, proper, register-level emulation of the sound subsystem. Since most of the sound system on the NES (commonly referred to as the 'APU' - *Audio Processing Unit*) relies on timing, and since the APU is clocked at 894KHz, it would've been impossible to emulate on the arduino's wee 16MHz 8-bit CPU (don't quote me on that, I'm pretty sure if LFT gives it a shot, he'd come up with some legendary ASM implementation that leaves 15% of free CPU time, too).
Instead, Pretendo takes an approach *similar* to the VGM format, but eliminating everything that isn't needed - APU registers? Nah. Envelope generators? Nope. Everything is stripped down and reduced to its essential components, leaving the file format itself to just capture the current *useful* state of the sound channels, such as volume, duty cycle, frequency, etc. This approach is similar to what other emulators do to run on low-spec machines, and is known as High-Level Emulation (HLE) - instead of parsing the code/data accurately, exactly like the original system, we parse/run it in a manner that allows the target system to perform the calculations much faster, generally at the expense of some accuracy. The only thing to add is that Pretendo does *not* parse original NSF files, but instead uses its own converter that spits out its own, already-optimized format. In the end, we get something even the Uno can run in realtime.

# Limitations

The current, Arduino Uno-only implementation has the following limitations:

- Sampling rate is capped at 22KHz
- The bit clock of the DMC is halved, effectively lowering its already-quite-low resolution. This is done to ensure a worst case of 1 DMC bit clock per sound engine cycle, in order to keep the engine running in real time on Arduino Uno
- Because of the sampling rate being lower than the LFSR (Noise channel)'s maximum frequency, some of the higher frequencies sound alike

# Possible uses

- Alarm clocks
- Doorbells
- Cool party trick

# To-Do

- Add support for PAL. Currently, all music files are treated as NTSC, affecting tempo and Noise / DPCM pitches
- Port to Arduino Due, ESP32

# Disclaimer

I am not responsible for any sound hardware you break when using this project.
Please do not wire 5v directly into your expensive headset.
