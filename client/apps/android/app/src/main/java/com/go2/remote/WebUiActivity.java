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

    /// ★ 网页界面**没被关掉**却离开了前台（按 Home / 锁屏 / 切到别的应用）时置位。
    /// MainActivity.onResume 会消费它 → 重新把 WebView 盖上来（用户要求：网页界面常驻）。
    /// 按**返回键**离开时不置位（isFinishing=true）—— 那是用户明确要回 ImGui 界面。
    private static volatile boolean sWentBackgrounded = false;

    /// native 侧查询（JNI）
    public static boolean isShowing() { return sShowing; }

    /// MainActivity.onResume 调用：网页界面是"被切后台"而非"被关掉"时，重新盖上来
    public static void reopenIfBackgrounded(Context ctx) {
        if (sWentBackgrounded) {
            sWentBackgrounded = false;
            open(ctx, null);   // url 为空 → 用 WebUiActivity 里记住的地址
        }
    }

    /// 从 SDL 线程调用也安全：post 到主线程再 startActivity（UI 操作必须在主线程）
    public static void open(final Context ctx, final String url) {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                Intent i = new Intent(ctx, WebUiActivity.class);
                if (url != null) i.putExtra("url", url);
                // ★ NEW_TASK：MainActivity 是 singleInstance（任务独占），WebView 开在**另一个任务**里。
                //   只用 REORDER_TO_FRONT 只会在 WebView 自己的任务内重排，**任务本身上不来** ——
                //   MainActivity 的任务还盖在上面，表现为"怎么都不回到网页界面"（踩过）。
                //   NEW_TASK 把 WebView 所在任务整个提到前台；SINGLE_TOP 复用已有实例不叠加。
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
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
        sWentBackgrounded = false;   // 已经回到网页界面了，别再让 MainActivity 重复开一次
    }

    @Override
    protected void onPause() {
        sShowing = false;
        super.onPause();
    }

    @Override
    protected void onStop() {
        // 没被 finish 却离开前台 = 按了 Home / 锁屏 / 切走 → 记下来，
        // 等 MainActivity.onResume 时把网页重新盖上来（用户要求网页界面常驻）
        if (!isFinishing()) sWentBackgrounded = true;
        super.onStop();
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
