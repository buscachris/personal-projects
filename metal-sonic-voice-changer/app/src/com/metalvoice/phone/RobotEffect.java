package com.metalvoice.phone;

/** Stateful, allocation-free metallic texture; an effect, not Fish voice cloning. */
public final class RobotEffect {
    private final int rate;
    private double phase, tremolo, previousInput, high, low;
    private final double[] delay;
    private int position;

    public RobotEffect(int rate) {
        this.rate = rate;
        delay = new double[Math.max(1, rate / 125)];
    }

    public void process(short[] samples, int count, float amount) {
        double strength = Math.max(0, Math.min(1, amount));
        for (int i = 0; i < count; i++) {
            double input = samples[i] / 32768.0;
            // Remove rumble, soften harsh harmonics, then add ring modulation and a short delay.
            high = 0.985 * (high + input - previousInput);
            previousInput = input;
            low += 0.42 * (high - low);
            double metallic = low * (0.60 + 0.55 * Math.sin(phase));
            double echo = delay[position];
            delay[position] = low;
            position = (position + 1) % delay.length;
            double wet = (metallic + echo * 0.25) * (0.90 + 0.10 * Math.sin(tremolo));
            double output = (input * (1 - strength) + wet * strength) * 0.85;
            samples[i] = (short) Math.max(-32768, Math.min(32767, Math.round(output * 32767)));
            phase += 2 * Math.PI * 72 / rate;
            tremolo += 2 * Math.PI * 28 / rate;
            if (phase > Math.PI * 2) phase -= Math.PI * 2;
            if (tremolo > Math.PI * 2) tremolo -= Math.PI * 2;
        }
    }
}
