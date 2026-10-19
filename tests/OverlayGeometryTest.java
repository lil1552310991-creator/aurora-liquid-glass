package com.aurora.runtime;

import java.util.Arrays;

public final class OverlayGeometryTest {
    private static void expect(int[] actual, int... expected) {
        if (!Arrays.equals(actual, expected)) {
            throw new AssertionError(Arrays.toString(actual));
        }
    }

    public static void main(String[] args) {
        expect(OverlayGeometry.map(new float[] {0, 0, .5f, .5f}, 1080, 2376, 0, 120),
                0, 120, 540, 1188);
        expect(OverlayGeometry.map(new float[] {.5f, .5f, 1, 1}, 1080, 2376, 0, 120),
                540, 1308, 540, 1188);
        expect(OverlayGeometry.map(new float[] {-.1f, -.1f, .5f, .5f}, 100, 100, 15, 30),
                15, 30, 50, 50);
        expect(OverlayGeometry.map(new float[] {.5f, .5f, 1.2f, 1.2f}, 100, 100, 0, 0),
                50, 50, 50, 50);
        if (OverlayGeometry.map(new float[4], 100, 100, 0, 0) != null
                || OverlayGeometry.map(new float[] {2, 0, 3, 1}, 100, 100, 0, 0) != null
                || OverlayGeometry.map(new float[] {0, 0, Float.NaN, 1}, 100, 100, 0, 0) != null
                || OverlayGeometry.map(new float[] {0, 0, 1, 1}, 0, 100, 0, 0) != null) {
            throw new AssertionError("invalid region must disable the proxy");
        }
        System.out.println("OverlayGeometryTest passed");
    }
}
