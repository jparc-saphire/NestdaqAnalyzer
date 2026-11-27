#include "monitor/ControlServer.hh"

#include <THttpCallArg.h>

#include <chrono>
#include <cstring>
#include <string>

namespace nda::monitor {

namespace {

// The method is never executed: the requests are answered in ProcessRequest. A comment is harmless in any case.
constexpr const char* kNoMethod = "/* handled by the monitor program */";

// Button icons (24 x 24 SVG as data URLs, so the page needs no image files): histogram bars crossed out, and a
// circular arrow.
constexpr const char* kResetIcon =
    "button;data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 24 24%22%3E%3Crect x=%223%22 y=%2212%22 width=%224%22 height=%229%22 fill=%22%23888%22/%3E%3Crect x=%2210%22 y=%226%22 width=%224%22 height=%2215%22 fill=%22%23888%22/%3E%3Crect x=%2217%22 y=%229%22 width=%224%22 height=%2212%22 fill=%22%23888%22/%3E%3Cpath d=%22M3 3L21 21M21 3L3 21%22 stroke=%22%23d00%22 stroke-width=%223%22/%3E%3C/svg%3E";
constexpr const char* kRestartIcon =
    "button;data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 24 24%22%3E%3Cpath d=%22M19 12a7 7 0 1 1-2.05-4.95%22 fill=%22none%22 stroke=%22%2306c%22 stroke-width=%223%22/%3E%3Cpath d=%22M20 2v7h-7z%22 fill=%22%2306c%22/%3E%3C/svg%3E";

// Reset of this browser only: the same bars crossed out, in the colour of a browser window.
constexpr const char* kResetBrowserIcon =
    "button;data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 24 24%22%3E%3Crect x=%221.5%22 y=%222.5%22 width=%2221%22 height=%2219%22 rx=%222%22 fill=%22none%22 stroke=%22%23888%22 stroke-width=%222%22/%3E%3Crect x=%225%22 y=%2213%22 width=%224%22 height=%226%22 fill=%22%23888%22/%3E%3Crect x=%2211%22 y=%228%22 width=%224%22 height=%2211%22 fill=%22%23888%22/%3E%3Crect x=%2217%22 y=%2211%22 width=%223%22 height=%228%22 fill=%22%23888%22/%3E%3Cpath d=%22M4 4L20 20M20 4L4 20%22 stroke=%22%23d00%22 stroke-width=%222.5%22/%3E%3C/svg%3E";
constexpr const char* kClearBrowserIcon =
    "button;data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 24 24%22%3E%3Crect x=%221.5%22 y=%222.5%22 width=%2221%22 height=%2219%22 rx=%222%22 fill=%22none%22 stroke=%22%23888%22 stroke-width=%222%22/%3E%3Crect x=%225%22 y=%2213%22 width=%224%22 height=%226%22 fill=%22%23888%22/%3E%3Crect x=%2211%22 y=%228%22 width=%224%22 height=%2211%22 fill=%22%23888%22/%3E%3Crect x=%2217%22 y=%2211%22 width=%223%22 height=%228%22 fill=%22%23888%22/%3E%3Cpath d=%22M3 21L21 3%22 stroke=%22%2306c%22 stroke-width=%222.5%22 stroke-dasharray=%222 2%22/%3E%3C/svg%3E";

std::string GenerationId(const std::string& base, unsigned n) { return base + "-" + std::to_string(n); }

}  // namespace

ControlServer::ControlServer(const char* engine, bool allowRestart)
    : THttpServer(engine),
      fAllowRestart(allowRestart),
      fGenerationBase(std::to_string(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count())),
      fGeneration("generation", GenerationId(fGenerationBase, 0).c_str()) {
  SetReadOnly(true);
  RegisterCommand("/Control/Reset", kNoMethod, kResetIcon);
  SetItemField("/Control/Reset", "_title",
               "Reset (all browsers): clear all histograms and counters of the analyzer, they fill again from now on");
  RegisterCommand("/Control/ResetBrowser", kNoMethod, kResetBrowserIcon);
  SetItemField("/Control/ResetBrowser", "_title",
               "Reset (this browser only): show the histograms counted from now on; the analyzer and other browsers "
               "are not affected");
  RegisterCommand("/Control/ClearBrowserReset", kNoMethod, kClearBrowserIcon);
  SetItemField("/Control/ClearBrowserReset", "_title", "Show the histograms as they are again (undo Reset of this browser)");
  Register("/Control", &fGeneration);
  SetItemField("/Control/generation", "_hidden", "true");
  SetItemField("/", "_autoload", "../../../nda/browser-reset.mjs");
  if (fAllowRestart) {
    RegisterCommand("/Control/Restart", kNoMethod, kRestartIcon);
    SetItemField("/Control/Restart", "_title",
                 "Restart: start the analyzer again (the page is unavailable for a few seconds)");
  }
}

ControlServer::~ControlServer() { Unregister(&fGeneration); }

bool ControlServer::TakeReset() {
  const bool r = fReset;
  fReset = false;
  if (r) fGeneration.SetTitle(GenerationId(fGenerationBase, ++fResets).c_str());
  return r;
}

bool ControlServer::TakeRestart() {
  const bool r = fRestart;
  fRestart = false;
  return r;
}

void ControlServer::ProcessRequest(std::shared_ptr<THttpCallArg> arg) {
  // ProcessRequest runs in the thread that calls ProcessEvents (the program's loop), so plain flags are enough.
  if (std::strcmp(arg->GetFileName(), "browser-reset.mjs") == 0) {
    std::string dir = arg->GetPathName();
    while (!dir.empty() && dir.front() == '/') dir.erase(0, 1);
    while (!dir.empty() && dir.back() == '/') dir.pop_back();
    if (dir == "nda") {
      arg->SetContentType("text/javascript");
      arg->SetContent(BrowserResetScript());
      return;
    }
  }
  if (std::strcmp(arg->GetFileName(), "cmd.json") == 0) {
    std::string path = arg->GetPathName();
    while (!path.empty() && path.front() == '/') path.erase(0, 1);
    while (!path.empty() && path.back() == '/') path.pop_back();
    if (path == "Control/Reset") {
      fReset = true;
      arg->SetJsonContent("true");
      return;
    }
    if (path == "Control/Restart") {
      if (fAllowRestart) fRestart = true;
      arg->SetJsonContent(fAllowRestart ? "true" : "false");
      return;
    }
    // Any other command request is refused: the server executes no commands of its own.
    arg->SetJsonContent("false");
    return;
  }
  THttpServer::ProcessRequest(arg);
}

}  // namespace nda::monitor
