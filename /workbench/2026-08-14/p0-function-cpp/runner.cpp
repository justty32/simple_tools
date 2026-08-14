#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <poll.h>
#include <string>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;
namespace fs = std::filesystem;

struct ChildError { int stage; int error; }; // exactly one PIPE_BUF-safe write
struct Result { std::string kind; int value = 0; int error = 0; std::string stage; std::vector<char> out, err; };

static void close_fd(int& fd) { if (fd >= 0) { while (::close(fd) < 0 && errno == EINTR) {} fd = -1; } }
static bool set_nonblock(int fd) { const int flags = ::fcntl(fd, F_GETFL); return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0; }
static void child_fail(int fd, int stage) { const ChildError msg{stage, errno}; const char* p = reinterpret_cast<const char*>(&msg); std::size_t n = sizeof msg; while (n) { const auto r = ::write(fd, p, n); if (r > 0) { p += r; n -= static_cast<std::size_t>(r); } else if (r < 0 && errno == EINTR) continue; else break; } _exit(127); }
static void drain_fd(int& fd, std::vector<char>& target) { char data[65536]; for (;;) { const auto n = ::read(fd, data, sizeof data); if (n > 0) target.insert(target.end(), data, data + n); else if (n == 0) { close_fd(fd); return; } else if (errno == EINTR) continue; else if (errno == EAGAIN || errno == EWOULDBLOCK) return; else { close_fd(fd); return; } } }
static const char* errno_name(int value) { const char* s = std::strerror(value); return s ? s : "unknown"; }
static std::string esc(const std::string& s) { std::string r; r.reserve(s.size()+8); static constexpr char hex[]="0123456789abcdef"; for (unsigned char c : s) { switch(c) { case '\\':r += "\\\\";break;case '"':r += "\\\"";break;case '\n':r += "\\n";break;case '\r':r += "\\r";break;case '\t':r += "\\t";break;default: if(c < 0x20) { r += "\\u00"; r += hex[c>>4]; r += hex[c&15]; } else r += char(c); } } return r; }
static bool write_file(const fs::path& path, const std::vector<char>& value) { std::ofstream f(path, std::ios::binary); if (!f) return false; f.write(value.data(), static_cast<std::streamsize>(value.size())); return bool(f); }

static Result run(const std::string& executable, const std::vector<std::string>& args, const std::optional<std::string>& cwd, const std::vector<char>& input) {
  int in_pipe[2]{-1,-1}, out_pipe[2]{-1,-1}, err_pipe[2]{-1,-1}, launch_pipe[2]{-1,-1};
  if (::pipe2(in_pipe, O_CLOEXEC) || ::pipe2(out_pipe, O_CLOEXEC) || ::pipe2(err_pipe, O_CLOEXEC) || ::pipe2(launch_pipe, O_CLOEXEC)) throw std::runtime_error("pipe2 failed");
  std::vector<char*> argv; argv.reserve(args.size()+1); for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str())); argv.push_back(nullptr);
  const pid_t pid = ::fork();
  if (pid < 0) throw std::runtime_error("fork failed");
  if (pid == 0) {
    close_fd(in_pipe[1]); close_fd(out_pipe[0]); close_fd(err_pipe[0]); close_fd(launch_pipe[0]);
    if (::dup2(in_pipe[0], STDIN_FILENO) < 0) child_fail(launch_pipe[1], 3);
    if (::dup2(out_pipe[1], STDOUT_FILENO) < 0) child_fail(launch_pipe[1], 3);
    if (::dup2(err_pipe[1], STDERR_FILENO) < 0) child_fail(launch_pipe[1], 3);
    close_fd(in_pipe[0]); close_fd(out_pipe[1]); close_fd(err_pipe[1]);
    // SIG_IGN survives exec.  Parent ignores SIGPIPE to turn EPIPE into a
    // normal stdin-close result, so restore the Function's default here.
    struct sigaction child_sigpipe{};
    child_sigpipe.sa_handler = SIG_DFL;
    if (::sigemptyset(&child_sigpipe.sa_mask) < 0 || ::sigaction(SIGPIPE, &child_sigpipe, nullptr) < 0) child_fail(launch_pipe[1], 3);
    if (cwd && ::chdir(cwd->c_str()) < 0) child_fail(launch_pipe[1], 1);
    ::execve(executable.c_str(), argv.data(), environ); child_fail(launch_pipe[1], 2);
  }
  close_fd(in_pipe[0]); close_fd(out_pipe[1]); close_fd(err_pipe[1]); close_fd(launch_pipe[1]);
  if (!set_nonblock(in_pipe[1]) || !set_nonblock(out_pipe[0]) || !set_nonblock(err_pipe[0]) || !set_nonblock(launch_pipe[0])) throw std::runtime_error("fcntl nonblocking failed");
  std::size_t offset = 0; int status = 0; bool reaped = false; std::array<char, sizeof(ChildError)> launch_bytes{}; std::size_t launch_size = 0;
  Result result;
  while (in_pipe[1] >= 0 || out_pipe[0] >= 0 || err_pipe[0] >= 0 || launch_pipe[0] >= 0 || !reaped) {
    std::array<pollfd,4> pf{}; nfds_t count = 0;
    if (in_pipe[1] >= 0) pf[count++] = {in_pipe[1], POLLOUT, 0};
    if (out_pipe[0] >= 0) pf[count++] = {out_pipe[0], POLLIN, 0};
    if (err_pipe[0] >= 0) pf[count++] = {err_pipe[0], POLLIN, 0};
    if (launch_pipe[0] >= 0) pf[count++] = {launch_pipe[0], POLLIN, 0};
    if (count) { int pr; do { pr = ::poll(pf.data(), count, 100); } while (pr < 0 && errno == EINTR); if (pr < 0) throw std::runtime_error("poll failed");
      for (nfds_t i=0;i<count;++i) { const auto fd=pf[i].fd; const auto ev=pf[i].revents; if (!ev) continue;
        if (fd == in_pipe[1] && (ev & (POLLOUT|POLLERR|POLLHUP|POLLNVAL))) { while (offset < input.size()) { const auto n=::write(in_pipe[1], input.data()+offset, input.size()-offset); if(n>0) offset+=static_cast<std::size_t>(n); else if(n<0&&errno==EINTR) continue; else if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)) break; else { offset=input.size(); break; } } if(offset==input.size()) close_fd(in_pipe[1]); }
        else if (fd == out_pipe[0] && (ev & (POLLIN|POLLHUP|POLLERR))) drain_fd(out_pipe[0], result.out);
        else if (fd == err_pipe[0] && (ev & (POLLIN|POLLHUP|POLLERR))) drain_fd(err_pipe[0], result.err);
        else if (fd == launch_pipe[0] && (ev & (POLLIN|POLLHUP|POLLERR))) { for (;;) { const auto n=::read(launch_pipe[0], launch_bytes.data()+launch_size, launch_bytes.size()-launch_size); if(n>0) launch_size+=static_cast<std::size_t>(n); else if(n==0) { close_fd(launch_pipe[0]); break; } else if(errno==EINTR) continue; else if(errno==EAGAIN||errno==EWOULDBLOCK) break; else { close_fd(launch_pipe[0]); break; } if(launch_size==launch_bytes.size()) {} } }
      }
    }
    if (!reaped) { const auto w=::waitpid(pid,&status,WNOHANG); if(w==pid) reaped=true; else if(w<0 && errno!=EINTR) throw std::runtime_error("waitpid failed"); }
  }
  if (launch_size == sizeof(ChildError)) { ChildError ce{}; std::memcpy(&ce, launch_bytes.data(), sizeof ce); result.kind="launch_error"; result.stage=ce.stage==1?"chdir":ce.stage==2?"exec":"stdio"; result.error=ce.error; }
  else if (WIFEXITED(status)) { result.kind="exited"; result.value=WEXITSTATUS(status); }
  else if (WIFSIGNALED(status)) { result.kind="signaled"; result.value=WTERMSIG(status); }
  else { result.kind="unknown"; }
  return result;
}

