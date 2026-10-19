package com.aurora.capture;

import android.graphics.Bitmap;
import android.hardware.HardwareBuffer;
import android.media.Image;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.IBinder;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

/** Persistent framework capture client. All hidden APIs are resolved by name once. */
public final class CaptureHelper {
    private static final int MAGIC = 0x41555243; // AURC, native little-endian wire protocol
    private static native int nativeSend(HardwareBuffer buffer, java.io.FileDescriptor socketDescriptor);
    private final Object windowManager;
    private final Object captureArgs;
    private final Method captureDisplay;
    private final Method createListener;
    private final Method getBuffer;
    private final Method getHardwareBuffer;
    private final Method getColorSpace;
    private final int displayId;
    private final Object displayManager;
    private final Method getDisplayInfo;
    private final int downscale;
    private final boolean mirrorRequested;
    private final float requestedRefreshRate;
    private DisplayGeometry geometry;
    private long nextGeometryCheckNs;
    private MirrorCapture mirror;
    private final List<MirrorCapture> retiredMirrors = new ArrayList<>();

    static final class DisplayGeometry {
        final int logicalWidth;
        final int logicalHeight;
        final int rotation;
        final int generation;
        DisplayGeometry(int width, int height, int rotation, int generation) {
            if (width <= 0 || height <= 0 || width > 32768 || height > 32768 || rotation < 0 || rotation > 3)
                throw new IllegalArgumentException("invalid source display geometry");
            logicalWidth = width;
            logicalHeight = height;
            this.rotation = rotation;
            this.generation = generation;
        }
        boolean sameLayout(DisplayGeometry other) {
            return other != null && logicalWidth == other.logicalWidth
                    && logicalHeight == other.logicalHeight && rotation == other.rotation;
        }
        void append(ByteBuffer header) {
            header.putInt(logicalWidth).putInt(logicalHeight).putInt(rotation).putInt(generation);
        }
        boolean matchesBuffer(int width, int height, int downscale) {
            // SurfaceFlinger's scaled integer dimensions may round differently for odd
            // logical sizes. A one-pixel difference is not a display configuration change.
            return Math.abs(width - logicalWidth / (float) downscale) <= 1.0f
                    && Math.abs(height - logicalHeight / (float) downscale) <= 1.0f;
        }
    }

    private void refreshGeometry(boolean force) throws Exception {
        long now = System.nanoTime();
        if (!force && now < nextGeometryCheckNs) return;
        nextGeometryCheckNs = now + 100000000L;
        // Direct service queries avoid a DisplayManagerGlobal cache whose invalidation
        // listener may not exist in a headless app_process. This is 10 IPCs/s, not per frame.
        Object info = getDisplayInfo.invoke(displayManager, displayId);
        if (info == null) throw new IllegalStateException("source display disappeared: " + displayId);
        DisplayGeometry updated = new DisplayGeometry(
                info.getClass().getField("logicalWidth").getInt(info),
                info.getClass().getField("logicalHeight").getInt(info),
                info.getClass().getField("rotation").getInt(info),
                geometry == null ? 1 : geometry.generation + 1);
        if (updated.sameLayout(geometry)) return;
        if (mirror != null) {
            mirror.retire();
            if (mirror.hasOutstandingLeases()) retiredMirrors.add(mirror);
            else mirror.close();
            mirror = null;
        }
        geometry = updated;
        System.err.println("[AuroraCapture] geometry logical=" + geometry.logicalWidth + "x"
                + geometry.logicalHeight + " rotation=" + geometry.rotation + " generation=" + geometry.generation);
        if (mirrorRequested) {
            try {
                mirror = new MirrorCapture(displayId, downscale, requestedRefreshRate, geometry);
            } catch (Throwable unsupported) {
                System.err.println("[AuroraMirror] geometry fallback to WMS snapshot: " + unsupported);
                unsupported.printStackTrace(System.err);
            }
        }
    }

    private void releaseFrame(int completed) {
        if (mirror != null) mirror.release(completed);
        for (Iterator<MirrorCapture> iterator = retiredMirrors.iterator(); iterator.hasNext();) {
            MirrorCapture retired = iterator.next();
            retired.release(completed);
            if (!retired.hasOutstandingLeases()) {
                retired.close();
                iterator.remove();
            }
        }
    }

