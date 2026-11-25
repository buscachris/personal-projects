package com.metalvoice.phone;

import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Process;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.concurrent.atomic.AtomicBoolean;

final class AudioEngine {
    interface Monitor {
        void level(float level);
        void route(String description);
    }

    private volatile AudioRecord activeRecord;
    private volatile AudioTrack activeTrack;

    void stop() {
        AudioRecord r = activeRecord;
        if (r != null) try { r.stop(); } catch (IllegalStateException ignored) {}
        AudioTrack t = activeTrack;
        if (t != null) try { t.pause(); t.flush(); } catch (IllegalStateException ignored) {}
    }

    private AudioRecord recorder(int rate, AudioDeviceInfo input) throws IOException {
        int min = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        if (min < 0) throw new IOException("This microphone does not support " + rate + " Hz recording.");
        AudioRecord r = new AudioRecord.Builder()
                .setAudioSource(MediaRecorder.AudioSource.VOICE_RECOGNITION)
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(rate)
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_IN_MONO).build())
                .setBufferSizeInBytes(Math.max(min, rate / 10 * 2)).build();
        if (r.getState() != AudioRecord.STATE_INITIALIZED) { r.release(); throw new IOException("Microphone could not start."); }
        if (input != null && !r.setPreferredDevice(input)) { r.release(); throw new IOException("Selected microphone is unavailable."); }
        activeRecord = r;
        return r;
    }

    private AudioTrack player(int rate, AudioDeviceInfo output, boolean live) throws IOException {
        int min = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        if (min < 0) throw new IOException("This output does not support " + rate + " Hz playback.");
        AudioTrack t = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(rate)
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setTransferMode(AudioTrack.MODE_STREAM).setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                .setBufferSizeInBytes(Math.max(min, live ? rate / 50 * 2 : rate / 5 * 2)).build();
        if (t.getState() != AudioTrack.STATE_INITIALIZED) { t.release(); throw new IOException("Audio output could not start."); }
        if (output != null && !t.setPreferredDevice(output)) { t.release(); throw new IOException("Selected output is unavailable."); }
        t.setVolume(live ? 0.65f : 0.9f);
        activeTrack = t;
        return t;
    }

    byte[] record(AtomicBoolean running, AudioDeviceInfo input, Monitor monitor) throws Exception {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
        AudioRecord r = recorder(16000, input);
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            short[] frame = new short[320];
            r.startRecording();
            if (r.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) throw new IOException("Microphone access was blocked.");
            monitor.route("Mic: " + name(r.getRoutedDevice()));
            int counter = 0;
            while (running.get() && bytes.size() < 16000 * 2 * 15) {
                int n = r.read(frame, 0, frame.length, AudioRecord.READ_BLOCKING);
                if (n < 0) { if (!running.get()) break; throw new IOException("Microphone disconnected (" + n + ")."); }
                for (int i = 0; i < n; i++) { bytes.write(frame[i] & 255); bytes.write((frame[i] >> 8) & 255); }
                if (++counter % 5 == 0) monitor.level(level(frame, n));
            }
            return bytes.toByteArray();
        } finally {
            activeRecord = null;
            try { r.stop(); } catch (IllegalStateException ignored) {}
            r.release();
        }
    }

    void play(byte[] pcm, AtomicBoolean running, AudioDeviceInfo output, float texture, Monitor monitor) throws Exception {
        AudioTrack t = player(24000, output, false);
        try {
            t.play();
            RobotEffect effect = new RobotEffect(24000);
            short[] frame = new short[960];
            int offset = 0;
            while (offset < pcm.length && running.get()) {
                int n = Math.min(frame.length, (pcm.length - offset) / 2);
                for (int i = 0; i < n; i++) frame[i] = (short) ((pcm[offset++] & 255) | (pcm[offset++] << 8));
                if (texture > 0) effect.process(frame, n, texture);
                write(t, frame, n, running);
                monitor.level(level(frame, n));
                if (offset == n * 2) monitor.route("Output: " + name(t.getRoutedDevice()));
            }
            // A streaming write returns before the final buffer is audible. Drain before releasing it.
            long frames = offset / 2;
            long deadline = System.nanoTime() + 3_000_000_000L;
            while (running.get() && Integer.toUnsignedLong(t.getPlaybackHeadPosition()) < frames && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }
        } finally {
            activeTrack = null;
            try { t.stop(); } catch (IllegalStateException ignored) {}
            t.release();
        }
    }

    void live(AtomicBoolean running, AudioDeviceInfo input, AudioDeviceInfo output, Monitor monitor) throws Exception {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
        AudioRecord r = recorder(48000, input);
        AudioTrack t = null;
        try {
            t = player(48000, output, true);
            RobotEffect effect = new RobotEffect(48000);
            short[] frame = new short[480];
            r.startRecording(); t.play();
            if (r.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) throw new IOException("Microphone access was blocked.");
            int counter = 0;
            while (running.get()) {
                int n = r.read(frame, 0, frame.length, AudioRecord.READ_BLOCKING);
                if (n < 0) { if (!running.get()) break; throw new IOException("Live microphone disconnected."); }
                if (++counter % 10 == 0) {
                    monitor.level(level(frame, n));
                    AudioDeviceInfo route = t.getRoutedDevice();
                    if (route != null && !external(route)) throw new IOException("Live output changed to the phone speaker. Stopped to prevent feedback.");
                    monitor.route("Mic: " + name(r.getRoutedDevice()) + "  •  Output: " + name(route));
                }
                effect.process(frame, n, 0.95f);
                write(t, frame, n, running);
            }
        } finally {
            activeRecord = null; activeTrack = null;
            try { r.stop(); } catch (IllegalStateException ignored) {}
            r.release();
            if (t != null) { try { t.stop(); } catch (IllegalStateException ignored) {} t.release(); }
        }
    }

    private void write(AudioTrack t, short[] frame, int n, AtomicBoolean running) throws IOException {
        int offset = 0;
        while (offset < n && running.get()) {
            int written = t.write(frame, offset, n - offset, AudioTrack.WRITE_BLOCKING);
            if (written < 0) { if (!running.get()) return; throw new IOException("Audio output disconnected."); }
            offset += written;
        }
    }

    static boolean external(AudioDeviceInfo d) {
        if (d == null) return false;
        int type = d.getType();
        return type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES || type == AudioDeviceInfo.TYPE_WIRED_HEADSET
                || type == AudioDeviceInfo.TYPE_USB_DEVICE || type == AudioDeviceInfo.TYPE_USB_HEADSET
                || type == AudioDeviceInfo.TYPE_USB_ACCESSORY || type == AudioDeviceInfo.TYPE_LINE_ANALOG
                || type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP || type == AudioDeviceInfo.TYPE_BLE_HEADSET;
    }

    static String name(AudioDeviceInfo d) {
        if (d == null) return "system default";
        String type;
        switch (d.getType()) {
            case AudioDeviceInfo.TYPE_BUILTIN_MIC: type = "Phone microphone"; break;
            case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER: type = "Phone speaker"; break;
            case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE: type = "Earpiece"; break;
            case AudioDeviceInfo.TYPE_WIRED_HEADSET: type = "Wired headset"; break;
            case AudioDeviceInfo.TYPE_WIRED_HEADPHONES: type = "Headphones"; break;
            case AudioDeviceInfo.TYPE_USB_DEVICE: case AudioDeviceInfo.TYPE_USB_HEADSET: case AudioDeviceInfo.TYPE_USB_ACCESSORY: type = "USB audio"; break;
            case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP: case AudioDeviceInfo.TYPE_BLUETOOTH_SCO: case AudioDeviceInfo.TYPE_BLE_HEADSET: type = "Bluetooth audio"; break;
            default: type = "Audio device";
        }
        return type + " · " + d.getProductName();
    }

    private static float level(short[] frame, int count) {
        if (count == 0) return 0;
        double sum = 0;
        for (int i = 0; i < count; i++) { double x = frame[i] / 32768.0; sum += x * x; }
        return Math.min(1f, (float) Math.sqrt(sum / count) * 4f);
    }
}
