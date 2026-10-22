#include "show_engine.hpp"

#include "command_service.hpp"
#include "sport_library.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace go2 {

namespace {

/// 按 key 在动作表里查一条动作。
const SportAction* findAction(const std::string& key) {
    if (key.empty()) return nullptr;
    for (const auto& a : sportActions()) {
        if (key == a.key) return &a;
    }
    return nullptr;
}

/// 角色匹配：roles 里可以写设备键（sn:/ip: 前缀），也可以直接写裸 IP。
///
/// ★ 为什么两种都认：编队指派用的是**设备键**（跨网络稳定），
///   但用户手写 cue 时十有八九直接粘 IP。两种都认，少一个"填了却不生效"的坑。
bool roleMatches(const std::vector<std::string>& roles, const std::string& key,
                 const std::string& ip) {
    if (roles.empty()) return true;
    for (const auto& r : roles) {
        if (r == key) return true;
        if (!ip.empty() && r == ip) return true;
    }
    return false;
}

long long nowEpochNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// 让出 CPU 的短暂等待（位移 worker 的节拍）
void nap(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

}  // namespace

// ---------------------------------------------------------------- LightSink

void LoggingLightSink::fireCue(const std::string& lightId, const std::vector<std::string>& roles) {
    {
        std::lock_guard<std::mutex> lk(m_);
        events_.push_back(Event{lightId, roles, nowEpochNs() / 1000000});
        if (events_.size() > 64) events_.erase(events_.begin());
    }
    if (ui_) {
        std::string who = roles.empty() ? std::string("全体") : std::string();
        for (size_t i = 0; i < roles.size(); ++i) {
            if (i) who += ",";
            who += roles[i];
        }
        ui_->addLog("[演出][灯光] " + lightId + (who.empty() ? "" : " → " + who));
    }
}

std::vector<LoggingLightSink::Event> LoggingLightSink::history() const {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<Event> out(events_.rbegin(), events_.rend());
    return out;
}

// ---------------------------------------------------------------- ShowEngine

ShowEngine::ShowEngine(std::shared_ptr<CommandSink> sink, UiState& ui) : sink_(std::move(sink)), ui_(ui) {
    light_ = std::make_shared<LoggingLightSink>(&ui);
}

ShowEngine::~ShowEngine() {
    stop();
}

void ShowEngine::setLightSink(std::shared_ptr<LightSink> s) {
    if (s) light_ = s;
}

bool ShowEngine::alive(unsigned gen) const {
    if (stopRequested_.load()) return false;
    if (ui_.estop.load()) return false;
    return ui_.scriptGen.load() == gen;
}

bool ShowEngine::waitUntil(const std::chrono::steady_clock::time_point& when, unsigned gen) {
    constexpr int kChunk = 25;  // 25ms 一片：急停/中止响应够快，又不至于空转
    for (;;) {
        if (!alive(gen)) return false;
        const auto now = std::chrono::steady_clock::now();
        if (now >= when) return true;
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(when - now).count();
        nap(static_cast<int>(std::min<long long>(kChunk, std::max<long long>(1, remain))));
    }
}

std::vector<std::string> ShowEngine::resolveTargets(const ShowCue& cue,
                                                     const std::vector<std::string>& ips) {
    const auto ready = sink_->readyIps();
    std::vector<std::string> out;
    for (const auto& ip : ips) {
        if (std::find(ready.begin(), ready.end(), ip) == ready.end()) continue;
        const std::string key = ui_.deviceKeyFor(ip);
        if (!roleMatches(cue.roles, key, ip)) continue;
        out.push_back(ip);
    }
    return out;
}

std::vector<std::pair<std::string, long long>> ShowEngine::parallelSend(
    const std::vector<std::string>& ips, int apiId, const nlohmann::json& param) {
    std::vector<std::pair<std::string, long long>> out;
    if (ips.empty() || apiId == 0) return out;

    std::mutex m;
    std::vector<std::thread> ths;
    ths.reserve(ips.size());
    for (const auto& ip : ips) {
        ths.emplace_back([this, &m, &out, ip, apiId, param] {
            // 取"准备写出去"的时刻作为发送时刻 —— 这是错位度量里最有意义的那个点。
            const long long at = nowEpochNs();
            sink_->sendSport(ip, apiId, param);
            std::lock_guard<std::mutex> lk(m);
            out.emplace_back(ip, at);
        });
    }
    for (auto& t : ths) t.join();  // 等全部发完再算错位
    std::sort(out.begin(), out.end());
    return out;
}

bool ShowEngine::start(const Show& show, const std::vector<std::string>& ips) {
    // ★ 先把上一场的线程收干净，再开新的。
    //   std::thread 的赋值运算符在目标线程仍 joinable 时会直接 std::terminate ——
    //   而上一场演完时调度线程**自己退出了**（running_ 已为 false），
    //   但它没被 join 过 → 这时 start() 再赋值就是直接崩进程。
    //   （`running_` 那道检查拦不住这种情况：它是"跑完了但没 join"。）
    joinWorkers();

    if (ui_.estop.load()) {
        std::lock_guard<std::mutex> lk(metricsMutex_);
        lastError_ = "急停锁定中，不能开演";
        return false;
    }
    if (show.cues.empty()) {
        std::lock_guard<std::mutex> lk(metricsMutex_);
        lastError_ = "时间轴是空的";
        return false;
    }
    if (running_.load()) {
        std::lock_guard<std::mutex> lk(metricsMutex_);
        lastError_ = "上一场演出还在跑（先停止它）";
        return false;
    }

    // 参与集合：调用方给了就用它；没给就用当前「受控且就绪」的集合。
    std::vector<std::string> targets = ips;
    if (targets.empty()) targets = sink_->selectedReadyIps();
    if (targets.empty()) {
        std::lock_guard<std::mutex> lk(metricsMutex_);
        lastError_ = "没有可用的机器狗（受控集合为空，或都没就绪）";
        return false;
    }

    // 开演前的静态校验：动作 key 不存在 / 高危动作 / 位移超长 —— 必须在开演前拦住。
    const auto problems = validateShow(show);
    if (!problems.empty()) {
        std::ostringstream os;
        os << "演出校验未通过：" << problems.front();
        if (problems.size() > 1) os << "（共 " << problems.size() << " 个问题）";
        {
            std::lock_guard<std::mutex> lk(metricsMutex_);
            lastError_ = os.str();
        }
        for (const auto& p : problems) ui_.addLog("[演出][校验] " + p);
        return false;
    }

    // 复用 UiState 的代际号：急停 / abortScript 自增它 → 引擎立刻察觉并收尾。
    const unsigned gen = ui_.scriptGen.fetch_add(1) + 1;

    stopRequested_.store(false);
    {
        std::lock_guard<std::mutex> lk(metricsMutex_);
        metrics_ = ShowSyncMetrics{};
        metrics_.cuesTotal = static_cast<int>(show.cues.size());
        metrics_.robots = static_cast<int>(targets.size());
        skewSamples_ = 0;
        lastError_.clear();
        showName_ = show.name;
    }
    ui_.scriptRunning.store(true);
    ui_.setScriptName("演出：" + show.name);

    running_.store(true);
    worker_ = std::thread([this, show, targets, gen] { runShow(show, targets, gen); });
    return true;
}

void ShowEngine::runShow(const Show& show, const std::vector<std::string>& ips, unsigned gen) {
    // 起点：现在 + 预备时间。预备时间给操作员一个"准备"窗口，
    // 也让第一条 cue 不会因为"刚连上还没稳"就被抢跑。
    t0_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(kStartLeadMs);
    t0EpochMs_.store(nowEpochNs() / 1000000);

    ui_.addLog("[演出] 「" + show.name + "」开始：" + std::to_string(show.bpm) + " BPM、" +
               std::to_string(show.beats) + " 拍、" + std::to_string(ips.size()) + " 台参与");

    // 按绝对时间排序（cue 在文件里未必有序）
    std::vector<ShowCue> cues = show.cues;
    std::stable_sort(cues.begin(), cues.end(),
                     [&](const ShowCue& a, const ShowCue& b) { return a.beat < b.beat; });

    for (const auto& cue : cues) {
        const double startSec = cueStartSec(show, cue);
        const auto when = t0_ + std::chrono::milliseconds(static_cast<long long>(startSec * 1000.0));
        if (!waitUntil(when, gen)) {
            ui_.addLog("[演出] 已中止");
            break;
        }
        // 迟到的 cue：调度被拖慢了（机器卡 / 线程被抢占）。记下来 —— 不记就看不出"没同步"。
        const double lateMs =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - when)
                .count() /
            1000.0;
        if (lateMs > kLateToleranceMs) {
            std::lock_guard<std::mutex> lk(metricsMutex_);
            metrics_.lateCues++;
            metrics_.maxLatenessMs = std::max(metrics_.maxLatenessMs, lateMs);
        }
        fireCue(cue, ips, when);
    }

