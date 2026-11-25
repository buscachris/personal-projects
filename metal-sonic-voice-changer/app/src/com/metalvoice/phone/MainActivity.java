package com.metalvoice.phone;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.media.AudioAttributes;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputFilter;
import android.text.InputType;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public final class MainActivity extends Activity {
    private static final String TAG = "MetalVoice";
    private static final int BG = 0xff080e1c, CARD = 0xff111d33, CYAN = 0xff68dfff, WHITE = 0xfff0f5ff, MUTED = 0xffa7b9d2;
    private static final String DEFAULT_VOICE = "";
    private static final String SAMPLE = "Metal Sonic online. Voice system ready.";
    private enum State { IDLE, RECORDING, NETWORK, PLAYING, LIVE, STOPPING }
    private State state = State.IDLE;
    private volatile long generation;
    private AtomicBoolean running = new AtomicBoolean(false);
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final AudioEngine audio = new AudioEngine();
    private final FishClient fish = new FishClient();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private AudioManager manager;
    private AudioFocusRequest focus;
    private TextView status, detail, keyStatus, routeStatus;
    private ProgressBar meter;
    private EditText phrase;
    private Spinner inputs, outputs;
    private Switch texture;
    private Button record, speak, test, replay, live, stop, settings;
    private final List<AudioDeviceInfo> inputDevices = new ArrayList<>(), outputDevices = new ArrayList<>();
    private volatile byte[] lastAudio;
    private long recordingSince;
    private long lastMeter;
    private boolean destroyed;
    private final AudioDeviceCallback deviceCallback = new AudioDeviceCallback() {
        @Override public void onAudioDevicesAdded(AudioDeviceInfo[] devices) { refreshDevices(); }
        @Override public void onAudioDevicesRemoved(AudioDeviceInfo[] devices) {
            if (state == State.LIVE || state == State.RECORDING || state == State.PLAYING) cancel("Audio device disconnected. Choose the route again.");
            refreshDevices();
        }
    };

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        manager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        focus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setOnAudioFocusChangeListener(change -> {
                    if (change == AudioManager.AUDIOFOCUS_LOSS || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) cancel("Audio paused by another app.");
                }).build();
        buildUi();
        manager.registerAudioDeviceCallback(deviceCallback, handler);
        refreshDevices();
        refreshKeyStatus();
        if (saved != null) phrase.setText(saved.getString("phrase", SAMPLE));
    }

    private void buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true); scroll.setBackgroundColor(BG);
        LinearLayout page = new LinearLayout(this); page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(22), dp(18), dp(22), dp(24)); scroll.addView(page);
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets.consumeSystemWindowInsets();
        });
        TextView eyebrow = label("METAL SONIC  /  VOICE LAB", 12, CYAN); eyebrow.setLetterSpacing(0.15f); page.addView(eyebrow);
        TextView title = label("Metal Voice", 34, WHITE); title.setTypeface(null, Typeface.BOLD); page.addView(title);
        addText(page, "Your voice in. Metal Sonic out.", 15, MUTED, 2);

        LinearLayout statusCard = card(page, 20);
        status = label("READY", 18, CYAN); status.setTypeface(null, Typeface.BOLD); statusCard.addView(status);
        detail = label("Try the voice, then record a short phrase.", 14, WHITE); add(statusCard, detail, 7);
        meter = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal); meter.setMax(1000);
        meter.setProgressTintList(android.content.res.ColorStateList.valueOf(CYAN));
        LinearLayout.LayoutParams meterParams = new LinearLayout.LayoutParams(-1, dp(5)); meterParams.topMargin = dp(14); statusCard.addView(meter, meterParams);
        keyStatus = label("", 12, MUTED); add(statusCard, keyStatus, 12);

        LinearLayout fishCard = card(page, 16);
        addText(fishCard, "01  FISH · METAL SONIC", 13, CYAN, 0);
        addText(fishCard, "Tap to record, then tap again to send. Your phrase is transcribed and spoken in the saved voice. Internet required; expect a delay.", 14, MUTED, 8);
        record = button("Record phrase", 0xff1955e5, WHITE); add(fishCard, record, 14); record.setOnClickListener(v -> recordPressed());
        phrase = new EditText(this); phrase.setTextColor(WHITE); phrase.setHintTextColor(MUTED);
        phrase.setText(SAMPLE); phrase.setTextSize(16); phrase.setMinLines(2); phrase.setMaxLines(5);
        phrase.setGravity(Gravity.TOP); phrase.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        phrase.setFilters(new InputFilter[]{new InputFilter.LengthFilter(500)}); phrase.setContentDescription("Phrase to speak");
        add(fishCard, phrase, 10);
        LinearLayout row = new LinearLayout(this);
        speak = button("Speak text", 0xff263b5c, WHITE); test = button("Test voice", 0xff263b5c, WHITE);
        row.addView(speak, new LinearLayout.LayoutParams(0, dp(50), 1));
        LinearLayout.LayoutParams half = new LinearLayout.LayoutParams(0, dp(50), 1); half.leftMargin = dp(8); row.addView(test, half); add(fishCard, row, 8);
        speak.setOnClickListener(v -> synthesizeText(phrase.getText().toString())); test.setOnClickListener(v -> synthesizeText(SAMPLE));
        texture = new Switch(this); texture.setText("Extra metallic texture"); texture.setTextColor(WHITE); texture.setTextSize(14); texture.setChecked(false); add(fishCard, texture, 12);
        replay = button("Replay last voice", 0xff263b5c, WHITE); add(fishCard, replay, 10); replay.setOnClickListener(v -> replayLast());

        LinearLayout liveCard = card(page, 16);
        addText(liveCard, "02  LIVE · ROBOT EFFECT", 13, CYAN, 0);
        addText(liveCard, "Immediate electronic effect on your own voice. Works offline. Connect headphones or an audio interface to avoid speaker feedback.", 14, MUTED, 8);
        live = button("Start live robot", 0xff263b5c, WHITE); add(liveCard, live, 12); live.setOnClickListener(v -> startLive());

        LinearLayout routes = card(page, 16);
        addText(routes, "AUDIO ROUTING", 13, CYAN, 0);
        addText(routes, "Microphone", 13, MUTED, 12); inputs = new Spinner(this); routes.addView(inputs, new LinearLayout.LayoutParams(-1, dp(48)));
        addText(routes, "Output", 13, MUTED, 8); outputs = new Spinner(this); routes.addView(outputs, new LinearLayout.LayoutParams(-1, dp(48)));
        routeStatus = label("Select an output to test the connection.", 12, MUTED); add(routes, routeStatus, 8);
        addText(routes, "Quest / VRChat: this app plays audio from the phone. A compatible external microphone connection to the Quest is still required. A charging cable alone does not create a virtual microphone.", 13, MUTED, 14);
        addText(routes, "Keep this app open during use. It stops recording and playback when you leave.", 13, MUTED, 9);

        stop = button("Stop audio", 0xff852d40, WHITE); add(page, stop, 18); stop.setOnClickListener(v -> cancel("Stopped."));
        settings = button("Fish settings", 0xff1b2940, MUTED); add(page, settings, 8); settings.setOnClickListener(v -> showSettings());
        addText(page, "v0.1 · Phone prototype", 12, MUTED, 18);
        setContentView(scroll); updateControls();
    }

    private void recordPressed() {
        if (state == State.RECORDING) {
            running.set(false); setState(State.NETWORK, "TRANSCRIBING", "Sending the phrase to Fish Audio…");
            return;
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, 7); return;
        }
        String key = requireKey(); if (key == null) return;
        AudioDeviceInfo input = selected(inputDevices, inputs), output = selected(outputDevices, outputs);
        String voice = voice(), model = model(); float amount = texture.isChecked() ? 0.7f : 0;
        recordingSince = android.os.SystemClock.elapsedRealtime();
        begin(State.RECORDING, "LISTENING", "Speak now. Tap again to send · 15 second limit.", (token, flag) -> {
            byte[] pcm = audio.record(flag, input, monitor(token));
            if (token != generation) return;
            if (pcm.length < 9600) throw new Exception("That was too short. Record at least half a second.");
            flag.set(true);
            ui(token, () -> setState(State.NETWORK, "TRANSCRIBING", "Sending the phrase to Fish Audio…"));
            String text = fish.transcribe(key, pcm);
            if (token != generation) return;
            ui(token, () -> phrase.setText(text));
            synthesizeAndPlay(token, flag, key, voice, model, text, output, amount);
        });
    }

    private void synthesizeText(String text) {
        text = text.trim();
        if (text.isEmpty()) { message("Enter a phrase first."); return; }
        String key = requireKey(); if (key == null) return;
        String voice = voice(), model = model(), spoken = text;
        AudioDeviceInfo output = selected(outputDevices, outputs); float amount = texture.isChecked() ? 0.7f : 0;
        begin(State.NETWORK, "GENERATING", "Asking Fish for your Metal Sonic voice…", (token, flag) ->
                synthesizeAndPlay(token, flag, key, voice, model, spoken, output, amount));
    }

    private void synthesizeAndPlay(long token, AtomicBoolean flag, String key, String voice, String model, String text, AudioDeviceInfo output, float amount) throws Exception {
        if (token != generation) return;
        long started = android.os.SystemClock.elapsedRealtime();
        ui(token, () -> setState(State.NETWORK, "GENERATING", "Creating the Metal Sonic voice…"));
        byte[] pcm = fish.synthesize(key, voice, model, text);
        if (token != generation) return;
        lastAudio = pcm;
        long ms = android.os.SystemClock.elapsedRealtime() - started;
        Log.i(TAG, "TTS_OK bytes=" + pcm.length + " generation_ms=" + ms);
        ui(token, () -> setState(State.PLAYING, "SPEAKING", "Metal Sonic · generated in " + String.format(java.util.Locale.US, "%.1f", ms / 1000.0) + "s"));
        audio.play(pcm, flag, output, amount, monitor(token));
        ui(token, () -> message("Voice played. Replay it or record another phrase."));
    }

    private void replayLast() {
        byte[] pcm = lastAudio; if (pcm == null) return;
        AudioDeviceInfo output = selected(outputDevices, outputs); float amount = texture.isChecked() ? 0.7f : 0;
        begin(State.PLAYING, "SPEAKING", "Replaying the last Fish voice. No new API request.", (token, flag) -> {
            audio.play(pcm, flag, output, amount, monitor(token));
            ui(token, () -> message("Replay finished."));
        });
    }

    private void startLive() {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, 7); return;
        }
        AudioDeviceInfo output = selected(outputDevices, outputs), input = selected(inputDevices, inputs);
        if (!AudioEngine.external(output)) {
            message("Connect headphones or a USB audio interface, then select it under Output. Live mode needs an external output."); return;
        }
        begin(State.LIVE, "LIVE ROBOT", "Microphone → electronic effect → selected output. Tap Stop audio to finish.",
                (token, flag) -> audio.live(flag, input, output, monitor(token)));
    }

    private interface Task { void run(long token, AtomicBoolean flag) throws Exception; }

    private void begin(State initial, String title, String description, Task task) {
        if (state != State.IDLE || destroyed) return;
        if (manager.requestAudioFocus(focus) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) { message("Audio is busy. Close the other audio app and try again."); return; }
        final long token = ++generation;
        final AtomicBoolean flag = new AtomicBoolean(true); running = flag;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setState(initial, title, description);
        worker.execute(() -> {
            try { task.run(token, flag); }
            catch (Exception e) {
                if (token == generation) {
                    String safe = e instanceof java.net.SocketTimeoutException ? "Fish timed out. Check the connection and try again." : e.getMessage();
                    if (safe == null || safe.length() > 250) safe = "Audio request failed. Check the connection and settings.";
                    final String error = safe;
                    Log.w(TAG, "TASK_FAILED " + e.getClass().getSimpleName());
                    ui(token, () -> { status.setText("TRY AGAIN"); detail.setText(error); });
                }
            } finally {
                flag.set(false);
                ui(token, () -> {
                    state = State.IDLE; meter.setProgress(0); updateControls();
                    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                    manager.abandonAudioFocusRequest(focus);
                    if (!status.getText().toString().equals("TRY AGAIN")) status.setText("READY");
                });
            }
        });
    }

    private void cancel(String reason) {
        if (state == State.IDLE || destroyed) return;
        final long token = ++generation;
        running.set(false); audio.stop();
        new Thread(fish::cancel, "fish-cancel").start();
        setState(State.STOPPING, "STOPPING", reason);
        worker.execute(() -> ui(token, () -> {
            state = State.IDLE; meter.setProgress(0); status.setText("READY"); detail.setText(reason); updateControls();
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); manager.abandonAudioFocusRequest(focus);
        }));
    }

    private AudioEngine.Monitor monitor(final long token) {
        return new AudioEngine.Monitor() {
            public void level(float level) {
                long now = android.os.SystemClock.elapsedRealtime();
                if (now - lastMeter < 75) return;
                lastMeter = now;
                ui(token, () -> {
                    meter.setProgress((int) (1000 * level));
                    if (state == State.RECORDING) {
                        long elapsed = (android.os.SystemClock.elapsedRealtime() - recordingSince) / 1000;
                        detail.setText("Speak now · " + elapsed + " / 15s\nTap Send phrase when finished.");
                    }
                });
            }
            public void route(String description) { ui(token, () -> routeStatus.setText(description)); }
        };
    }

    private void setState(State next, String title, String description) { state = next; status.setText(title); detail.setText(description); updateControls(); }
    private void message(String text) { detail.setText(text); }
    private void ui(long token, Runnable work) { handler.post(() -> { if (!destroyed && token == generation) work.run(); }); }

    private void updateControls() {
        boolean idle = state == State.IDLE;
        record.setEnabled(idle || state == State.RECORDING); record.setText(state == State.RECORDING ? "Send phrase" : "Record phrase");
        speak.setEnabled(idle); test.setEnabled(idle); replay.setEnabled(idle && lastAudio != null); live.setEnabled(idle);
        inputs.setEnabled(idle); outputs.setEnabled(idle); texture.setEnabled(idle); settings.setEnabled(idle); phrase.setEnabled(idle);
        stop.setEnabled(!idle && state != State.STOPPING);
    }

    private void refreshDevices() {
        if (inputs == null) return;
        AudioDeviceInfo previousIn = selected(inputDevices, inputs), previousOut = selected(outputDevices, outputs);
        inputDevices.clear(); outputDevices.clear();
        for (AudioDeviceInfo d : manager.getDevices(AudioManager.GET_DEVICES_INPUTS)) inputDevices.add(d);
        for (AudioDeviceInfo d : manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) outputDevices.add(d);
        populate(inputs, inputDevices, previousIn, true); populate(outputs, outputDevices, previousOut, false);
    }

    private void populate(Spinner spinner, List<AudioDeviceInfo> devices, AudioDeviceInfo previous, boolean mic) {
        List<String> labels = new ArrayList<>(); labels.add("System default");
        int selection = 0;
        for (int i = 0; i < devices.size(); i++) {
            AudioDeviceInfo d = devices.get(i); labels.add(AudioEngine.name(d));
            if (previous != null && previous.getId() == d.getId()) selection = i + 1;
        }
        if (previous == null && mic) for (int i = 0; i < devices.size(); i++) {
            if (devices.get(i).getType() == AudioDeviceInfo.TYPE_BUILTIN_MIC) { selection = i + 1; break; }
        }
        if (previous == null && !mic) for (int i = 0; i < devices.size(); i++) {
            if (AudioEngine.external(devices.get(i))) { selection = i + 1; break; }
        }
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item); spinner.setAdapter(adapter); spinner.setSelection(selection);
    }

    private AudioDeviceInfo selected(List<AudioDeviceInfo> devices, Spinner spinner) {
        int i = spinner.getSelectedItemPosition() - 1;
        return i >= 0 && i < devices.size() ? devices.get(i) : null;
    }

    private String key() {
        try { return new String(Files.readAllBytes(new File(getFilesDir(), "fish_key").toPath()), StandardCharsets.UTF_8).trim(); }
        catch (Exception ignored) { return ""; }
    }

    private String requireKey() {
        String key = key();
        if (key.isEmpty()) { message("Add your Fish Audio key in Fish settings, then try again."); showSettings(); return null; }
        return key;
    }

    private String voice() { return getPreferences(0).getString("voice", DEFAULT_VOICE); }
    private String model() { return getPreferences(0).getString("model", "s1"); }
    private void refreshKeyStatus() { keyStatus.setText((key().isEmpty() ? "Fish key needed" : "Fish key configured") + " · model " + model()); }

    private void showSettings() {
        LinearLayout form = new LinearLayout(this); form.setOrientation(LinearLayout.VERTICAL); form.setPadding(dp(22), dp(8), dp(22), dp(8));
        addText(form, "API key", 14, WHITE, 0);
        EditText api = new EditText(this); api.setSingleLine(true); api.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        api.setHint(key().isEmpty() ? "Paste Fish Audio API key" : "Configured · leave blank to keep"); form.addView(api);
        addText(form, "Metal Sonic voice ID", 14, WHITE, 12);
        EditText voice = new EditText(this); voice.setSingleLine(true); voice.setText(voice()); form.addView(voice);
        addText(form, "Fish model", 14, WHITE, 12);
        Spinner models = new Spinner(this); String[] choices = {"s1", "s2-pro", "s2.1-pro", "s2.1-pro-free"};
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, choices); models.setAdapter(adapter);
        for (int i = 0; i < choices.length; i++) if (choices[i].equals(model())) models.setSelection(i);
        form.addView(models);
        addText(form, "Fish mode sends recorded speech and text to Fish Audio. Live robot stays on this phone. Keys stay in this app's private storage.", 13, MUTED, 14);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("Fish settings").setView(form).setNegativeButton("Cancel", null).setPositiveButton("Save", null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String id = voice.getText().toString().trim(), token = api.getText().toString().trim();
            if (id.isEmpty() || !id.matches("[A-Za-z0-9_-]{8,100}")) { voice.setError("Enter a Fish voice ID"); return; }
            if (token.matches(".*\\s+.*") || token.startsWith("FISH_API_KEY=")) { api.setError("Paste only the key value"); return; }
            try {
                if (!token.isEmpty()) try (FileOutputStream out = openFileOutput("fish_key", MODE_PRIVATE)) { out.write(token.getBytes(StandardCharsets.UTF_8)); }
                getPreferences(0).edit().putString("voice", id).putString("model", models.getSelectedItem().toString()).apply();
                refreshKeyStatus(); dialog.dismiss();
            } catch (Exception e) { api.setError("Could not save key"); }
        }));
        dialog.show(); dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
    }

    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] grants) {
        super.onRequestPermissionsResult(request, permissions, grants);
        if (request == 7) message(grants.length > 0 && grants[0] == PackageManager.PERMISSION_GRANTED ? "Microphone ready. Tap Record phrase or Start live robot." : "Microphone permission is needed to record your voice. Text mode still works.");
    }
    @Override protected void onResume() { super.onResume(); if (keyStatus != null) refreshKeyStatus(); }
    @Override protected void onPause() { cancel("Stopped because the app left the screen."); super.onPause(); }
    @Override protected void onSaveInstanceState(Bundle state) { state.putString("phrase", phrase.getText().toString()); super.onSaveInstanceState(state); }
    @Override protected void onDestroy() {
        cancel("App closed."); destroyed = true; generation++; running.set(false); audio.stop();
        manager.unregisterAudioDeviceCallback(deviceCallback); manager.abandonAudioFocusRequest(focus);
        worker.shutdownNow(); super.onDestroy();
    }

    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView label(String text, int size, int color) { TextView v = new TextView(this); v.setText(text); v.setTextSize(size); v.setTextColor(color); v.setLineSpacing(dp(2), 1f); return v; }
    private void addText(LinearLayout parent, String text, int size, int color, int top) { add(parent, label(text, size, color), top); }
    private void add(LinearLayout parent, View child, int top) { LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2); params.topMargin = dp(top); parent.addView(child, params); }
    private GradientDrawable background(int color, int radius) { GradientDrawable d = new GradientDrawable(); d.setColor(color); d.setCornerRadius(dp(radius)); return d; }
    private LinearLayout card(LinearLayout page, int top) { LinearLayout c = new LinearLayout(this); c.setOrientation(LinearLayout.VERTICAL); c.setPadding(dp(17), dp(17), dp(17), dp(17)); c.setBackground(background(CARD, 18)); add(page, c, top); return c; }
    private Button button(String text, int color, int foreground) { Button b = new Button(this); b.setText(text); b.setTextColor(foreground); b.setTextSize(15); b.setAllCaps(false); b.setMinHeight(dp(50)); b.setPadding(dp(12), dp(8), dp(12), dp(8)); b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(color)); return b; }
}
