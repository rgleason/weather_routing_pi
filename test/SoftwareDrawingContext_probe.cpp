#include <wx/wx.h>
#include <wx/dcmemory.h>
#include <cstdio>
#include <stdexcept>
#include "pidc.h"

// Link the real drawing helper but make any context-free GL query fail before
// reaching the driver. The unguarded dependency fails this probe on Linux too.
static int gl_queries = 0;
extern "C" void glGetIntegerv(GLenum, GLint*) {
  ++gl_queries;
  throw std::runtime_error("software drawing queried OpenGL");
}

class DrawingProbeApp : public wxApp {
 public:
  bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(DrawingProbeApp);

int main(int argc, char** argv) {
  if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
  int result = 0;
  try {
    wxBitmap bitmap(48, 32, 32);
    wxMemoryDC memory(bitmap);
    memory.SetBackground(*wxWHITE_BRUSH);
    memory.Clear();
    {
      piDC software(memory);
      software.SetPen(wxPen(wxColour(170, 0, 200), 2));
      software.DrawLine(4, 5, 40, 5);
      software.SetFont(wxFont(wxFontInfo(9)));
      software.SetTextForeground(wxColour(100, 0, 130));
      software.DrawText("Shoreline", 2, 12);
    }
    memory.SelectObject(wxNullBitmap);
    const auto image = bitmap.ConvertToImage();
    bool painted = false;
    for (int y = 0; y < image.GetHeight(); ++y)
      for (int x = 0; x < image.GetWidth(); ++x)
        painted |= image.GetRed(x,y) != 255 || image.GetGreen(x,y) != 255 ||
                   image.GetBlue(x,y) != 255;
    if (gl_queries || !painted) result = 3;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    result = 1;
  }
  wxTheApp->OnExit();
  wxEntryCleanup();
  return result;
}