    // 收尾：给所有参与的车补一条停车（速度是保持型的）。
    stopAll(ips);

    if (ui_.scriptGen.load() == gen) {  // 只有"还是当前脚本"才复位标志
        ui_.scriptRunning.store(false);
        ui_.setScriptName("");
    }
    running_.store(false);

    std::lock_guard<std::mutex> lk(metricsMutex_);
    std::ostringstream os;
    os << "[演出] 「" << show.name << "」结束：触发 " << metrics_.cuesFired << "/"
       << metrics_.cuesTotal << " 条，最大错位 " << metrics_.maxSkewMs << " ms，平均错位 "
       << metrics_.avgSkewMs << " ms";
    if (metrics_.lateCues) os << "，迟到 " << metrics_.lateCues << " 条";
    ui_.addLog(os.str());
}

void ShowEngine::fireCue(const ShowCue& cue, const std::vector<std::string>& ips,
                         const std::chrono::steady_clock::time_point& when) {
    const auto targets = resolveTargets(cue, ips);
    if (targets.empty()) {
        std::lock_guard<std::mutex> lk(metricsMutex_);
        metrics_.cuesFired++;
        return;  // 没有目标就当空放（不算错，只是这一拍没人）
    }

    switch (cue.kind) {
        case CueKind::Action: {
            if (cue.actionKey.empty() || cue.actionKey == "wait") break;
            const SportAction* action = findAction(cue.actionKey);
            if (!action) {
                ui_.addLog("[演出] 未知动作：" + cue.actionKey);
                break;
            }
            const int apiId = cmd::resolveApiId(ui_, *action);
            if (apiId == 0) {
                ui_.addLog("[演出] 当前指令集里没有「" + cue.actionKey + "」这条");
                break;
            }
            const nlohmann::json param = cmd::packParam(ui_, *action, true);

            // ★ 先停掉正在走的位移：四足不能一边走一边做动作（空翻更会摔）。
            for (const auto& ip : targets) sink_->stopMove(ip);

            const auto sent = parallelSend(targets, apiId, param);

            // 错位度量：同一条 cue 内，各狗"写出去的时刻"的最大差。
            if (!sent.empty()) {
                long long mn = sent.front().second, mx = sent.front().second;
                for (const auto& s : sent) {
                    mn = std::min(mn, s.second);
                    mx = std::max(mx, s.second);
                }
                const double skewMs = static_cast<double>(mx - mn) / 1e6;
                std::lock_guard<std::mutex> lk(metricsMutex_);
                metrics_.maxSkewMs = std::max(metrics_.maxSkewMs, skewMs);
                metrics_.avgSkewMs += skewMs;
                ++skewSamples_;
            }

            // 持续模式（开关型）动作：记下开过的开关，演出结束/中止时逐个关掉。
            if (action->toggle) {
                ui_.setToggle(cue.actionKey, true);
                ui_.setToggleActive(apiId, true);
            }
            break;
        }
        case CueKind::Move:
            startMove(cue, ips, when);
            break;
        case CueKind::Light: {
            std::vector<std::string> roles;
            for (const auto& ip : targets) roles.push_back(ui_.deviceKeyFor(ip));
            if (light_) light_->fireCue(cue.lightId, roles);
            break;
        }
    }

    std::lock_guard<std::mutex> lk(metricsMutex_);
    metrics_.cuesFired++;
}

