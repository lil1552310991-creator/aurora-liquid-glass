package com.aurora.imebridge;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Insets;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.Selection;
import android.util.Base64;
import android.util.DisplayMetrics;
import android.view.ContextThemeWrapper;
import android.view.Display;
import android.view.Gravity;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.InetAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * 输入法宿主服务。由系统通过已安装的辅助包启动，因此拥有真正的应用身份。
 *
 * 核心机制（参考 ssyclr/AndroidSurfaceImgui-Enhanced-Input-method）：
 *   创建一个**透明、无阴影的 1×1 悬浮窗**。待命时窗口带
 *   FLAG_NOT_FOCUSABLE（把焦点还给前台应用）；需要输入时移除该标志并
 *   requestFocus()，此时窗口成为输入法焦点宿主，再 showSoftInput 拉起键盘。
 *
 *   为什么必须是 1×1 而不是全屏：全屏透明窗会挡住下方应用的触摸。
 *
 * 与 C++ 的通信：C++ 侧是 TCP **服务端**（监听 127.0.0.1 随机端口），
 * 本服务是**客户端**。连上后 C++ 先发 `HELLO <token>`，本服务校验后回 `READY`。
 */
public final class AuroraImeService extends Service {

    private static final String TAG = "AuroraIme";
    private static final String CHANNEL_ID = "aurora_ime";
    private static final int NOTIFICATION_ID = 0x41555241;   // "AURA"

    /** 输入会话状态。 */
    private static final class Session {
        int id = 0;
        boolean shown = false;
    }

    private Context windowContext;
    private int displayId = 0;
    private int resolvedDisplayId = 0;

    private WindowManager windowManager;
    private EditText editText;
    private WindowManager.LayoutParams params;
    private InputMethodManager imm;
    private ClipboardManager clipboard;

    private Socket client;
    private PrintWriter out;
    private BufferedReader in;
    private final java.util.concurrent.BlockingQueue<String> outQueue =
            new java.util.concurrent.LinkedBlockingQueue<>();
    private volatile boolean writerRunning = false;

    private String token = "";
    private int port = 0;

    private final Session session = new Session();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final AtomicBoolean running = new AtomicBoolean(false);

    private String lastSentText = "";

    /**
     * 编辑回传的合并节流。中文输入法一次上屏会触发多次回调，而且 applySync
     * 自己的 setText/append 也会触发回调 —— 那会造成自我回环。
     */
    private boolean ignoreNextChange = false;
    private boolean reportScheduled = false;
    private boolean lastReportedVisible = false;
    /** 上一次回传给 ELF 的键盘矩形（物理像素；全 0 = 未知）。 */
    private final int[] lastImeRect = new int[4];
    private boolean hideRequested = false;
    private int showAttempts = 0;
    private final Runnable showRetry = this::showRetry;
    private static final long REPORT_DELAY_MS = 30;

    // -----------------------------------------------------------------------
    // 生命周期
    // -----------------------------------------------------------------------
    @Override
    public void onCreate() {
        super.onCreate();
        clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        // 前台服务在 Android 12+ 可能因后台启动限制抛异常。这里只是「尽量」提升，
        // 失败不影响主流程（辅助包已拿到 SYSTEM_ALERT_WINDOW，本身就被豁免）。
        try {
            startForegroundCompat();
        } catch (Throwable t) {
            log("startForeground 失败（忽略）: " + t);
        }
        log("Service onCreate");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null) {
            port = intent.getIntExtra("port", 0);
            displayId = intent.getIntExtra("display", 0);
            token = intent.getStringExtra("token");
            if (token == null) token = "";
        }
        log("onStartCommand port=" + port + " display=" + displayId);

