package com.aurora.capture;

import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.hardware.HardwareBuffer;
import android.hardware.SyncFence;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.view.Surface;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.time.Duration;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.Executor;

/** A damage-driven WMS subtree mirror. Images remain leased until the ELF GPU ACKs them. */
final class MirrorCapture implements AutoCloseable {
    /** No Image has ever arrived, so there cannot be any outstanding GPU lease. */
    static final class InitialFrameTimeout extends Exception {
        InitialFrameTimeout() { super("mirror produced no initial frame within 2000ms"); }
    }
    static final class Frame {
        final Image image;
        final long receivedTimestamp;
        final long eventCount;
        final long droppedCount;
        final long duplicateCount;
        Frame(Image image, long received, long events, long dropped, long duplicates) {
            this.image = image;
            receivedTimestamp = received;
            eventCount = events;
            droppedCount = dropped;
            duplicateCount = duplicates;
        }
    }
    // Five leased GPU frames, one latest frame, and two free slots for acquireLatestImage.
    // The normal path has only one or two leases; this bounds a temporarily stalled GPU.
    private static final int MAX_IMAGES = 8;
    private final Object lock = new Object();
    private final Map<Integer, Image> leases = new HashMap<>();
    private HandlerThread thread;
    private ImageReader reader;
    private Object displayManager;
    private Object displayCallback;
    private Method releaseDisplay;
    private int mirrorDisplayId = -1;
    private Object mirrorSurface;
    private Object mirrorRoot;
    private Class<?> surfaceControlClass;
    private Class<?> transactionClass;
    private Image latest;
    private long latestTimestamp;
    private long latestReceivedTimestamp;
    private long events;
    private long dropped;
    private long duplicateTimestamps;
    private long previousTimestamp;
    private boolean closed;
    private Throwable failure;
    private int width;
    private int height;
    private int idleWaitMs;
    private final CaptureHelper.DisplayGeometry geometry;