void ShowEngine::startMove(const ShowCue& cue, const std::vector<std::string>& ips,
                           const std::chrono::steady_clock::time_point& when) {
    const auto targets = resolveTargets(cue, ips);
    if (targets.empty()) return;

    const double translateSec = moveTranslateSec(cue.move);
    const double turnSec = moveTurnSec(cue.move);
    if (translateSec + turnSec <= 1e-6) return;

    float vx = 0, vy = 0, vz = 0;
    moveVelocity(cue.move, 0, &vx, &vy, &vz);
    float tz = 0;
    moveVelocity(cue.move, 1, nullptr, nullptr, &tz);

    const unsigned gen = ui_.scriptGen.load();
    // ★ 位移的绝对起止时刻必须从 **cue 的绝对时间**算，不能从"worker 起来的时刻"算 ——
    //   否则 worker 晚起 30ms，这段位移就整体晚 30ms，与后面的 cue 错开。
    for (const auto& ip : targets) {
        std::lock_guard<std::mutex> lk(moversMutex_);
        movers_.emplace_back([this, ip, gen, when, translateSec, turnSec, vx, vy, vz, tz] {
            const auto phase0End = when + std::chrono::milliseconds(
                                             static_cast<long long>(translateSec * 1000.0));
            const auto phase1End = phase0End + std::chrono::milliseconds(
                                               static_cast<long long>(turnSec * 1000.0));

            // ① 平移段
            while (std::chrono::steady_clock::now() < phase0End) {
                // ★ 中止时必须**补一条停车**再退出：急停的 StopMove 与这里的补发之间
                //   可能差一个节拍（100ms），这段时间里狗会继续按上一个速度走。
                //   少这一条，"急停后狗还在滑行"就是必然的。
                if (!alive(gen)) {
                    sink_->stopMove(ip);
                    return;
                }
                nlohmann::json p = nlohmann::json::object();
                p["x"] = vx;
                p["y"] = vy;
                p["z"] = 0.0f;
                sink_->sendSport(ip, kApiMove, p);
                nap(kMoveTickMs);
            }
            // ② 转向段
            while (std::chrono::steady_clock::now() < phase1End) {
                if (!alive(gen)) {
                    sink_->stopMove(ip);
                    return;
                }
                nlohmann::json p = nlohmann::json::object();
                p["x"] = 0.0f;
                p["y"] = 0.0f;
                p["z"] = tz;
                sink_->sendSport(ip, kApiMove, p);
                nap(kMoveTickMs);
            }
            // ③ 收尾：速度是保持型的，必须显式停车
            sink_->stopMove(ip);
        });
    }
}