        if (!running.getAndSet(true)) {
            handler.post(() -> {
                prepareWindowContext();
                createInputWindow();
                new Thread(this::connectLoop, "aurora-ime-io").start();
            });
        }
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        log("Service onDestroy");
        running.set(false);
        closeQuietly();
        removeInputWindow();
        super.onDestroy();
    }

    // -----------------------------------------------------------------------
    // 窗口上下文：display > 0 时把窗口建在指定显示上
    // -----------------------------------------------------------------------
    /**
     * 默认情况下 Service 的 Context 属于默认显示（display 0，物理屏）。
     * 传入 display=N 时改用一个绑定到该显示的 Context，这样：
     *   - WindowManager 把 1×1 窗建在显示 N 上；
     *   - InputMethodManager.forContext(ctx) 的 mDisplayId 也是 N，
     *     IMMS 的 hasInputMethodClientFocus 才能对上（WMS:8223-8245）。
     */
    private void prepareWindowContext() {
        windowContext = this;
        resolvedDisplayId = 0;
        if (displayId > 0) {
            try {
                DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
                Display d = dm == null ? null : dm.getDisplay(displayId);
                if (d == null) {
                    log("显示 " + displayId + " 不存在，回退默认显示");
                } else {
                    Context dc = createDisplayContext(d);
                    // EditText 需要主题；在显示 Context 外面再套一层主题包装，
                    // 这样 view.getContext().getDisplayId() 仍然是 displayId。
                    windowContext = new ContextThemeWrapper(
                            dc, android.R.style.Theme_Material_Light_NoActionBar);
                    resolvedDisplayId = displayId;
                    log("窗口上下文绑定到显示 " + displayId);
                }
            } catch (Throwable t) {
                log("createDisplayContext 失败，回退默认显示: " + t);
                windowContext = this;
                resolvedDisplayId = 0;
            }
        }
        windowManager = (WindowManager) windowContext.getSystemService(Context.WINDOW_SERVICE);
        imm = (InputMethodManager) windowContext.getSystemService(Context.INPUT_METHOD_SERVICE);
    }

    // -----------------------------------------------------------------------
    // 前台通知
    // -----------------------------------------------------------------------
    private void startForegroundCompat() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            NotificationChannel ch = new NotificationChannel(CHANNEL_ID, "Aurora IME",
                    NotificationManager.IMPORTANCE_MIN);
            ch.setShowBadge(false);
            ch.setSound(null, null);
            ch.enableVibration(false);
            nm.createNotificationChannel(ch);
        }
        Notification.Builder b = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        Notification n = b.setContentTitle("Aurora 输入")
                .setContentText("输入法桥接运行中")
                .setSmallIcon(android.R.drawable.ic_menu_edit)
                .setPriority(Notification.PRIORITY_MIN)
                .build();
        startForeground(NOTIFICATION_ID, n);
    }

    // -----------------------------------------------------------------------
    // 1x1 透明输入窗
    // -----------------------------------------------------------------------
    private void createInputWindow() {
        if (editText != null) return;
        if (windowManager == null) {
            log("添加窗口失败: WindowManager 为空");
            return;
        }

        editText = new EditText(windowContext);
        editText.setBackgroundColor(0x00000000);
        editText.setAlpha(0.0f);
        editText.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        editText.setSingleLine(false);
        editText.setImeOptions(EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI);

        editText.addTextChangedListener(new android.text.TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(Editable s) {
                scheduleEditReport();
            }
        });

        // 真实可见性来自 WindowInsets，而不是「我调过 showSoftInput」的假设。
        // ★★ 同时把**键盘的实际几何**回传给 ELF：我们的玻璃覆盖层是 SurfaceFlinger 里
        //    mRoot 的兄弟层，z 序在 WMS 整棵子树（含输入法弹层）之上，所以
        //    ① 视觉上玻璃会压住键盘；② 触摸上落在我们拦截区里的点会先给我们、按键点不动。
        //    宿主拿到这个矩形后就能把键盘那一块从触摸拦截区里挖掉、并让窗口避让。
        //    协议里 "IME <id> <vis> <l> <t> <r> <b>" 的后 4 个字段原本是预留的 0，
        //    旧版 C++ 只读前两段 ⇒ 多发字段不会打坏旧宿主（向后兼容）。
        editText.setOnApplyWindowInsetsListener((v, insets) -> {
            boolean visible = insets.isVisible(WindowInsets.Type.ime());
            Insets ime = insets.getInsets(WindowInsets.Type.ime());
            if (visible != lastReportedVisible) {
                lastReportedVisible = visible;
                session.shown = visible;
                log("IME insets visible=" + visible + " rect=" + ime.left + "," + ime.top
                        + "," + ime.right + "," + ime.bottom);
                sendImeState(visible, visible ? ime : null);
            } else if (visible) {
                // 可见期间几何还可能变（横竖屏、切换输入法、悬浮键盘）⇒ 变了就补报一次
                if (ime.left != lastImeRect[0] || ime.top != lastImeRect[1]
                        || ime.right != lastImeRect[2] || ime.bottom != lastImeRect[3]) {
                    sendImeState(true, ime);
                }
            }
            return insets;
        });

        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;

        params = new WindowManager.LayoutParams(
                1, 1, type,
                // 待命时不抢焦点
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.START;
        params.x = 0;
        params.y = 0;
        params.setTitle("aurora-ime-bridge");
        params.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN;

        try {
            windowManager.addView(editText, params);
            log("1x1 输入窗已创建（请求显示=" + displayId + " 实际显示=" + resolvedDisplayId + "）");
            send("WINDOW requestDisplay=" + displayId + " actualDisplay=" + resolvedDisplayId + " ok");
        } catch (Throwable t) {
            log("添加窗口失败: " + t);
            send("WINDOW display=" + displayId + " FAILED " + t);
            editText = null;
        }
    }

    private void removeInputWindow() {
        if (editText != null) {
            try {
                windowManager.removeViewImmediate(editText);
            } catch (Throwable ignored) {
            }
            editText = null;
        }
    }

    /** 让 1x1 窗取得焦点并唤起键盘。 */
    private void showKeyboard() {
        handler.post(() -> {
            if (editText == null) {
                log("显示键盘失败: 窗口未就绪");
                send("SHOWFAIL 窗口未就绪");
                return;
            }
            // 1. 移除 NOT_FOCUSABLE，让窗口能接收输入法焦点；同时把
            //    SOFT_INPUT_STATE_ALWAYS_HIDDEN 换成 UNSPECIFIED。
            //    待命态带着 ALWAYS_HIDDEN 时，即使 showSoftInput 被受理，
            //    WMS/IMMS 也会按「本窗口要求隐藏」处理。
            params.flags &= ~WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            params.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                    | WindowManager.LayoutParams.SOFT_INPUT_STATE_UNSPECIFIED;
            try {
                windowManager.updateViewLayout(editText, params);
            } catch (Throwable t) {
                log("updateViewLayout 失败: " + t);
            }
            // 2. 请求焦点
            editText.setFocusableInTouchMode(true);
            editText.setFocusable(true);
            editText.requestFocus();
            showAttempts = 0;
            handler.postDelayed(showRetry, 50);
        });
    }

    /**
     * 反复请求直到「窗口真的拿到焦点」且键盘真的可见。
     * 不能只调一次 showSoftInput —— 焦点事件、IMM 的连接建立都是异步的。
     */
    private void showRetry() {
        if (editText == null || hideRequested) return;
        if (!editText.hasWindowFocus()) {
            if (++showAttempts < 60) handler.postDelayed(showRetry, 32);
            else log("等待窗口焦点超时（60 次）");
            return;
        }
        try {
            if (!imm.isActive(editText)) imm.restartInput(editText);
            boolean requested = imm.showSoftInput(editText, InputMethodManager.SHOW_IMPLICIT);
            if (lastReportedVisible) {
                log("键盘已可见（第 " + showAttempts + " 次请求）");
                send("SHOWVISIBLE " + showAttempts);
                return;
            }
            if (showAttempts % 10 == 0 || showAttempts < 3) {
                log("showSoftInput=" + requested + " 尝试 " + showAttempts
                        + " hasWindowFocus=" + editText.hasWindowFocus());
            }
            send("SHOWREQUEST attempt=" + showAttempts + " returned=" + (requested ? 1 : 0)
                    + " visible=" + (lastReportedVisible ? 1 : 0));
        } catch (Throwable t) {
            log("showSoftInput 失败: " + t);
            send("SHOWFAIL " + t);
            return;
        }
        if (++showAttempts < 60) handler.postDelayed(showRetry, 32);
        else send("SHOWGIVEUP 60 次请求后键盘仍不可见");
    }

    /** 收起键盘并把焦点还给前台应用。 */
    private void hideKeyboard() {
        handler.post(() -> {
            hideRequested = true;
            handler.removeCallbacks(showRetry);
            if (editText == null) return;
            try {
                imm.hideSoftInputFromWindow(editText.getWindowToken(), 0);
            } catch (Throwable ignored) {
            }
            // 恢复 NOT_FOCUSABLE + ALWAYS_HIDDEN，焦点交还前台
            params.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            params.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                    | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN;
            try {
                windowManager.updateViewLayout(editText, params);
            } catch (Throwable ignored) {
            }
            editText.clearFocus();
            session.shown = false;
            lastReportedVisible = false;
            // 收起时把矩形清零：宿主据此恢复"不挖洞、不避让"的常态布局。
            sendImeState(false, null);
            send("HIDDEN " + session.id);
            log("已收起键盘");
        });
    }

    // -----------------------------------------------------------------------
    // 文本同步
    // -----------------------------------------------------------------------
    /** C++ 用 UTF-8 字节偏移，Android 用 UTF-16 偏移。不做换算中文会错位。 */
    private static int utf8ToUtf16(String s, int utf8Offset) {
        if (utf8Offset <= 0) return 0;
        byte[] bytes = s.getBytes(StandardCharsets.UTF_8);
        if (utf8Offset > bytes.length) utf8Offset = bytes.length;
        int len = 0;
        int i = 0;
        while (i < utf8Offset) {
            int b = bytes[i] & 0xFF;
            int size = (b < 0x80) ? 1 : (b < 0xE0) ? 2 : (b < 0xF0) ? 3 : 4;
            i += size;
            len += (size == 4) ? 2 : 1;   // 4 字节 UTF-8 是代理对，占 2 个 UTF-16
        }
        return len;
    }

    private static int utf16ToUtf8(String s, int utf16Offset) {
        if (utf16Offset <= 0) return 0;
        if (utf16Offset > s.length()) utf16Offset = s.length();
        return s.substring(0, utf16Offset).getBytes(StandardCharsets.UTF_8).length;
    }

    private void applySync(int sid, String text, int startUtf8, int endUtf8, int flags) {
        handler.post(() -> {
            if (editText == null) return;
            session.id = sid;

            int start = utf8ToUtf16(text, startUtf8);
            int end = utf8ToUtf16(text, endUtf8);

            ignoreNextChange = true;
            editText.setText("");
            editText.append(text);
            final int n = editText.getText().length();
            int a = Math.min(start, n);
            int b = Math.min(end, n);
            Selection.setSelection(editText.getText(), a, b);

            int t = InputType.TYPE_CLASS_TEXT;
            if ((flags & 1) != 0) t |= InputType.TYPE_TEXT_FLAG_MULTI_LINE;
            editText.setInputType(t);

            lastSentText = editText.getText().toString();
            log("SYNC 会话 " + sid + " 长度 " + n + " 选区 " + a + ".." + b);
        });
    }

    /**
     * 取"**已经上屏**"的文本，排除输入法正在拼的那一段（composing text）。
     *
     * ★★ 为什么必须排除（用户原话："使用体验怪怪的"）：
     *   打拼音时 EditText 里先是 "nihao"（composing，候选还没上屏），选词之后才变成 "你好"。
     *   如果把 composing 一起回传，宿主会在输入框里先看到一串拼音字母、再被替换成汉字
     *   （而且光标的"已提交位置"也对不上）—— 键入过程中界面一直在抖。
     *   Android 的 composing 区间由 InputConnection 维护，用
     *   {@link BaseInputConnection#getComposingSpanStart/End} 取；拿不到时退化为整段。
     */
    private String committedText(Editable ed) {
        try {
            int cs = BaseInputConnection.getComposingSpanStart(ed);
            int ce = BaseInputConnection.getComposingSpanEnd(ed);
            if (cs < 0 || ce < 0 || ce <= cs || cs > ed.length()) return ed.toString();
            if (ce > ed.length()) ce = ed.length();
            return ed.subSequence(0, cs).toString() + ed.subSequence(ce, ed.length()).toString();
        } catch (Throwable t) {
            return ed.toString();
        }
    }

    /** 把当前 EditText 内容作为 EDIT 消息回给 C++。 */
    private void reportEdit() {
        handler.post(() -> {
            if (editText == null) return;
            Editable ed = editText.getText();
            String text = committedText(ed);
            if (text.equals(lastSentText)) return;   // 无变化不发（含"只有拼音在变"的情况）
            lastSentText = text;
            // 光标：composing 区间在光标之后时要把它折掉，否则偏移会越过已提交文本
            int a = Selection.getSelectionStart(ed);
            int b = Selection.getSelectionEnd(ed);
            int cs = BaseInputConnection.getComposingSpanStart(ed);
            if (cs > 0 && a > cs) a -= Math.min(a, ed.length()) - cs;
            if (cs > 0 && b > cs) b -= Math.min(b, ed.length()) - cs;
            if (a < 0) a = 0;
            if (b < 0) b = text.length();
            if (a > text.length()) a = text.length();
            if (b > text.length()) b = text.length();
            send("EDIT " + session.id + " " + utf16ToUtf8(text, a) + " "
                    + utf16ToUtf8(text, b) + " " + b64(text));
        });
    }

    private void scheduleEditReport() {
        if (ignoreNextChange) {
            ignoreNextChange = false;
            return;
        }
        if (reportScheduled) return;
        reportScheduled = true;
        handler.postDelayed(() -> {
            reportScheduled = false;
            reportEdit();
        }, REPORT_DELAY_MS);
    }

    /** ime == null ⇒ 键盘已不可见，矩形清零（宿主恢复常态布局）。 */
    private void sendImeState(boolean visible, Insets ime) {
        lastImeRect[0] = ime != null ? ime.left : 0;
        lastImeRect[1] = ime != null ? ime.top : 0;
        lastImeRect[2] = ime != null ? ime.right : 0;
        lastImeRect[3] = ime != null ? ime.bottom : 0;
        send("IME " + session.id + " " + (visible ? 1 : 0)
                + " " + lastImeRect[0] + " " + lastImeRect[1]
                + " " + lastImeRect[2] + " " + lastImeRect[3]);
    }

    // -----------------------------------------------------------------------
    // TCP 协议（本服务是客户端）
    // -----------------------------------------------------------------------
    private static String b64(String s) {
        return Base64.encodeToString(s.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP);
    }

    private static String unb64(String s) {
        try {
            return new String(Base64.decode(s, Base64.NO_WRAP), StandardCharsets.UTF_8);
        } catch (Throwable t) {
            return "";
        }
    }

    private void send(String line) {
        // 绝不在这里直接写 socket：本方法会被主线程调用（WindowInsets 回调、
        // Handler.post 里的 SYNC/SHOW 处理），在主线程做网络 IO 会触发
        // StrictMode 的 NetworkOnMainThreadException 直接把进程干掉。
        // 统一入队，由 aurora-ime-writer 线程写出。
        outQueue.offer(line);
    }

    private void connectLoop() {
        try {
            client = new Socket(InetAddress.getByName("127.0.0.1"), port);
            client.setTcpNoDelay(true);
            in = new BufferedReader(
                    new InputStreamReader(client.getInputStream(), StandardCharsets.UTF_8));
            out = new PrintWriter(client.getOutputStream(), true);
            log("已连接 C++ 127.0.0.1:" + port);

            // 握手必须在写线程启动之前完成。
            // 否则队列里已经排着的消息（例如 createInputWindow 发的 WINDOW）
            // 会先于 READY 发出去，C++ 侧会把首行误判成「没有 READY」。
            String hello = in.readLine();
            if (hello == null || !hello.equals("HELLO " + token)) {
                log("令牌校验失败，断开: " + hello);
                closeQuietly();
                return;
            }
            out.println("READY");
            out.flush();

            writerRunning = true;
            Thread writer = new Thread(this::writerLoop, "aurora-ime-writer");
            writer.setDaemon(true);
            writer.start();
            log("令牌校验通过，进入协议循环");

            String line;
            while (running.get() && (line = in.readLine()) != null) {
                final String msg = line;
                handler.post(() -> handleLine(msg));
            }
            log("连接结束");
        } catch (Throwable t) {
            if (running.get()) log("IO 线程结束: " + t);
        } finally {
            closeQuietly();
        }
    }

    /** 唯一的 socket 写出线程。 */
    private void writerLoop() {
        try {
            while (writerRunning) {
                String line = outQueue.poll(200, java.util.concurrent.TimeUnit.MILLISECONDS);
                if (line == null) continue;
                PrintWriter w = out;
                if (w == null) continue;
                w.println(line);
                w.flush();
            }
        } catch (Throwable ignored) {
        }
    }

    private void handleLine(String line) {
        final String[] p = line.split(" ", 7);
        if (p.length == 0 || p[0].isEmpty()) return;
        switch (p[0]) {
            case "SYNC": {
                // SYNC session start end flags capacity text
                if (p.length < 7) {
                    log("SYNC 参数不足: " + p.length);
                    return;
                }
                applySync(Integer.parseInt(p[1]), unb64(p[6]),
                        Integer.parseInt(p[2]), Integer.parseInt(p[3]),
                        Integer.parseInt(p[4]));
                break;
            }
            case "SHOW": {
                if (p.length > 1) session.id = Integer.parseInt(p[1]);
                hideRequested = false;
                showKeyboard();
                break;
            }
            case "HIDE":
                hideKeyboard();
                break;
            case "GETCLIP": {
                handler.post(() -> {
                    String t = "";
                    try {
                        ClipData c = clipboard.getPrimaryClip();
                        if (c != null && c.getItemCount() > 0) {
                            CharSequence cs = c.getItemAt(0).coerceToText(this);
                            if (cs != null) t = cs.toString();
                        }
                    } catch (Throwable ignored) {
                    }
                    send("CLIP " + session.id + " " + b64(t));
                });
                break;
            }
            case "SETCLIP": {
                final String t = p.length > 1 ? unb64(p[1]) : "";
                handler.post(() -> {
                    try {
                        clipboard.setPrimaryClip(ClipData.newPlainText("aurora", t));
                    } catch (Throwable ignored) {
                    }
                });
                break;
            }
            case "QUIT":
                log("收到 QUIT");
                running.set(false);
                closeQuietly();
                handler.post(() -> {
                    try {
                        stopForeground(true);
                    } catch (Throwable ignored) {
                    }
                    stopSelf();
                });
                break;
            default:
                log("未知命令: " + p[0]);
        }
    }

    private void closeQuietly() {
        // 同样不能在主线程关 socket，统一丢到后台线程。
        writerRunning = false;
        final BufferedReader r = in;
        final PrintWriter w = out;
        final Socket c = client;
        in = null;
        out = null;
        client = null;
        Thread t = new Thread(() -> {
            try { if (r != null) r.close(); } catch (Throwable ignored) {}
            try { if (w != null) w.close(); } catch (Throwable ignored) {}
            try { if (c != null) c.close(); } catch (Throwable ignored) {}
        }, "aurora-ime-close");
        t.setDaemon(true);
        t.start();
    }

    private static void log(String msg) {
        android.util.Log.i(TAG, msg);
        System.err.println("aurora-ime " + msg);
    }
}
