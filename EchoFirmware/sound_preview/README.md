# ECHO firmware sound preview (offline)

Renders the same DSP chain as `EchoFirmware/AudioSynth.cpp` into WAV files so you can listen on a PC without flashing an ESP32.

## Requirements

- Python 3
- `numpy` (`python3 -m pip install numpy`)

This script uses only the Python standard library plus `numpy`, and writes WAV with the `wave` module.

## Run

```bash
cd sound_preview
python3 preview.py
```

Then open the generated WAVs in any audio player:

- `output_bounce_close_053.wav` — personality `BOUNCE`, closeness `0.53`, 20 s  
- `output_shy_close_053.wav` — `SHY`, closeness `0.53`, 20 s  
- `output_messy_close_053.wav` — `MESSY`, closeness `0.53`, 20 s  
- `output.wav` — same render as the bounce file (quick default listen)

## Notes

- Constants in `preview.py` follow the preview spec (`SAMPLE_RATE`, `BUFFER_SIZE`, `DELAY_SIZE`, `AUDIO_GAIN`). If your flashed firmware uses different values (for example `DELAY_SIZE` in `EchoFirmware/Config.h`), change the matching constants at the top of `preview.py` so the WAV matches the device.
- Triggers are fired once per emulated `BUFFER_SIZE` block at `interval_ms = 300 - closeness * 220` (with `closeness = 0.53`), while `triggerPersonality` still runs `scheduleNextGridMs` internally like the firmware.

## Web prototype

Tone.js experiments live in `../web_prototype/` (sibling folder). This repository may not include HTML there yet; add your own test pages as needed.
