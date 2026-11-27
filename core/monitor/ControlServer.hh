// THttpServer with the control buttons of the monitor page.
//
// The server stays read-only (no object methods, no interpreted commands). These commands are shown as buttons
// at the top of the JSROOT page and in the folder Control:
//   Reset                clear the histograms and counters of the program (every browser sees it); they fill
//                        again from the next time frame
//   ResetBrowser         clear only what this browser shows (see BrowserResetScript)
//   ClearBrowserReset    show the histograms as they are again
//   Restart              ask the program to stop so that whoever started it starts it again (only when enabled)
// The requests of Reset and Restart (Control/<name>/cmd.json) are answered here and only set a flag; the program acts
// on it in its own loop through TakeReset() and TakeRestart(), so nothing runs inside the HTTP handling.
// The two browser commands are executed by a script of the page, which this server provides (nda/browser-reset.mjs);
// a request for them that reaches the server is refused. The histograms that show a current value instead of
// accumulated counts (rates, status) are marked with the item field _nda_instant and are never changed by it.
#pragma once

#include <memory>
#include <string>

#include <TNamed.h>
#include <THttpServer.h>

namespace nda::monitor {

// The JavaScript module of the browser-side reset.
const char* BrowserResetScript();

class ControlServer : public THttpServer {
 public:
  // engine: as for THttpServer, for example "http:127.0.0.1:8080"; allowRestart: show the Restart button.
  ControlServer(const char* engine, bool allowRestart);
  ~ControlServer() override;

  // True once after a button was pressed (the flag is cleared).
  bool TakeReset();
  bool TakeRestart();
  bool RestartAllowed() const { return fAllowRestart; }
  // Identifier of the current contents of the histograms (the object Control/generation): it changes at every
  // Reset and differs for every start of the program, so a browser can tell that its saved baseline is obsolete.
  const char* Generation() const { return fGeneration.GetTitle(); }

 protected:
  void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

 private:
  bool fAllowRestart;
  bool fReset = false;
  bool fRestart = false;
  std::string fGenerationBase;
  unsigned fResets = 0;
  TNamed fGeneration;
};

}  // namespace nda::monitor
