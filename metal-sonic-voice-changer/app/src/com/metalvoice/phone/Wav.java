package com.metalvoice.phone;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public final class Wav {
    private Wav() {}

    public static byte[] wrap(byte[] pcm, int rate) {
        ByteBuffer b = ByteBuffer.allocate(44 + pcm.length).order(ByteOrder.LITTLE_ENDIAN);
        b.put(new byte[]{'R','I','F','F'}).putInt(36 + pcm.length);
        b.put(new byte[]{'W','A','V','E','f','m','t',' '}).putInt(16);
        b.putShort((short) 1).putShort((short) 1).putInt(rate).putInt(rate * 2);
        b.putShort((short) 2).putShort((short) 16);
        b.put(new byte[]{'d','a','t','a'}).putInt(pcm.length).put(pcm);
        return b.array();
    }
}
