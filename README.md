![Pretendo](img/logo.png?raw=true "Pretendo")

Turn an Arduino Uno (or any ATmega328-based dev board), Arduino Due or ESP32 into an NES APU emulator.

# Demos

Captured via Line In from an Arduino Uno
- [Super Mario Bros. - Underwater](demos/pretendo-demo-smb-underwater.ogg)
- [Dr. Mario - Pre-game screen](demos/pretendo-demo-dr-mario-pre-game.ogg)
- [Contra - Base](demos/pretendo-demo-contra-base.ogg)

# Features

- Full support for all 5 channels (2 x Square, Triangle, Noise, DMC)
- Stereo / Mono sound
- NES DAC transfer function emulation / Linear mixing
- Oneshot / Looping / Looping with Intro support
- PAL / NTSC region support for timings and Noise / DMC tuning
- Logged format with rudimentary compression

# Library dependencies

- [TimerOne](https://github.com/PaulStoffregen/TimerOne)

# Setup

- See wiring diagram below for how to connect headphones to your board
- If you're using Arduino Uno:
    - Download and install [TimerOne](https://github.com/PaulStoffregen/TimerOne) into /Arduino/libraries/
- Go to the top of Pretendo.ino and select one of the available target platforms:
    - Arduino Uno
    - Arduino Due
    - ESP32
- Upload Sketch
- Open Serial Monitor
- Instructions appear inside Serial Monitor and music plays
- Enjoy!
- *See 'Advanced Use' below for how to convert your own files*

# Wiring Diagram

Use 220Ω resistors.

On Arduino Uno, this program uses PWM to simulate a DAC.

Alternative configurations:
* If you're using an Arduino Due, connect the White wire to DAC1, Red wire to DAC2
* If you're using an ESP32 board, connect the White wire to DAC1, Red wire to DAC2

![Wiring diagram](img/wiring.png?raw=true "Wiring diagram")

# Advanced Use

Converting your own files is Easy™

It is done via a Lua script and the FCEUX NES emulator
- Download [FCEUX](http://fceux.com/web/download.html)
- Open FCEUX, load up your favourite ROM in the form of an NSF
- Make sure you have the NES controller SELECT button bound in Settings
- File > Lua > New Lua Script Window > Browse... *fceux-apu-capture.lua*
- You're now ready to convert your own music

### Recording NES music via FCEUX

Using the Lua script, we can capture music data for Pretendo to play.
- After selecting a ROM, you are free to nagivate inside FCEUX and find the track you wish to record.
- Once you have found a track, write down its number inside the *Arguments* field in the Lua script window in FCEUX.
    - If the track should not loop, also write down *--oneshot* after the track number.
- After entering the track number and the optional *--oneshot* argument, press Run in the Lua script window. The capture process will start, and the emulator will start playing back the music.
    - **If the selected music does not loop** (i.e. it stops when it ends), press the **SELECT** key when you wish to end the capture, and the data will be written into the Output Console.
    - **If the selected music loops**, you need to **let it play at least once, and then at least another half of the looping part's length**. This is because the conversion script looks for similar parts in the music and then trims the excess, leaving a perfectly looping track. Once the music has played through once, and then at least another half of the looping part, press the **SELECT** button and the converter will dump the data to the Output Console.

In the case of looping music, it is also important to **not** let it loop two full times, as the trimming will fail to properly discard all the redundant data.

To better understand how the automatic trimmer works, have a look at the following diagram:
![Cut example](img/cut-example.png?raw=true "Cut example")

Cut at the time indicated by the red arrow.

The automatic trimmer looks for similarities in looping music, cuts off the redundancy and sets the loop point. The converter will only cut looping music once; that means that if the music loops for two full runs of the main melody and you cut when it's already playing the main melody the third time, it will only cut off the third partial playthrough, while keeping the other two playthroughs. It *will* work, but you will end up with the main melody captured twice.

In order to capture the music data without any redundancy, you only need to let it loop for a full playthrough of the main melody, and then cut it off far enough in the second playthrough, but before it actually gets to finish playing that second playthrough.

When you want to convert again, press Stop, change your arguments, and tap Run again in the Lua script window.

Once the music is captured, the Output Console displays a long list of numbers.
- Select the music data from the Output Console
- Copy it
- Paste it inside music.h under music_dev
- Uncomment line 117 in the sketch (demoMusic = music_dev;)
- Upload the code
- Select your music in the Serial Monitor
- Done!

# Caveats
The converter has no idea what region it should set the playback engine to. I've tried fiddling with the ROM metadata but nothing seemed to yield reliable results.

Inside the dumped music data, you will find a commented line at the very beginning. You can change the region flag accordingly in that line.

# Background

Emulating the NES APU on the Arduino Uno is more or less impossible, if we're talking about actual, proper, register-level emulation of the sound subsystem. Since most of the sound system on the NES (commonly referred to as the 'APU' - *Audio Processing Unit*) relies on timing, and since the APU is clocked at 894KHz, it would've been impossible to emulate on the arduino's wee 16MHz 8-bit CPU (don't quote me on that, I'm pretty sure if LFT gives it a shot, he'd come up with some legendary ASM implementation that leaves 15% of free CPU time, too).
Instead, Pretendo takes an approach *similar* to the VGM format, but eliminating everything that isn't needed - APU registers? Nah. Envelope generators? Nope. Everything is stripped down and reduced to its essential components, leaving the file format itself to just capture the current *useful* state of the sound channels, such as volume, duty cycle, frequency, etc. This approach is similar to what other emulators do to run on low-spec machines, and is known as High-Level Emulation (HLE) - instead of parsing the code/data accurately, exactly like the original system, we parse/run it in a manner that allows the target system to perform the calculations much faster, generally at the expense of some accuracy. The only thing to add is that Pretendo does *not* parse original NSF files, but instead uses its own converter that spits out its own, already-optimized format. In the end, we get something even the Uno can run in realtime.

# Limitations

- The following apply only to Arduino Uno:
    - Sampling rate is capped at 20KHz
    - The bit clock of the DMC is halved, effectively lowering its already-quite-low resolution. This is done to ensure a worst case of 1 DMC bit clock per sound engine cycle, in order to keep the engine running in real time
    - Because of the sampling rate being lower than the LFSR (Noise channel)'s maximum frequency, some of the higher frequencies sound alike

# Possible uses

- NES development / DSP research
- Alarm clocks
- Doorbells
- Cool party trick


# Disclaimer

I am not responsible for any sound hardware you break when using this project.

Please do not wire 5v directly into your expensive headset.
