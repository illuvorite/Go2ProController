package org.libsdl.app;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;

/**
 * 应用主 Activity（SDL2 模板基类）。
 *
 * 额外职责（原生侧通过 SDL_AndroidGetActivity() 调用下面的方法）：
 *  1. 持有 WifiManager.MulticastLock —— Android 默认丢弃组播包，
 *     不持锁时 SN 多播发现（231.1.1.1:10131）收不到任何回包。
 *  2. 遥控时保持屏幕常亮（FLAG_KEEP_SCREEN_ON）—— 操作中息屏会断连，属安全项。
 *  3. 提供安全区（刘海 / 圆角 / 手势条）给原生侧做布局让位。
 *
 * 注意：**不要**在这里覆盖 decorView 的 OnApplyWindowInsetsListener ——
 * SDLActivity 用它做沉浸式全屏，覆盖会破坏全屏处理。
 * 所以安全区走"按需刷新 + 缓存"：原生侧需要时调 refreshSafeAreaInsets()，
 * 再读 getSafeAreaInsets()。
 */
public class MainActivity extends SDLActivity {
    private WifiManager.MulticastLock multicastLock = null;

    /** 安全区缓存，单位**物理像素**，顺序 [左, 上, 右, 下]。由 UI 线程写入。 */
    private volatile int[] safeInsets = new int[]{0, 0, 0, 0};

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL2", "main"};
    }

    /** 原生侧调用：申请组播锁 */
    public void acquireMulticastLock() {
        if (multicastLock != null) return;
        WifiManager wifi = (WifiManager) getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
        if (wifi == null) return;
        multicastLock = wifi.createMulticastLock("go2-multicast");
        multicastLock.setReferenceCounted(false);
        multicastLock.acquire();
    }

    /** 原生侧调用：释放组播锁 */
    public void releaseMulticastLock() {
        if (multicastLock == null) return;
        if (multicastLock.isHeld()) multicastLock.release();
        multicastLock = null;
    }

    /**
     * 原生侧调用：遥控时保持屏幕常亮。
     * 用 addFlags/clearFlags 而不是 setKeepScreenOn()，避免影响其它 view 的状态。
     */
    public void setKeepScreenOn(final boolean on) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (on) {
                    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                } else {
                    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                }
            }
        });
    }

    /**
     * 原生侧调用：刷新安全区缓存（转 UI 线程读 WindowInsets，结果存进 safeInsets）。
     * 屏幕旋转 / 尺寸变化后调一次即可 —— WindowInsets 只在配置变化时改变。
     */
    public void refreshSafeAreaInsets() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                View decor = getWindow().getDecorView();
                WindowInsets ins = decor.getRootWindowInsets();
                safeInsets = readInsets(ins);
            }
        });
    }

    /** 原生侧调用：取上次刷新的安全区（物理像素，[左, 上, 右, 下]） */
    public int[] getSafeAreaInsets() {
        return safeInsets;
    }

    /** 从 WindowInsets 里取「系统栏 + 屏幕缺口」的安全区 */
    private int[] readInsets(WindowInsets ins) {
        if (ins == null) return new int[]{0, 0, 0, 0};
        if (Build.VERSION.SDK_INT >= 30) {
            android.graphics.Insets b = ins.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            return new int[]{b.left, b.top, b.right, b.bottom};
        }
        // API 26–29 的老接口（已废弃但可用）
        return new int[]{
                ins.getSystemWindowInsetLeft(),
                ins.getSystemWindowInsetTop(),
                ins.getSystemWindowInsetRight(),
                ins.getSystemWindowInsetBottom()};
    }
}
