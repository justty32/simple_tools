#include "vm.hpp"

#include <janet.h>

#include <cstring>

#include "bootstrap.hpp"
#include "unixcall.hpp"

namespace aoslisp {
namespace {

std::string bytes_of(JanetBuffer *b) {
    if (b == nullptr || b->count <= 0) return std::string{};
    return std::string(reinterpret_cast<const char *>(b->data),
                       static_cast<std::size_t>(b->count));
}

// 把任意 Janet 值變成一行人看得懂的字。只給錯誤路徑用。
std::string describe(Janet v) {
    JanetBuffer *b = janet_buffer(64);
    janet_to_string_b(b, v);
    return bytes_of(b);
}

}  // namespace

Vm::Vm() {
    if (janet_init() != 0) {
        boot_error_ = "janet_init 失敗";
        return;
    }
    started_ = true;

    JanetTable *core = janet_core_env(nullptr);
    if (core == nullptr) {
        boot_error_ = "janet_core_env 回 NULL";
        return;
    }

    // C 這一側提供的能力先進 core env，bootstrap 造的 task-env 以它為 prototype，
    // 所以任務看得到 unix/*，但任務自己的 def 不會蓋掉這裡。
    install_unix_cfuns(core);

    Janet out = janet_wrap_nil();
    const int err = janet_dobytes(
        core, reinterpret_cast<const uint8_t *>(kBootstrap),
        static_cast<int32_t>(std::strlen(kBootstrap)), "aos-lisp-bootstrap",
        &out);
    if (err != 0) {
        boot_error_ = "bootstrap 跑不起來：" + describe(out);
        return;
    }
    if (!janet_checktype(out, JANET_FUNCTION)) {
        boot_error_ = "bootstrap 應該回一個函式，拿到 " + describe(out);
        return;
    }

    // ★ 一定要 gcroot。這個函式只有 C 這邊的指標指著它，Janet 那側沒有任何
    //   綁定引用得到（那正是我們要的隔離），所以不 root 的話下一次 GC 就收走了。
    janet_gcroot(out);
    run_job_ = janet_unwrap_function(out);
    ok_ = true;
}

Vm::~Vm() {
    if (run_job_ != nullptr) {
        janet_gcunroot(janet_wrap_function(static_cast<JanetFunction *>(run_job_)));
        run_job_ = nullptr;
    }
    if (started_) janet_deinit();
}

void Vm::run(Job &job) noexcept {
    if (!ok_) {
        job.err = "這條 loop 的 Janet VM 沒有起來：" + boot_error_ + "\n";
        job.status = 1;
        return;
    }

    JanetBuffer *out = janet_buffer(256);
    JanetBuffer *err = janet_buffer(64);

    const Janet argv[3] = {
        janet_stringv(reinterpret_cast<const uint8_t *>(job.source.data()),
                      static_cast<int32_t>(job.source.size())),
        janet_wrap_buffer(out),
        janet_wrap_buffer(err),
    };

    Janet result = janet_wrap_nil();
    JanetFiber *fiber = nullptr;
    // ★ janet_pcall 而不是 janet_call：bootstrap 已經把任務的錯誤攔成資料了，
    //   但 bootstrap 自己也可能出事（例如 render 撞到怪東西）。那一層再接一次，
    //   讓一個壞 job 絕對炸不掉這條 loop——跟 aos-core「一個壞外掛不會讓 daemon
    //   起不來」是同一條規矩。
    const JanetSignal sig =
        janet_pcall(static_cast<JanetFunction *>(run_job_), 3, argv, &result,
                    &fiber);

    job.out = bytes_of(out);
    job.err = bytes_of(err);

    if (sig != JANET_SIGNAL_OK) {
        job.err += "求值層本身出事：" + describe(result) + "\n";
        job.status = 1;
        return;
    }
    job.status = janet_checktype(result, JANET_NUMBER)
                     ? static_cast<int>(janet_unwrap_number(result))
                     : 0;
}

}  // namespace aoslisp
