// Only include in main.cpp!


#ifdef _WIN32

#include <QApplication>
#include <QFont>

void win32_set_font() {
  // On Windows, Qt 5's default system font (MS Shell Dlg 2) is outdated.
  // Interestingly, the QMessageBox font is correct and comes from lfMessageFont
  // (Segoe UI on English computers).
  // So use it for the entire application.
  // This code will become unnecessary and obsolete once we switch to Qt 6.
  QApplication::setFont(QApplication::font("QMessageBox"));
}

#else
void win32_set_font() {
}
#endif
