#!/usr/bin/env bash
set -euo pipefail
project="$(cd -- "$(dirname -- "$0")" && pwd)"
sdk="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
bt="$sdk/build-tools/35.0.0"
platform="$sdk/platforms/android-35/android.jar"
build="$project/builds/$(date +%Y-%m-%d_%H-%M-%S)_phone-v01"
mkdir -p "$build"/{gen,classes,dex}
"$bt/aapt" package -f -M "$project/app/AndroidManifest.xml" -I "$platform" -S "$project/app/res" -J "$build/gen" -F "$build/resources.apk"
mapfile -t sources < <(rg --files "$project/app/src" "$build/gen" -g '*.java' | sort)
javac -encoding UTF-8 --release 8 -classpath "$platform" -d "$build/classes" "${sources[@]}"
mapfile -t classes < <(rg --files "$build/classes" -g '*.class' | sort)
"$bt/d8" --min-api 26 --lib "$platform" --output "$build/dex" "${classes[@]}"
cp "$build/resources.apk" "$build/unsigned.apk"
zip -q -j "$build/unsigned.apk" "$build/dex/classes.dex"
"$bt/zipalign" -f 4 "$build/unsigned.apk" "$build/aligned.apk"
"$bt/apksigner" sign --ks "$HOME/.android/debug.keystore" --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out "$build/MetalVoice-v0.1.apk" "$build/aligned.apk"
"$bt/apksigner" verify "$build/MetalVoice-v0.1.apk"
sha256sum "$build/MetalVoice-v0.1.apk" | tee "$build/SHA256SUMS"
printf '%s\n' "$build/MetalVoice-v0.1.apk" > "$project/latest-apk.txt"
printf 'APK: %s\n' "$build/MetalVoice-v0.1.apk"