int main(int argc, char** argv) {
  std::optional<std::string> cwd; std::optional<fs::path> stdin_path; std::optional<fs::path> out_dir; int i=1;
  for (;i<argc;++i) { std::string a=argv[i]; if(a=="--cwd" && i+1<argc) cwd=argv[++i]; else if(a=="--stdin" && i+1<argc) stdin_path=argv[++i]; else if(a=="--out-dir" && i+1<argc) out_dir=argv[++i]; else if(a=="--") { ++i; break; } else { std::cerr<<"usage: aos_p0_runner [--cwd DIR] [--stdin FILE] [--out-dir DIR] -- EXECUTABLE [ARG ...]\n"; return 64; } }
  if(!out_dir || i>=argc) { std::cerr<<"missing --out-dir or executable\n"; return 64; }
  std::vector<char> input; if(stdin_path) { std::ifstream f(*stdin_path,std::ios::binary); if(!f) { std::cerr<<"cannot read stdin file\n"; return 64;} input.assign(std::istreambuf_iterator<char>(f),{}); }
  std::vector<std::string> args; for(;i<argc;++i) args.emplace_back(argv[i]);
  try { fs::create_directories(*out_dir); struct sigaction sa{}; sa.sa_handler=SIG_IGN; sigemptyset(&sa.sa_mask); sigaction(SIGPIPE,&sa,nullptr); Result r=run(args[0],args,cwd,input);
    if(!write_file(*out_dir/"stdout.bin",r.out) || !write_file(*out_dir/"stderr.bin",r.err)) throw std::runtime_error("cannot write blobs");
    std::string outcome; if(r.kind=="exited") outcome="{\"kind\":\"exited\",\"code\":"+std::to_string(r.value)+"}"; else if(r.kind=="signaled") outcome="{\"kind\":\"signaled\",\"signal\":"+std::to_string(r.value)+"}"; else if(r.kind=="launch_error") outcome="{\"kind\":\"launch_error\",\"stage\":\""+r.stage+"\",\"errno\":"+std::to_string(r.error)+",\"errno_message\":\""+esc(errno_name(r.error))+"\"}"; else outcome="{\"kind\":\"unknown\"}";
    const std::string receipt="{\"version\":0,\"outcome\":"+outcome+",\"stdout\":{\"file\":\"stdout.bin\",\"size\":"+std::to_string(r.out.size())+"},\"stderr\":{\"file\":\"stderr.bin\",\"size\":"+std::to_string(r.err.size())+"}}\n";
    std::ofstream rf(*out_dir/"receipt.json",std::ios::binary); rf<<receipt; if(!rf) throw std::runtime_error("cannot write receipt"); std::cout<<receipt; return 0;
  } catch(const std::exception& e) { std::cerr<<"runner error: "<<e.what()<<"\n"; return 70; }
}
