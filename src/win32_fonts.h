// Only include in main.cpp!


#ifdef _WIN32

#include <Windows.h>

// Taken from https://github.com/qt/qtbase/blob/835c3e94f6089751421a19008d442faec9649ed8/src/platformsupport/fontdatabases/windows/qwindowsfontdatabase.cpp#L2064
int defaultVerticalDPI()
{
  static int vDPI = -1;
  if (vDPI == -1)
  {
    if (HDC defaultDC = GetDC(nullptr))
    {
      vDPI = GetDeviceCaps(defaultDC, LOGPIXELSY);
      ReleaseDC(nullptr, defaultDC);
    }
    else
    {
      // FIXME: Resolve now or return 96 and keep unresolved?
      vDPI = 96;
    }
  }
  return vDPI;
}

// Taken from https://github.com/qt/qtbase/blob/835c3e94f6089751421a19008d442faec9649ed8/src/platformsupport/fontdatabases/windows/qwindowsfontdatabase.cpp#L2048
QFont LOGFONT_to_QFont(const LOGFONT& logFont, int verticalDPI_In = 0)
{
  if (verticalDPI_In <= 0)
    verticalDPI_In = defaultVerticalDPI();
  QFont qFont(QString::fromWCharArray(logFont.lfFaceName));
  qFont.setItalic(logFont.lfItalic);
  //if (logFont.lfWeight != FW_DONTCARE)
  //  qFont.setWeight(QPlatformFontDatabase::weightFromInteger(logFont.lfWeight));
  const qreal logFontHeight = qAbs(logFont.lfHeight);
  qFont.setPointSizeF(logFontHeight * 72.0 / qreal(verticalDPI_In));
  qFont.setUnderline(logFont.lfUnderline);
  qFont.setOverline(false);
  qFont.setStrikeOut(logFont.lfStrikeOut);
  return qFont;
}

void win32_set_font() {
    // Edited from https://github.com/dolphin-emu/dolphin/blob/82fe8f61b6de80549a6afc9542120efffbedc0d1/Source/Core/DolphinQt/Main.cpp#L105-L127
    // Get the default system font because Qt's way of obtaining it is outdated
    NONCLIENTMETRICS metrics = {};
    LOGFONT& logfont = metrics.lfMenuFont;
    metrics.cbSize = sizeof(NONCLIENTMETRICS);

    if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        // Sadly Qt 5 doesn't support turning a native font handle into a QFont so this is the next best
        // thing
        QFont font = LOGFONT_to_QFont(logfont);

        QApplication::setFont(font);
    }

}
#else
void win32_set_font() {
}
#endif
