#include "router.hpp"

namespace aossimple {

const char *to_string(Router::Result result) {
    switch (result) {
        case Router::Result::Routed: return "routed";
        case Router::Result::FellBack: return "fell-back";
        case Router::Result::NoRoute: return "no-route";
        case Router::Result::Closed: return "closed";
    }
    return "no-route";
}

Outcome DropArgv0Executor::run(const Call &call) {
    Call inner = call;
    if (!inner.argv.empty()) inner.argv.erase(inner.argv.begin());
    return inner_.run(inner);
}

void Router::route(std::string argv0, Sink &sink) {
    routes_[std::move(argv0)] = &sink;
}

void Router::fallback(Sink &sink) { fallback_ = &sink; }

Router::Decision Router::dispatch(const Call &call) {
    Decision decision;

    // argv 空的話沒有 argv[0] 可以看。★ 這裡不做形狀驗證——那是目的地
    //   Executor 的事（不同的 queue 對「什麼算合法」的看法本來就不一樣）。
    //   但沒有 argv[0] 就真的無從分流，只能走 fallback。
    Sink *target = nullptr;
    if (!call.argv.empty()) {
        const auto it = routes_.find(call.argv[0]);
        if (it != routes_.end()) {
            target = it->second;
            decision.result = Result::Routed;
            decision.destination = it->first;
        }
    }

    if (target == nullptr) {
        if (fallback_ == nullptr) return decision;  // NoRoute
        target = fallback_;
        decision.result = Result::FellBack;
        decision.destination = "*";
    }

    if (!target->push(call)) {
        // 目的地已經收工。★ 要講出來，不能安靜丟掉——呼叫端會一直等一個
        //   永遠不會被處理的呼叫。
        decision.result = Result::Closed;
    }
    return decision;
}

std::vector<std::string> Router::names() const {
    std::vector<std::string> out;
    out.reserve(routes_.size());
    for (const auto &entry : routes_) out.push_back(entry.first);
    return out;  // map 本來就是照字典序
}

}  // namespace aossimple