    private static int readSequence(InputStream in) throws Exception {
        int sequence = 0;
        for (int shift = 0; shift != 32; shift += 8) {
            int value = in.read();
            if (value < 0) throw new IllegalStateException("truncated GPU lease acknowledgement");
            sequence |= value << shift;
        }
        return sequence;
    }

    private CaptureHelper(int displayId, int downscale, boolean mirrorRequested,
                          float requestedRefreshRate) throws Exception {
        this.displayId = displayId;
        this.downscale = downscale;
        this.mirrorRequested = mirrorRequested;
        this.requestedRefreshRate = requestedRefreshRate;
        // app_process registers the framework JNI implementations, including the platform
        // ScreenCaptureListener Binder implementation. No private native vtable is fabricated.
        Class<?> serviceManager = Class.forName("android.os.ServiceManager");
        IBinder displayBinder = (IBinder) serviceManager.getMethod("getService", String.class)
                .invoke(null, "display");
        if (displayBinder == null) throw new IllegalStateException("display service unavailable");
        displayManager = Class.forName("android.hardware.display.IDisplayManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, displayBinder);
        getDisplayInfo = Class.forName("android.hardware.display.IDisplayManager")
                .getMethod("getDisplayInfo", int.class);
        IBinder binder = (IBinder) serviceManager.getMethod("getService", String.class)
                .invoke(null, "window");
        if (binder == null) throw new IllegalStateException("window service unavailable");
        Class<?> wm = Class.forName("android.view.IWindowManager");
        windowManager = Class.forName("android.view.IWindowManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
        Class<?> screenCapture = Class.forName("android.window.ScreenCapture");
        Class<?> argsClass = Class.forName("android.window.ScreenCapture$CaptureArgs");
        Class<?> builderClass = Class.forName("android.window.ScreenCapture$CaptureArgs$Builder");
        Class<?> listenerClass = Class.forName("android.window.ScreenCapture$ScreenCaptureListener");
        Class<?> syncClass = Class.forName("android.window.ScreenCapture$SynchronousScreenCaptureListener");
        Class<?> bufferClass = Class.forName("android.window.ScreenCapture$ScreenshotHardwareBuffer");
        Object builder = builderClass.getConstructor().newInstance();
        builderClass.getMethod("setFrameScale", float.class).invoke(builder, 1.0f / downscale);
        builderClass.getMethod("setCaptureSecureLayers", boolean.class).invoke(builder, false);
        builderClass.getMethod("setAllowProtected", boolean.class).invoke(builder, false);
        // Ask for SDR data; it avoids gainmap and HDR buffers for a glass backdrop.
        try {
            builderClass.getMethod("setHintForSeamlessTransition", boolean.class)
                    .invoke(builder, true);
        } catch (NoSuchMethodException olderAndroid) { /* optional platform hint */ }
        captureArgs = builderClass.getMethod("build").invoke(builder);
        captureDisplay = wm.getMethod("captureDisplay", int.class, argsClass, listenerClass);
        createListener = screenCapture.getMethod("createSyncCaptureListener");
        getBuffer = syncClass.getMethod("getBuffer");
        getHardwareBuffer = bufferClass.getMethod("getHardwareBuffer");
        getColorSpace = bufferClass.getMethod("getColorSpace");
        refreshGeometry(true);
    }

    private void serve(LocalSocket socket) throws Exception {
        InputStream in = socket.getInputStream();
        OutputStream out = socket.getOutputStream();
        byte[] pixels = null;
        ByteBuffer pixelBuffer = null;
        ByteBuffer header = ByteBuffer.allocate(80).order(ByteOrder.LITTLE_ENDIAN);
        int sequence = 0;
        for (;;) {
            int request = in.read();
            if (request == 4) {
                int completed = readSequence(in);
                releaseFrame(completed);
                continue;
            }
            if (request != 1 && request != 2 && request != 3) break;
            final long begin = System.nanoTime();
            refreshGeometry(false);
            if (request == 3 && mirror != null) {
                MirrorCapture.Frame frame;
                try {
                    frame = mirror.take(sequence + 1);
                } catch (MirrorCapture.InitialFrameTimeout unsupported) {
                    // Only a mirror which has never delivered an Image may fall back here.
                    // A failure after startup is fatal, rather than freezing the backdrop.
                    System.err.println("[AuroraMirror] initial-frame fallback to WMS snapshot: " + unsupported);
                    mirror.close();
                    mirror = null;
                    frame = null;
                }
                if (mirror != null) {
                Image image = frame == null ? null : frame.image;
                HardwareBuffer hardwareBuffer = null;
                try {
                    if (image != null) {
                        MirrorCapture.awaitProducer(image);
                        hardwareBuffer = image.getHardwareBuffer();
                        if (hardwareBuffer == null) throw new IllegalStateException("mirror HardwareBuffer unavailable");
                        ++sequence;
                    }
                    long captured = System.nanoTime();
                    header.clear();
                    header.putInt(MAGIC).putInt(6).putInt(mirror.width()).putInt(mirror.height()).putInt(0)
                            .putInt(sequence).putInt((int) ((captured - begin) / 1000)).putInt(0)
                            .putLong(image == null ? mirror.lastTimestamp() : image.getTimestamp())
                            .putLong(image == null ? mirror.lastReceivedTimestamp() : frame.receivedTimestamp)
                            .putInt((int) (image == null ? mirror.eventCount() : frame.eventCount))
                            .putInt((int) (image == null ? mirror.droppedCount() : frame.droppedCount))
                            .putInt((int) (image == null ? mirror.duplicateCount() : frame.duplicateCount))
                            .putInt(image == null ? 1 : 0);
                    mirror.geometry().append(header);
                    out.write(header.array());
                    out.flush();
                    if (image != null) {
                        int result = nativeSend(hardwareBuffer, socket.getFileDescriptor());
                        if (result != 0) throw new IllegalStateException("mirror AHardwareBuffer send status=" + result);
                        if (sequence == 1 || sequence % 120 == 0)
                            System.err.println("[AuroraMirror] frame=" + sequence + " events=" + frame.eventCount
                                    + " dropped=" + frame.droppedCount + " duplicates=" + frame.duplicateCount
                                    + " timestampNs=" + image.getTimestamp()
                                    + " readyAgeUs=" + ((captured - image.getTimestamp()) / 1000));
                    }
                } finally {
                    // The allocation reference can close after FD transfer. The Image itself
                    // stays in mirror.leases until the receiving GPU acknowledges this sequence.
                    if (hardwareBuffer != null) hardwareBuffer.close();
                }
                continue;
                }
            }
            if (request == 3) request = 2; // Startup-only compatibility fallback.
            DisplayGeometry snapshotGeometry = geometry;
            Object listener = createListener.invoke(null);
            // WindowManager owns the root SurfaceControl for each logical display. This
            // works for display 0 and virtual displays, and captures wallpaper + all apps.
            captureDisplay.invoke(windowManager, displayId, captureArgs, listener);
            Object screenshot = getBuffer.invoke(listener);
            final long captured = System.nanoTime();
            if (screenshot == null) throw new IllegalStateException("capture callback has no buffer");
            HardwareBuffer hardwareBuffer = (HardwareBuffer) getHardwareBuffer.invoke(screenshot);
            Bitmap hardwareBitmap = null;
            Bitmap softwareBitmap = null;
            try {
                if (request == 2) {
                    int width = hardwareBuffer.getWidth(), height = hardwareBuffer.getHeight();
                    if (!geometry.matchesBuffer(width, height, downscale)) {
                        refreshGeometry(true); // Rotation occurred while WMS was capturing.
                        if (geometry.matchesBuffer(width, height, downscale))
                            snapshotGeometry = geometry;
                    }
                    header.clear();
                    // Snapshot request start is a lower bound for actual source-frame age.
                    // It is explicitly identified by wire mode 7, rather than claimed to be
                    // the underlying app's presentation timestamp.
                    header.putInt(MAGIC).putInt(7).putInt(width).putInt(height).putInt(0)
                            .putInt(++sequence).putInt((int) ((captured - begin) / 1000)).putInt(0)
                            .putLong(begin).putLong(captured).putInt(0).putInt(0).putInt(0).putInt(0);
                    snapshotGeometry.append(header);
                    out.write(header.array());
                    out.flush();
                    int result = nativeSend(hardwareBuffer, socket.getFileDescriptor());
                    if (result != 0) throw new IllegalStateException("AHardwareBuffer send status=" + result);
                    if (sequence == 1 || sequence % 120 == 0)
                        System.err.println("[AuroraCapture] GPU frame=" + sequence + " " + width + "x"
                                + height + " captureUs=" + ((captured - begin) / 1000));
                    continue;
                }
                hardwareBitmap = Bitmap.wrapHardwareBuffer(hardwareBuffer,
                        (android.graphics.ColorSpace) getColorSpace.invoke(screenshot));
                if (hardwareBitmap == null) throw new IllegalStateException("wrapHardwareBuffer failed");
                softwareBitmap = hardwareBitmap.copy(Bitmap.Config.ARGB_8888, false);
                if (softwareBitmap == null) throw new IllegalStateException("GPU readback failed");
                int width = softwareBitmap.getWidth();
                int height = softwareBitmap.getHeight();
                int bytes = softwareBitmap.getByteCount();
                if (width <= 0 || height <= 0 || width > 4096 || height > 4096
                        || softwareBitmap.getRowBytes() != width * 4 || bytes != width * height * 4)
                    throw new IllegalStateException("unexpected RGBA bitmap stride");
                if (pixels == null || pixels.length != bytes) {
                    pixels = new byte[bytes];
                    pixelBuffer = ByteBuffer.wrap(pixels);
                }
                pixelBuffer.clear();
                softwareBitmap.copyPixelsToBuffer(pixelBuffer);
                final long copied = System.nanoTime();
                header.clear();
                if (!geometry.matchesBuffer(width, height, downscale)) {
                    refreshGeometry(true);
                    if (geometry.matchesBuffer(width, height, downscale))
                        snapshotGeometry = geometry;
                }
                header.putInt(MAGIC).putInt(8).putInt(width).putInt(height).putInt(bytes)
                        .putInt(++sequence).putInt((int) ((captured - begin) / 1000))
                        .putInt((int) ((copied - captured) / 1000))
                        .putLong(begin).putLong(captured).putInt(0).putInt(0).putInt(0).putInt(0);
                snapshotGeometry.append(header);
                out.write(header.array());
                out.write(pixels);
                out.flush();
                if (sequence == 1 || sequence % 120 == 0)
                    System.err.println("[AuroraCapture] frame=" + sequence + " " + width + "x"
                            + height + " captureUs=" + ((captured - begin) / 1000)
                            + " readbackUs=" + ((copied - captured) / 1000));
            } finally {
                if (softwareBitmap != null) softwareBitmap.recycle();
                if (hardwareBitmap != null) hardwareBitmap.recycle();
                if (hardwareBuffer != null) hardwareBuffer.close();
            }
        }
    }

    public static void main(String[] args) {
        try (LocalSocket socket = new LocalSocket()) {
            if (args.length != 4 && args.length != 6)
                throw new IllegalArgumentException("socket logicalDisplayId downscale nativeLibrary [backend requestedHz]");
            int downscale = Integer.parseInt(args[2]);
            if (downscale < 1 || downscale > 8) throw new IllegalArgumentException("downscale out of range");
            System.load(args[3]);
            CaptureHelper helper = new CaptureHelper(Integer.parseInt(args[1]), downscale,
                    args.length == 6 && args[4].equals("mirror"),
                    args.length == 6 ? Float.parseFloat(args[5]) : 120.0f);
            socket.connect(new LocalSocketAddress(args[0], LocalSocketAddress.Namespace.ABSTRACT));
            System.err.println("[AuroraCapture] ready uid=" + android.os.Process.myUid()
                    + " display=" + args[1] + " downscale=" + downscale);
            try { helper.serve(socket); }
            finally {
                if (helper.mirror != null) helper.mirror.close();
                for (MirrorCapture retired : helper.retiredMirrors) retired.close();
                helper.retiredMirrors.clear();
            }
        } catch (Throwable failure) {
            failure.printStackTrace(System.err);
            System.exit(1);
        }
    }
}
