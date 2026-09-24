package org.libsdl.app;

import android.content.Context;
import android.net.wifi.WifiManager;

/**
 * 应用主 Activity（SDL2 模板基类）。
 * 额外职责：持有 WifiManager.MulticastLock —— Android 默认丢弃组播包，
 * 不持锁时 SN 多播发现（231.1.1.1:10131）收不到任何回包。
 * 原生侧通过 SDL_AndroidGetActivity() 调用下面两个方法。
 */
public class MainActivity extends SDLActivity {
    private WifiManager.MulticastLock multicastLock = null;

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
}