    MirrorCapture(int sourceDisplay, int downscale, float requestedRefreshRate,
                  CaptureHelper.DisplayGeometry geometry) throws Exception {
        this.geometry = geometry;
        try {
            Class<?> globalClass = Class.forName("android.hardware.display.DisplayManagerGlobal");
            Object global = globalClass.getMethod("getInstance").invoke(null);
            int sourceWidth = geometry.logicalWidth;
            int sourceHeight = geometry.logicalHeight;
            width = Math.max(1, sourceWidth / downscale);
            height = Math.max(1, sourceHeight / downscale);
            idleWaitMs = Math.max(8, Math.min(100, (int) Math.ceil(2000.0 / requestedRefreshRate)));
            thread = new HandlerThread("AuroraMirrorImages");
            thread.start();
            reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, MAX_IMAGES,
                    HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE);
            reader.setOnImageAvailableListener(this::onImageAvailable, new Handler(thread.getLooper()));

            Class<?> builderClass = Class.forName("android.hardware.display.VirtualDisplayConfig$Builder");
            Object builder = builderClass.getConstructor(String.class, int.class, int.class, int.class)
                    .newInstance("AuroraBackdropMirror", width, height, 1);
            builderClass.getMethod("setFlags", int.class).invoke(builder, 16); // AUTO_MIRROR, never hosts apps/input
            builderClass.getMethod("setDisplayIdToMirror", int.class).invoke(builder, sourceDisplay);
            // Suppress DMS's whole-layer-stack mirror. Our overlay is a sibling of the WMS
            // display root, so including the whole stack would feed the glass back into itself.
            builderClass.getMethod("setWindowManagerMirroringEnabled", boolean.class).invoke(builder, true);
            builderClass.getMethod("setRequestedRefreshRate", float.class).invoke(builder, requestedRefreshRate);
            builderClass.getMethod("setSurface", Surface.class).invoke(builder, reader.getSurface());
            Object config = builderClass.getMethod("build").invoke(builder);
            Class<?> callbackClass = Class.forName("android.hardware.display.DisplayManagerGlobal$VirtualDisplayCallback");
            Constructor<?> callbackCtor = callbackClass.getDeclaredConstructor(VirtualDisplay.Callback.class, Executor.class);
            callbackCtor.setAccessible(true);
            Object callback = callbackCtor.newInstance(null, null);
            Class<?> serviceManager = Class.forName("android.os.ServiceManager");
            IBinder displayBinder = (IBinder) serviceManager.getMethod("getService", String.class).invoke(null, "display");
            Object manager = Class.forName("android.hardware.display.IDisplayManager$Stub")
                    .getMethod("asInterface", IBinder.class).invoke(null, displayBinder);
            Class<?> configClass = Class.forName("android.hardware.display.VirtualDisplayConfig");
            Class<?> callbackInterface = Class.forName("android.hardware.display.IVirtualDisplayCallback");
            Class<?> projectionClass = Class.forName("android.media.projection.IMediaProjection");
            Method create = Class.forName("android.hardware.display.IDisplayManager").getMethod(
                    "createVirtualDisplay", configClass, callbackInterface, projectionClass, String.class);
            displayManager = manager;
            displayCallback = callback;
            releaseDisplay = Class.forName("android.hardware.display.IDisplayManager")
                    .getMethod("releaseVirtualDisplay", callbackInterface);
            int id = (Integer) create.invoke(manager, config, callback, null, null);
            if (id < 0) throw new IllegalStateException("mirror display creation failed: " + id);
            mirrorDisplayId = id;
            Object mirrorInfo = globalClass.getMethod("getDisplayInfo", int.class).invoke(global, id);
            if (mirrorInfo == null) throw new IllegalStateException("mirror display info unavailable");
            int layerStack = mirrorInfo.getClass().getField("layerStack").getInt(mirrorInfo);

            // IWindowManager.mirrorDisplay mirrors getWindowingLayer(), which contains the
            // desktop/app hierarchy while excluding our global root sibling. The mirror is
            // assigned only to this new virtual display's stack; the source is never mutated.
            surfaceControlClass = Class.forName("android.view.SurfaceControl");
            transactionClass = Class.forName("android.view.SurfaceControl$Transaction");
            Constructor<?> surfaceConstructor = surfaceControlClass.getDeclaredConstructor();
            surfaceConstructor.setAccessible(true);
            mirrorSurface = surfaceConstructor.newInstance();
            IBinder windowBinder = (IBinder) serviceManager.getMethod("getService", String.class).invoke(null, "window");
            Object windowManager = Class.forName("android.view.IWindowManager$Stub")
                    .getMethod("asInterface", IBinder.class).invoke(null, windowBinder);
            boolean mirrored = (Boolean) Class.forName("android.view.IWindowManager")
                    .getMethod("mirrorDisplay", int.class, surfaceControlClass)
                    .invoke(windowManager, sourceDisplay, mirrorSurface);
            if (!mirrored) throw new IllegalStateException("WMS subtree mirror unavailable");
            // mirrorSurface() explicitly creates an offscreen layer (addToRoot=false).
            // A stack vote cannot change that. Root can create a normal container, then
            // attach the mirror to it without reparenting anything in the source display.
            Class<?> surfaceBuilderClass = Class.forName("android.view.SurfaceControl$Builder");
            Object rootBuilder = surfaceBuilderClass.getConstructor().newInstance();
            surfaceBuilderClass.getMethod("setName", String.class).invoke(rootBuilder, "AuroraBackdropMirrorRoot");
            surfaceBuilderClass.getMethod("setContainerLayer").invoke(rootBuilder);
            mirrorRoot = surfaceBuilderClass.getMethod("build").invoke(rootBuilder);
            Object transaction = transactionClass.getConstructor().newInstance();
            try {
                transactionClass.getMethod("setLayerStack", surfaceControlClass, int.class)
                        .invoke(transaction, mirrorRoot, layerStack);
                transactionClass.getMethod("reparent", surfaceControlClass, surfaceControlClass)
                        .invoke(transaction, mirrorSurface, mirrorRoot);
                transactionClass.getMethod("setPosition", surfaceControlClass, float.class, float.class)
                        .invoke(transaction, mirrorSurface, 0.0f, 0.0f);
                transactionClass.getMethod("setMatrix", surfaceControlClass, float.class, float.class, float.class, float.class)
                        .invoke(transaction, mirrorSurface, width / (float) sourceWidth, 0.0f, 0.0f, height / (float) sourceHeight);
                // WMS windowing layers already use logical display coordinates. Physical
                // rotation belongs to the display projection; applying it here rotates twice.
                // The crop excludes oversized wallpaper/parallax layers on either orientation.
                transactionClass.getMethod("setCrop", surfaceControlClass, Rect.class)
                        .invoke(transaction, mirrorSurface, new Rect(0, 0, sourceWidth, sourceHeight));
                transactionClass.getMethod("show", surfaceControlClass).invoke(transaction, mirrorSurface);
                transactionClass.getMethod("show", surfaceControlClass).invoke(transaction, mirrorRoot);
                transactionClass.getMethod("apply").invoke(transaction);
            } finally {
                transactionClass.getMethod("close").invoke(transaction);
            }
            float actualRefreshRate = 0.0f;
            try {
                Object mode = mirrorInfo.getClass().getMethod("getMode").invoke(mirrorInfo);
                actualRefreshRate = (Float) mode.getClass().getMethod("getRefreshRate").invoke(mode);
            } catch (ReflectiveOperationException optionalDiagnostic) { /* requestedHz remains available */ }
            System.err.println("[AuroraMirror] ready source=" + sourceDisplay + " mirror=" + id
                    + " stack=" + layerStack + " size=" + width + "x" + height
                    + " requestedHz=" + requestedRefreshRate + " actualHz=" + actualRefreshRate
                    + " logical=" + sourceWidth + "x" + sourceHeight + " rotation=" + geometry.rotation
                    + " generation=" + geometry.generation
                    + " origin=WMS-windowing-subtree");
        } catch (Throwable error) {
            close();
            if (error instanceof Exception) throw (Exception) error;
            throw new RuntimeException(error);
        }
    }

    private void onImageAvailable(ImageReader source) {
        synchronized (lock) {
            if (closed) return;
            try {
                Image image = source.acquireLatestImage();
                if (image == null) return;
                long timestamp = image.getTimestamp();
                ++events;
                if (timestamp == previousTimestamp) ++duplicateTimestamps;
                previousTimestamp = timestamp;
                if (latest != null) { latest.close(); ++dropped; }
                latest = image;
                latestTimestamp = timestamp;
                latestReceivedTimestamp = System.nanoTime();
                lock.notifyAll();
            } catch (Throwable error) {
                failure = error;
                lock.notifyAll();
            }
        }
    }

    Frame take(int sequence) throws Exception {
        synchronized (lock) {
            final long deadline = System.nanoTime() + (events == 0 ? 2000 : idleWaitMs) * 1000000L;
            while (latest == null && failure == null && !closed) {
                long remaining = deadline - System.nanoTime();
                if (remaining <= 0) {
                    if (events == 0) throw new InitialFrameTimeout();
                    return null; // Unchanged source is an idle frame, not a capture failure.
                }
                lock.wait(Math.max(1, remaining / 1000000L));
            }
            if (failure != null) throw new IllegalStateException("mirror image listener failed", failure);
            if (closed) throw new IllegalStateException("mirror stopped");
            // Keep the latest image while the GPU is busy, then return to the
            // command reader so queued ACKs can release leases. A transient GPU
            // stall must not close/recycle an allocation still being sampled.
            if (leases.size() >= MAX_IMAGES - 3) return null;
            Image image = latest;
            latest = null;
            leases.put(sequence, image);
            return new Frame(image, latestReceivedTimestamp, events, dropped, duplicateTimestamps);
        }
    }

    static void awaitProducer(Image image) throws Exception {
        // An AHardwareBuffer handle owns the allocation, not the producer's acquire fence.
        // Waiting once before sending preserves completed-frame ordering without CPU pixels.
        try (SyncFence fence = image.getFence()) {
            if (fence.isValid() && !fence.await(Duration.ofSeconds(1)))
                throw new IllegalStateException("mirror producer fence timeout");
        }
    }

    void release(int sequence) {
        synchronized (lock) {
            Image image = leases.remove(sequence);
            if (image != null) image.close();
            if (closed && leases.isEmpty()) closeReader();
        }
    }

    boolean hasOutstandingLeases() { synchronized (lock) { return !leases.isEmpty(); } }
    CaptureHelper.DisplayGeometry geometry() { return geometry; }

    int width() { return width; }
    int height() { return height; }
    long eventCount() { synchronized (lock) { return events; } }
    long droppedCount() { synchronized (lock) { return dropped; } }
    long duplicateCount() { synchronized (lock) { return duplicateTimestamps; } }
    long lastTimestamp() { synchronized (lock) { return latestTimestamp; } }
    long lastReceivedTimestamp() { synchronized (lock) { return latestReceivedTimestamp; } }

    // Stop old-generation production without invalidating Images sampled by the GPU.
    // CaptureHelper keeps this object until every old sequence has been ACKed.
    void retire() {
        synchronized (lock) {
            if (closed) return;
            closed = true;
            System.err.println("[AuroraMirror] retire generation=" + geometry.generation + " leases=" + leases.size());
            if (mirrorSurface != null && transactionClass != null) {
                try {
                    Object transaction = transactionClass.getConstructor().newInstance();
                    try {
                        transactionClass.getMethod("remove", surfaceControlClass).invoke(transaction, mirrorSurface);
                        if (mirrorRoot != null)
                            transactionClass.getMethod("remove", surfaceControlClass).invoke(transaction, mirrorRoot);
                        transactionClass.getMethod("apply").invoke(transaction);
                    } finally {
                        transactionClass.getMethod("close").invoke(transaction);
                    }
                } catch (Throwable error) { System.err.println("[AuroraMirror] remove: " + error); }
                try { surfaceControlClass.getMethod("release").invoke(mirrorSurface); }
                catch (Throwable error) { System.err.println("[AuroraMirror] release: " + error); }
                mirrorSurface = null;
            }
            if (mirrorRoot != null) {
                try { surfaceControlClass.getMethod("release").invoke(mirrorRoot); }
                catch (Throwable error) { System.err.println("[AuroraMirror] root release: " + error); }
                mirrorRoot = null;
            }
            if (mirrorDisplayId >= 0 && releaseDisplay != null) {
                try { releaseDisplay.invoke(displayManager, displayCallback); }
                catch (Throwable error) { System.err.println("[AuroraMirror] display release: " + error); }
                mirrorDisplayId = -1;
            }
            if (latest != null) { latest.close(); latest = null; }
            if (leases.isEmpty()) closeReader();
            lock.notifyAll();
        }
        if (thread != null) thread.quitSafely();
    }

    private void closeReader() {
        if (reader != null) {
            reader.close(); reader = null;
            System.err.println("[AuroraMirror] reader released generation=" + geometry.generation + " leases=" + leases.size());
        }
    }

    @Override public void close() {
        retire();
        synchronized (lock) {
            // Final socket EOF occurs only after the native owner drains GPU work.
            for (Image image : leases.values()) image.close();
            leases.clear();
            closeReader();
        }
    }
}
