# ESP32-P4 Accessibility: Speech & Audio Capabilities

Research notes on using the ESP32-P4 LCD 4.3 board's audio hardware for
accessibility features (screen reading, voice navigation).

## Hardware: Waveshare ESP32-P4 WiFi6 Touch LCD 4.3

The board has a full audio subsystem:

| Chip | Role |
|---|---|
| **ES7210** | 4-channel 24-bit ADC, drives 2 onboard MEMS mics. 102 dB SNR, up to 96 kHz. |
| **ES8311** | Mono codec (ADC + DAC), secondary mic path + speaker output. |
| **NS4150B** | Class-D power amplifier for the onboard speaker. |

The dual-mic array supports acoustic echo cancellation (AEC) between mics and
speaker. Everything connects to the ESP32-P4 over I2S + I2C.

**Current codebase state:** `board_audio.c` only initializes the ES8311. The
ES7210 dual-mic array has no driver code yet.

---

## Speech-to-Text (STT): Limited to Fixed Commands

### On-Device: Espressif ESP-SR (proven, official P4 support)

ESP-SR provides wake word detection + speech command recognition:

| Component | What it Does | PSRAM Usage |
|---|---|---|
| **WakeNet9** | Wake word detection (50+ options including custom) | ~325 KB |
| **MultiNet7** | Speech command recognition, up to **200 commands** | ~2.9 MB |
| **AFE** | Full pipeline: AEC + noise suppression + VAD | ~1.2 MB (dual-mic) |

Total: ~4 MB PSRAM out of 32 MB available. Runs real-time on the dual-core
400 MHz RISC-V.

**Critical limitation:** MultiNet is **command recognition only**. It matches
audio against a predefined set of up to 200 phrases. It cannot transcribe
arbitrary speech.

- Accuracy: 97% in quiet, ~90% in noise
- Languages: English, Chinese

### On-Device Free-Form STT: Not Feasible

Whisper tiny alone needs ~75 MB model weights + ~1 GB working RAM. Even the
smallest viable ASR models far exceed the 32 MB PSRAM. Sherpa-ONNX and TFLite
Micro don't change this picture.

### BIP39 Seed Word Recognition: Not Practical

The BIP39 wordlist has 2048 words, well over MultiNet7's 200-command limit.
Even if the vocabulary could be stretched, homophones and similar-sounding
words in the BIP39 list (e.g., "aisle"/"isle", "bear"/"bare",
"ceiling"/"sealing") make reliable recognition dangerous -- misrecognition of a
single seed word is catastrophic.

### Cloud STT: Proven but Inappropriate for SeedSigner

Streaming audio to Google Cloud Speech, OpenAI Whisper API, Deepgram, etc. is
well-proven on ESP32 platforms. However, sending seed phrase audio to a cloud
service is a non-starter for SeedSigner's air-gapped threat model.

### STT Bottom Line

Voice input is viable **only for UI navigation commands** where the vocabulary
is small (~50-100 phrases) and errors are low-stakes (just try again). Seed
phrase entry must stay tactile.

---

## Text-to-Speech (TTS): Viable for Screen Reading

### On-Device Options

| Engine | Languages | Quality | Flash | RAM | Notes |
|---|---|---|---|---|---|
| **PicoTTS** | EN, DE, FR, IT, ES | Moderate | ~1.5 MB | 1.1 MB PSRAM | Best option. ESP-IDF component. |
| **Espressif ESP-TTS** | Chinese only | Robotic | 2.2 MB | 20 KB | No English support. |
| **eSpeak-NG** | 100+ languages | Robotic but clear | ~1.6 MB | ~121 KB | Arduino library, needs porting. |
| **SAM** | English | 1982 retro | 39 KB | Tiny | Novelty only. |

Neural TTS models (Piper, VITS) need tens of MB plus an ONNX runtime that
doesn't exist for ESP-IDF. Not feasible on-device.

### PicoTTS: The Practical Choice

Based on SVOX Pico (the old stock Android TTS). Clearly synthetic but
perfectly intelligible. Resource cost is trivial on this board. Real-time with
no perceptible delay. Fully offline -- consistent with SeedSigner's air-gapped
philosophy.

- ESP-IDF component: https://github.com/DiUS/esp-picotts
- Also on ESP Component Registry (`jmattsson/picotts` v1.1.3, "Supports all targets")
- Output: 16-bit mono @ 16 kHz

### PicoTTS Pronunciation Control

PicoTTS supports inline markup for controlling pronunciation of technical terms
and acronyms:

**`<spell>` tag -- for acronyms:**
```
<spell>PSBT</spell>              <!-- spells out "P S B T" -->
<spell mode="200">UTXO</spell>   <!-- 200ms pause between letters -->
```

**`<phoneme>` tag -- full phonetic control via X-SAMPA:**
```
<phoneme ph="ju: ti: Eks o:z">UTXOs</phoneme>
```
Falls back to normal pronunciation if the phoneme string fails to parse.

**Plain text respelling -- simplest approach:**
```
"PSBT"  -> "P. S. B. T."
"UTXO"  -> "you tex oh"
"BIP39" -> "bip thirty nine"
"xpub"  -> "ex pub"
```

**What does NOT work:**
- **User lexicons / custom dictionaries** -- The engine architecturally
  supports them, but a copy-paste bug in upstream SVOX Pico source makes
  lexicon loading impossible, and no public tooling exists to generate the
  binary dictionary format.
- **SSML `<say-as>`** -- Not recognized by the Pico engine.

**Practical pattern:** A lookup table mapping domain terms to marked-up
pronunciation, applied before passing text to PicoTTS:

```c
{"PSBT",  "<spell>PSBT</spell>"},
{"UTXO",  "<phoneme ph=\"ju: tE ks o:\">UTXO</phoneme>"},
{"xpub",  "ex pub"},
{"BIP39", "bip thirty nine"},
```

The vocabulary of Bitcoin/SeedSigner terms needing special handling is small
and well-defined.

### Cloud TTS (for reference)

Not applicable to SeedSigner's air-gapped use case, but documented for
completeness: OpenAI TTS, Edge TTS (Microsoft), Amazon Polly, and self-hosted
Piper are all proven on ESP32 platforms with near-human quality.

---

## Recommended Architecture for SeedSigner Accessibility

1. **TTS screen reader (PicoTTS):** Read screen content, confirm selections,
   announce navigation state. Fully offline, instant response, ~2.6 MB total
   resource cost (flash + PSRAM).

2. **Voice commands (ESP-SR MultiNet7, optional):** Small vocabulary of UI
   navigation commands ("confirm", "go back", "next", "scroll down"). ~4 MB
   PSRAM for the full AFE + WakeNet + MultiNet pipeline. Requires ES7210
   dual-mic driver work.

3. **Pronunciation dictionary:** Maintained lookup table for
   Bitcoin/SeedSigner domain terms, applied as a text preprocessing step
   before TTS.

---

*Research conducted 2026-03-30. Based on ESP-SR docs, SVOX Pico manual,
esp-picotts source, and ESP-IDF component registry.*
