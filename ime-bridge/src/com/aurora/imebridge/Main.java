package com.aurora.imebridge;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Looper;
import android.os.Process;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;

/**
 * 由 C++ 通过 root app_process 启动的入口类。
 *
 * 启动方式（C++ 侧）：
 *   app_process -Djava.class.path=<apk> /system/bin com.aurora.imebridge.Main \
 *               --port <p> --token <t> --apk <path> [--display <id>]
 *
 * 为什么需要这个类而不是直接起 Service：
 *   Service 必须由系统通过已安装的包来启动，而 app_process 起的进程只是一个
 *   「有 Android 类库的普通进程」——它不在 AMS 的 mProcessMap 里，所以拿不到
 *   WMS 窗口（Session.java:142-148 的 PID 门禁）。这里做三件事：
 *     1. 把内嵌 APK 安装成临时应用（pm install，与参考实现一致）
 *     2. 授予 SYSTEM_ALERT_WINDOW AppOp 并给 10 秒临时后台启动许可
 *     3. 通过 am start-service 让系统启动那个应用里的 AuroraImeService
 *   之后 Service 拥有真正的应用身份，能创建窗口、能拿输入法焦点。
 *
 * 进程生命周期：native 父进程持有本进程 stdin 的写端。父进程关闭该 pipe 时
 * 本进程读到 EOF → 卸载辅助包 → 退出。这样清理一定跑得到。
 */
public final class Main {

    private static final String TAG = "AuroraIme";

    /**
     * 辅助包的包名。必须硬编码。
     *
     * 不能写成 ctx.getPackageName()：app_process 起的进程里，
     * 系统 Context 的包名是 "android"，不是辅助包。用错会启动不到 Service。
     */
    static final String AUX_PACKAGE = "com.aurora.imebridge";

    public static void main(String[] args) {
        int port = 0;
        int display = 0;
        String token = "";
        String apkPath = null;
        try {
            for (int i = 0; i < args.length; i++) {
                if ("--port".equals(args[i]) && i + 1 < args.length) {
                    port = Integer.parseInt(args[++i]);
                } else if ("--token".equals(args[i]) && i + 1 < args.length) {
                    token = args[++i];
                } else if ("--apk".equals(args[i]) && i + 1 < args.length) {
                    apkPath = args[++i];
                } else if ("--display".equals(args[i]) && i + 1 < args.length) {
                    display = Integer.parseInt(args[++i]);
                }
            }
        } catch (Exception e) {
            log("参数解析失败: " + e);
            System.exit(2);
        }

        if (port <= 0 || token.isEmpty() || apkPath == null) {
            log("用法: Main --port <port> --token <token> --apk <path> [--display <id>]");
            System.exit(2);
        }

        log("启动 pid=" + Process.myPid() + " uid=" + Process.myUid()
                + " port=" + port + " display=" + display + " apk=" + apkPath);

        // app_process 起的进程默认没有 Looper。窗口、输入法、Service 都要求
        // 主线程有 Looper，所以这里必须准备一个。
        if (Looper.myLooper() == null) {
            Looper.prepareMainLooper();
        }

        // native 父进程关闭 stdin ⇒ 卸载辅助包并退出。
        startParentWatchdog();

        Context ctx = getSystemContext();
        if (ctx == null) {
            log("无法获得系统 Context，退出");
            System.exit(3);
        }

        // ---- 1. 安装辅助包 ----
        // 用 pm install -S 从 stdin 流式喂字节：PackageInstallerService 以 system
        // 身份运行，读不到 /data/adb 下的 root 私有文件，必须由本进程（uid 0）喂。
        boolean installed = false;
        try {
            String r = run(apkPath, "/system/bin/pm", "install", "-r", "--user", "0",
                    "-S", Long.toString(new File(apkPath).length()));
            log("pm install => " + r.replace('\n', ' '));
            installed = r.contains("Success");
        } catch (Throwable t) {
            log("安装异常: " + t);
        }
        if (!installed) {
            // 已经装过（上次没清理干净）且签名一致时，后续仍可启动 Service。
            log("安装未返回 Success，尝试继续（可能已安装）");
        }

        // ---- 2. 悬浮窗 AppOp + 临时后台启动许可 ----
        // 没有 SYSTEM_ALERT_WINDOW，TYPE_APPLICATION_OVERLAY 会被 WMS 拒；
        // 同时它也是「允许后台启动 Service」的官方豁免条件之一。
        try {
            log("appops => " + run(null, "/system/bin/appops", "set", "--user", "0",
                    AUX_PACKAGE, "SYSTEM_ALERT_WINDOW", "allow").replace('\n', ' '));
        } catch (Throwable t) {
            log("appops 失败: " + t);
        }
        try {
            run(null, "/system/bin/cmd", "deviceidle", "tempwhitelist", "-u", "0",
                    "-d", "10000", AUX_PACKAGE);
        } catch (Throwable t) {
            log("tempwhitelist 失败: " + t);
        }

        // ---- 3. 让系统启动 Service ----
        try {
            String r = run(null, "/system/bin/am", "start-service", "--user", "0",
                    "-n", AUX_PACKAGE + "/.AuroraImeService",
                    "--ei", "port", Integer.toString(port),
                    "--es", "token", token,
                    "--ei", "display", Integer.toString(display));
            log("am start-service => " + r.replace('\n', ' '));
        } catch (Throwable t) {
            log("启动 Service 失败: " + t);
            System.exit(4);
        }

        log("已请求启动 Service，进入消息循环");
        Looper.loop();
    }

