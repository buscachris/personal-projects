package com.metalvoice.phone;

import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import javax.net.ssl.HttpsURLConnection;

final class FishClient {
    private volatile HttpsURLConnection active;

    void cancel() {
        HttpsURLConnection c = active;
        if (c != null) c.disconnect();
    }

    private HttpsURLConnection connect(String endpoint, String key, String type, String model) throws IOException {
        HttpsURLConnection c = (HttpsURLConnection) new URL("https://api.fish.audio/v1/" + endpoint).openConnection();
        active = c;
        c.setInstanceFollowRedirects(false);
        c.setConnectTimeout(15000);
        c.setReadTimeout(90000);
        c.setRequestMethod("POST");
        c.setDoOutput(true);
        c.setRequestProperty("Authorization", "Bearer " + key);
        c.setRequestProperty("Content-Type", type);
        c.setRequestProperty("model", model);
        return c;
    }

    String transcribe(String key, byte[] pcm) throws Exception {
        String boundary = "MetalVoiceBoundary7b952";
        HttpsURLConnection c = connect("asr", key, "multipart/form-data; boundary=" + boundary, "transcribe-1-pro");
        byte[] head = ("--" + boundary + "\r\nContent-Disposition: form-data; name=\"ignore_timestamps\"\r\n\r\ntrue\r\n"
                + "--" + boundary + "\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n").getBytes(StandardCharsets.UTF_8);
        byte[] wav = Wav.wrap(pcm, 16000);
        byte[] tail = ("\r\n--" + boundary + "--\r\n").getBytes(StandardCharsets.UTF_8);
        c.setFixedLengthStreamingMode(head.length + wav.length + tail.length);
        try {
            try (OutputStream out = c.getOutputStream()) {
                out.write(head); out.write(wav); out.write(tail);
            }
            String text = new JSONObject(new String(response(c, 1024 * 1024), StandardCharsets.UTF_8)).optString("text", "").trim();
            if (text.isEmpty()) throw new IOException("No speech recognized. Try a shorter, clearer phrase.");
            return text;
        } finally { c.disconnect(); active = null; }
    }

    byte[] synthesize(String key, String voice, String model, String text) throws Exception {
        byte[] body = new JSONObject().put("text", text).put("reference_id", voice)
                .put("format", "pcm").put("sample_rate", 24000).put("latency", "balanced")
                .put("chunk_length", 100).toString().getBytes(StandardCharsets.UTF_8);
        HttpsURLConnection c = connect("tts", key, "application/json", model);
        c.setFixedLengthStreamingMode(body.length);
        try {
            try (OutputStream out = c.getOutputStream()) { out.write(body); }
            byte[] audio = response(c, 24000 * 2 * 90);
            if (audio.length < 480 || (audio.length & 1) != 0) throw new IOException("Fish returned invalid PCM audio.");
            String type = c.getContentType();
            if (type != null && type.contains("json")) throw new IOException("Fish returned a message instead of audio.");
            return audio;
        } finally { c.disconnect(); active = null; }
    }

    private byte[] response(HttpsURLConnection c, int limit) throws IOException {
        int code = c.getResponseCode();
        if (code != 200) {
            String message;
            switch (code) {
                case 401: message = "Fish rejected the API key. Open Settings to update it."; break;
                case 402: message = "Fish Audio needs account credit for this request."; break;
                case 403: message = "Fish denied access to this voice or model."; break;
                case 429: message = "Fish is rate limited. Wait a moment and try again."; break;
                case 400: case 422: message = "Fish rejected the request. Check the voice ID and model in Settings."; break;
                case 503: message = "Fish is busy. Try again shortly."; break;
                default: message = "Fish request failed (HTTP " + code + ").";
            }
            throw new IOException(message);
        }
        try (InputStream in = c.getInputStream(); ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int n;
            while ((n = in.read(buffer)) != -1) {
                if (bytes.size() + n > limit) throw new IOException("Audio response is too long. Use a shorter phrase.");
                bytes.write(buffer, 0, n);
            }
            return bytes.toByteArray();
        }
    }
}
