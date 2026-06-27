// ─────────────────────────────────────────────────────────────────────────────
// QtDemo — a small Qt-style touch UI built with the header-only MinisQt library.
//
// Run it in the MyCastle in-browser WASM simulator (electronics → C++ → Run in
// browser): the framebuffer shows up in the DISPLAY pane and you can click the
// slider / button / checkbox with the mouse. On real hardware, provide strong
// minis_canvas_present()/minis_canvas_poll() definitions to drive a touch screen.
//
// MinisQt.h is vendored next to this sketch so the simulator compiles it without
// any extra library install; the canonical copy lives in libs/Qt of this repo.
// ─────────────────────────────────────────────────────────────────────────────
#include "MinisQt.h"

static QApplication* app        = nullptr;
static QSlider*      slider      = nullptr;
static QProgressBar* bar         = nullptr;
static QLabel*       valueLabel  = nullptr;
static QLabel*       clickLabel  = nullptr;
static int           clickCount  = 0;

static void setLabel(QLabel* lbl, const char* prefix, int v) {
  if (!lbl) return;
  char buf[32];
  snprintf(buf, sizeof(buf), "%s%d", prefix, v);
  lbl->setText(buf);
}

void setup() {
  Serial.begin(115200);
  Serial.println("MinisQt demo starting");

  app = new QApplication(320, 240);
  app->setBackground(QColor(24, 26, 30));
  QWidget* root = app->root();

  QVBoxLayout* lay = new QVBoxLayout();
  lay->setSpacing(8);
  lay->setContentsMargins(12);

  QLabel* title = new QLabel("MinisQt Demo", root);
  title->setAlignment(Qt::AlignCenter);
  title->setFont(QFont(24, true));
  lay->addWidget(title);

  valueLabel = new QLabel("Value: 30", root);
  valueLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  lay->addWidget(valueLabel);

  slider = new QSlider(root);
  slider->setRange(0, 100);
  slider->setValue(30);
  slider->valueChanged.connect([](int v) {
    if (bar) bar->setValue(v);
    setLabel(valueLabel, "Value: ", v);
    Serial.print("slider="); Serial.println(v);
  });
  lay->addWidget(slider);

  bar = new QProgressBar(root);
  bar->setRange(0, 100);
  bar->setValue(30);
  lay->addWidget(bar);

  QCheckBox* check = new QCheckBox("Enable feature", root);
  check->toggled.connect([](bool on) {
    Serial.print("checkbox="); Serial.println(on ? 1 : 0);
  });
  lay->addWidget(check);

  clickLabel = new QLabel("Clicks: 0", root);
  clickLabel->setAlignment(Qt::AlignCenter);
  lay->addWidget(clickLabel);

  QPushButton* btn = new QPushButton("Tap me", root);
  btn->clicked.connect([]() {
    clickCount++;
    setLabel(clickLabel, "Clicks: ", clickCount);
    Serial.println("button clicked");
  });
  lay->addWidget(btn);

  root->setLayout(lay);
  Serial.println("MinisQt demo ready");
}

void loop() {
  if (app) app->tick();
  delay(33);
}