    /**
     * native 父进程持有我们 stdin 的写端：它一关闭（包括父进程被 SIGTERM/崩溃），
     * 这里就收到 EOF，于是卸载辅助包并退出。
     */
    private static void startParentWatchdog() {
        Thread t = new Thread(() -> {
            try {
                InputStream in = System.in;
                byte[] buf = new byte[256];
                while (in.read(buf) != -1) {
                    // 父进程不会往这里写东西，读到的都丢掉
                }
            } catch (Throwable ignored) {
            }
            log("父进程已退出（stdin EOF），卸载辅助包");
            try {
                log("pm uninstall => "
                        + run(null, "/system/bin/pm", "uninstall", AUX_PACKAGE).replace('\n', ' '));
            } catch (Throwable e) {
                log("卸载失败: " + e);
            }
            System.exit(0);
        }, "aurora-ime-parent");
        t.setDaemon(true);
        t.start();
    }

    /**
     * 取系统 Context。
     *
     * app_process 进程里没有 ActivityThread 的应用上下文，只能通过反射拿
     * ActivityThread.currentActivityThread().getSystemContext()。这是这类
     * 「无 APK 的 root Java 进程」的标准做法。
     */
    private static Context getSystemContext() {
        try {
            Class<?> atClass = Class.forName("android.app.ActivityThread");
            Object at = atClass.getMethod("currentActivityThread").invoke(null);
            if (at == null) {
                // app_process 还没初始化 ActivityThread，手工触发一次
                at = atClass.getMethod("systemMain").invoke(null);
            }
            return (Context) atClass.getMethod("getSystemContext").invoke(at);
        } catch (Throwable t) {
            log("getSystemContext 失败: " + t);
            return null;
        }
    }

    /** 跑一个外部命令；stdinPath 非空时把该文件内容喂给它的 stdin。 */
    private static String run(String stdinPath, String... args) throws Exception {
        // 注意：本文件 import 了 android.os.Process，所以 java.lang.Process 必须写全名。
        java.lang.Process process = new ProcessBuilder(args).redirectErrorStream(true).start();
        try (OutputStream stdin = process.getOutputStream()) {
            if (stdinPath != null) {
                try (InputStream file = new FileInputStream(stdinPath)) {
                    byte[] bytes = new byte[65536];
                    int count;
                    while ((count = file.read(bytes)) != -1) {
                        stdin.write(bytes, 0, count);
                    }
                }
            }
        }
        StringBuilder result = new StringBuilder();
        try (BufferedReader reader =
                     new BufferedReader(new InputStreamReader(process.getInputStream()))) {
            String line;
            while ((line = reader.readLine()) != null) {
                result.append(line).append('\n');
            }
        }
        int code = process.waitFor();
        String text = result.toString().trim();
        if (code != 0) {
            throw new IllegalStateException(args[0] + " 退出码 " + code + ": " + text);
        }
        return text;
    }

    private static void log(String msg) {
        System.err.println("aurora-ime " + msg);
    }

    private Main() {}
}
