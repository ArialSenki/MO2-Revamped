using namespace cli;

#include "fluentcontentdialog.h"

/**
 * @brief An OMOD's readme or a text its script shows, RTF or plain: the
 * window's title as the heading, the text, and Close. MO2 Revamped draws it as
 * a Windows 11 content dialog.
 **/
class RtfPopup : public RevampedFluent::ContentDialog
{
  Q_OBJECT

public:
  RtfPopup(System::String^ rtfText, QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());

protected:
  // the callers set the title after building it: the heading follows
  void changeEvent(QEvent* event) override;
};
