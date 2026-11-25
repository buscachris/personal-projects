# Metal Voice — Android phone prototype

I built this Android app so I can record a phrase on my phone and play it back in a Metal Sonic voice alongside my standalone Quest and VRChat setup. I wrote it in Java against API 35. It requires Android 8 or newer.

## Use

1. Open **Metal Voice** on the phone. Tap **Test voice** to hear the configured Fish voice.
2. Tap **Record phrase**, allow the microphone, say a short sentence, then tap **Send phrase**. Recording automatically ends at 15 seconds. Fish transcribes it and synthesizes the text in the saved Metal Sonic voice. The recognized words appear in the text box.
3. Edit the text and tap **Speak text** to correct recognition or type a line. **Replay last voice** makes no new API request.
4. **Extra metallic texture** adds a local electronic effect to Fish playback. It is off by default to preserve the reference voice.
5. **Start live robot** applies a local robot effect to your own voice with no API calls. Select headphones or an audio interface under **Output** first. This effect is separate from the Fish voice model.
6. **Stop audio** cancels recording, synthesis, or playback. Keep the app in the foreground; leaving the app stops audio. Phone calls and other audio interruptions stop the session.

Fish mode sends recorded speech to `https://api.fish.audio/v1/asr` (`transcribe-1-pro`), then text to `/v1/tts` (`s1` by default, matching the existing Metal Sonic screen reader). Each returned phrase is 24 kHz mono signed 16-bit little-endian PCM. ASR and TTS have network delay and may consume Fish account credit. Fish model, voice ID, and API key can be changed in **Fish settings**.

Microphone recordings stay in memory and are discarded after the request. The last synthesized phrase stays in memory for replay. The key is stored in the app's private internal storage and excluded from Android backups; it is not compiled into the APK or printed in logs. This is a locally signed, debuggable development build; do not distribute a configured phone backup or treat this APK as a production security boundary.

## Quest / VRChat boundary

This app produces audio **on the phone**. It cannot replace another app's microphone on an unmodified Quest. Routing into standalone VRChat needs a compatible external microphone path into the Quest. A normal USB charging cable between the phone and Quest does not do that. An audio interface / suitable mic-level connection still needs physical testing. No claim of working VRChat microphone injection or completed Quest integration is made by this prototype.

For now, Test voice plays through the selected phone output. Live mode refuses phone-speaker output to reduce feedback; wired headphones or an interface are preferable to Bluetooth for delay. The route readout shows the device Android actually uses when audio starts. The built-in phone microphone is selected initially so an output adapter's microphone does not silently become the recording source.

## Build / install

With JDK 17, SDK platform 35, build-tools 35.0.0, `rg`, `zip`, and the usual Android debug keystore:

```sh
bash build.sh
adb install -r "$(cat latest-apk.txt)"
```

The build uses local `aapt`, `javac`, `d8`, `zipalign`, and `apksigner`; no Gradle or dependency download is needed. APKs and hashes are placed in timestamped `builds/` directories. `latest-apk.txt` points to the latest one. Enter your own Fish Audio API key and voice reference ID in **Fish settings** before testing cloud speech.