void ShowEngine::stopAll(const std::vector<std::string>& ips) {
    for (const auto& ip : ips) sink_->stopMove(ip);
}

void ShowEngine::stop() {
    stopRequested_.store(true);
    // 自增代际号 → 调度线程与位移 worker 立刻察觉并收尾。
    // （不直接用 ui_.scriptGen 自增：那是 UiState 的语义，引擎不该替界面改它；
    //   这里让 abortScript 去改，引擎只负责停自己。）
    joinWorkers();
    running_.store(false);
}

void ShowEngine::joinWorkers() {
    // 顺序：先让 worker 知道要停（stopRequested_ 已在 stop() 里置位），
    // 再 join。**绝不在持锁状态下 join** —— 被 join 的线程可能反过来要拿同一把锁。
    if (worker_.joinable()) worker_.join();
    std::vector<std::thread> tmp;
    {
        std::lock_guard<std::mutex> lk(moversMutex_);
        tmp.swap(movers_);
    }
    for (auto& t : tmp) {
        if (t.joinable()) t.join();
    }
}

bool ShowEngine::running() const { return running_.load(); }

ShowSyncMetrics ShowEngine::metrics() const {
    std::lock_guard<std::mutex> lk(metricsMutex_);
    ShowSyncMetrics m = metrics_;
    // 平均错位按**真正量到错位的 cue 数**算（不是 cuesFired）——
    // 没有目标的那一拍不产生错位样本，混进去会把平均值稀释得好看但失真。
    if (skewSamples_ > 0) m.avgSkewMs /= static_cast<double>(skewSamples_);
    return m;
}

std::string ShowEngine::lastError() const {
    std::lock_guard<std::mutex> lk(metricsMutex_);
    return lastError_;
}

std::string ShowEngine::currentShowName() const {
    std::lock_guard<std::mutex> lk(metricsMutex_);
    return showName_;
}

std::shared_ptr<ShowEngine> globalShowEngine(RobotManager& mgr, UiState& ui) {
    static std::mutex m;
    static std::shared_ptr<ShowEngine> inst;
    std::lock_guard<std::mutex> lk(m);
    if (!inst) inst = std::make_shared<ShowEngine>(std::make_shared<ManagerSink>(mgr, ui), ui);
    return inst;
}

}  // namespace go2
