package com.go2.remote;

// 网页界面外壳：一个纯 WebView 的 Activity，指向本进程内 C++ 起的本机服务（127.0.0.1:8123）。
//
// 为什么这么做：界面用 Vue3 写（渐变/阴影/模糊/过渡在 CSS 里就是几行，ImGui 里要手画几十个 draw call），
// 协议 / 加密 / 钥匙库 / 运动指令表仍全部留在 C++（见 ui/web_bridge.cpp）—— 只换界面，不碰协议。
// WebView 与 C++ 在同一进程、同一台设备，127.0.0.1 天然可达，不对外暴露。
//
// 用法：native 侧 go2::startWebUi() 起服务后调用 open(ctx, url)（见 main_android.cpp）。
// 返回键 / 返回手势直接 finish() —— 回到原来的 SDL(ImGui) 界面，两套界面共存，随时可切。

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.ViewGroup;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

public class WebUiActivity extends Activity {
    private WebView web;
    private String url = "http://127.0.0.1:8123/";
    private int retries = 0;   // 首屏加载失败自动重试（服务可能比 WebView 晚一拍）

    /// 网页界面是否在前台。SDL 界面切后台的"停车并断开"逻辑要看这个标志：
    /// WebView 盖在 SDL 界面上时，**不算切后台**（连接要保持，网页界面才能控制狗）
    private static volatile boolean sShowing = false;

    /// native 侧查询（JNI）
    public static boolean isShowing() { return sShowing; }

    /// 从 SDL 线程调用也安全：post 到主线程再 startActivity（UI 操作必须在主线程）
    public static void open(final Context ctx, final String url) {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                Intent i = new Intent(ctx, WebUiActivity.class);
                i.putExtra("url", url);
                i.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
                ctx.startActivity(i);
            }
        });
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);          // Vue3 免构建运行需要
        s.setDomStorageEnabled(true);
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);  // 改前端后不用清缓存
        s.setLoadWithOverviewMode(false);
        s.setUseWideViewPort(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        web.setWebViewClient(new WebViewClient() {  // 别跳到外部浏览器
            @Override
            public void onReceivedError(WebView v, android.webkit.WebResourceRequest req,
                                        android.webkit.WebResourceError err) {
                // 主文档加载失败（多半是服务还没 bind 上）→ 稍等重试，最多 25 次 ≈ 10 秒
                if (req.isForMainFrame() && retries++ < 25) {
                    v.postDelayed(() -> v.loadUrl(url), 400);
                }
            }
        });
        setContentView(web, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        String u = getIntent().getStringExtra("url");
        if (u != null && !u.isEmpty()) url = u;
        web.loadUrl(url);
    }

    @Override
    protected void onResume() {
        super.onResume();
        sShowing = true;
    }

    @Override
    protected void onPause() {
        sShowing = false;
        super.onPause();
    }

    @Override
    public void onBackPressed() {
        finish();   // 回到 ImGui 界面
    }

    @Override
    protected void onDestroy() {
        if (web != null) { web.destroy(); web = null; }
        super.onDestroy();
    }
}
