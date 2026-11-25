// THttpServer with the control buttons of the monitor page.
//
// The server stays read-only (no object methods, no interpreted commands). Two commands are shown as buttons
// at the top of the JSROOT page and in the folder Control:
//   Reset    clear the histograms and counters; they fill again from the next time frame
//   Restart  ask the program to stop so that whoever started it starts it again (only when enabled)
// Their requests (Control/<name>/cmd.json) are answered here and only set a flag; the program acts on it in its
// own loop through TakeReset() and TakeRestart(), so nothing runs inside the HTTP handling.
#pragma once

#include <memory>

#include <THttpServer.h>

namespace nda::monitor {

class ControlServer : public THttpServer {
 public:
  // engine: as for THttpServer, for example "http:127.0.0.1:8080"; allowRestart: show the Restart button.
  ControlServer(const char* engine, bool allowRestart);

  // True once after a button was pressed (the flag is cleared).
  bool TakeReset();
  bool TakeRestart();
  bool RestartAllowed() const { return fAllowRestart; }

 protected:
  void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

 private:
  bool fAllowRestart;
  bool fReset = false;
  bool fRestart = false;
};

}  // namespace nda::monitor
