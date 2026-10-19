package com.aurora.runtime;

final class OverlayGeometry {
    private OverlayGeometry() {}

    static int[] map(float[] region, int width, int height, int originX, int originY) {
        if (region == null || region.length != 4 || width <= 0 || height <= 0) {
            return null;
        }
        for (float value : region) {
            if (!Float.isFinite(value)) {
                return null;
            }
        }
        final int left = (int) Math.floor(Math.max(0f, region[0]) * width);
        final int top = (int) Math.floor(Math.max(0f, region[1]) * height);
        final int right = (int) Math.ceil(Math.min(1f, region[2]) * width);
        final int bottom = (int) Math.ceil(Math.min(1f, region[3]) * height);
        if (right <= left || bottom <= top) {
            return null;
        }
        return new int[] {originX + left, originY + top, right - left, bottom - top};
    }
}
