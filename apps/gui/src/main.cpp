#include "core/calculator.hpp"
#include "core/opcodes.hpp"
#include "core/presets.hpp"
#include "core/types.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSplitter>
#include <QStatusBar>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using mk61::core::AngleMode;
using mk61::core::Calculator;
using mk61::core::DisplayState;
using mk61::core::Key;
using mk61::core::OperatingMode;
using mk61::core::PrefixState;

QString to_qstr(std::string_view sv) {
  return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
}

/// Індикатор на 12 розрядів, як ІЛЦ2-12/8Л на корпусі МК-61
class VfdScreenWidget final : public QWidget {
public:
  explicit VfdScreenWidget(const QFont &vfd_font, QWidget *parent = nullptr)
      : QWidget(parent), vfd_font_(vfd_font) {
    setFixedHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void setDisplayState(const DisplayState &state) {
    state_ = state;
    update();
  }

protected:
  void paintEvent(QPaintEvent * /*event*/) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect r = rect();
    p.fillRect(r, QColor(5, 18, 12));

    // Тонка сітка катодної нитки розжарювання ВЛІ
    p.setPen(QPen(QColor(15, 45, 30, 90), 1));
    for (int y = 6; y < r.height(); y += 8) {
      p.drawLine(4, y, r.width() - 4, y);
    }

    constexpr int kCells = 12;
    const double pad_x = 8.0;
    const double avail_w = std::max(120.0, static_cast<double>(r.width()) - 2.0 * pad_x);
    const double cell_w = avail_w / static_cast<double>(kCells);

    struct CellGlyph {
      QString ch{QStringLiteral(" ")};
      bool dot{false};
    };
    std::array<CellGlyph, kCells> cells{};

    auto parse_into_cells = [&](const QString &str, std::size_t start_idx, std::size_t max_cells) {
      std::size_t pos = start_idx;
      for (qsizetype i = 0; i < str.size(); ++i) {
        const QChar c = str[i];
        if ((c == QLatin1Char('.') || c == QLatin1Char(',')) && pos > start_idx &&
            pos - 1 < start_idx + max_cells && !cells[pos - 1].dot) {
          cells[pos - 1].dot = true;
        } else if (pos < start_idx + max_cells) {
          cells[pos].ch = QString(c);
          ++pos;
        }
      }
    };

    if (!state_.is_dark) {
      const QString mant_q = to_qstr(state_.mantissa_part);
      const QString exp_q = to_qstr(state_.exponent_part);
      if (!exp_q.isEmpty()) {
        parse_into_cells(mant_q, 0, 9);
        parse_into_cells(exp_q, 9, 3);
      } else {
        parse_into_cells(to_qstr(state_.text), 0, 12);
      }
    }

    QFont digit_font = vfd_font_;
    const int px_size = std::clamp(static_cast<int>(cell_w * 1.18), 20, 32);
    digit_font.setPixelSize(px_size);
    p.setFont(digit_font);

    const QColor ghost_color(10, 32, 21);
    const QColor glow_color(28, 175, 100, 95);
    const QColor active_color(68, 255, 158);

    for (int i = 0; i < kCells; ++i) {
      const double cx = pad_x + static_cast<double>(i) * cell_w;
      const QRectF cell_rect(cx, 4.0, cell_w, static_cast<double>(r.height()) - 8.0);
      const QRectF dot_rect(cx + cell_w * 0.62, 4.0, cell_w * 0.5,
                            static_cast<double>(r.height()) - 8.0);

      // 1. Тіньові (неактивні) сегменти "8"
      p.setPen(ghost_color);
      p.drawText(cell_rect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("8"));

      if (state_.is_dark) {
        continue;
      }

      // 2. Активний символ із легким люмінофорним ореолом
      const QString &ch = cells[static_cast<std::size_t>(i)].ch;
      if (!ch.isEmpty() && ch != QStringLiteral(" ")) {
        p.setPen(glow_color);
        p.drawText(cell_rect.adjusted(-1.0, -1.0, 1.0, 1.0), Qt::AlignLeft | Qt::AlignVCenter, ch);
        p.setPen(active_color);
        p.drawText(cell_rect, Qt::AlignLeft | Qt::AlignVCenter, ch);
      }

      // 3. Активна десяткова крапка (кома)
      if (cells[static_cast<std::size_t>(i)].dot) {
        p.setPen(glow_color);
        p.drawText(dot_rect.adjusted(-1.0, -1.0, 1.0, 1.0), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("."));
        p.setPen(active_color);
        p.drawText(dot_rect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("."));
      }
    }
  }

private:
  QFont vfd_font_;
  DisplayState state_{};
};

class Mk61MainWindow final : public QMainWindow {
public:
  explicit Mk61MainWindow(const QFont &vfd_font, QWidget *parent = nullptr)
      : QMainWindow(parent), vfd_font_(vfd_font) {
    setWindowTitle(QStringLiteral("Електроніка МК-61 — Емулятор програмованого мікрокалькулятора"));
    setMinimumSize(820, 640);

    run_timer_ = new QTimer(this);
    run_timer_->setInterval(40);
    QObject::connect(run_timer_, &QTimer::timeout, this, &Mk61MainWindow::onRunTimerTick);

    setupUi();
    refreshAll();

    if (const QScreen *scr = QGuiApplication::primaryScreen()) {
      const QRect avail = scr->availableGeometry();
      const int target_w = std::min(1040, std::max(820, avail.width() - 60));
      const int target_h = std::min(680, std::max(640, avail.height() - 60));
      resize(target_w, target_h);
    } else {
      resize(1020, 660);
    }
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_F10) {
      onDebugStepClicked();
      return;
    }
    if (event->key() == Qt::Key_F5) {
      onDebugPlayPauseClicked();
      return;
    }
    if (event->key() == Qt::Key_F9) {
      toggleBreakpointAtPc();
      return;
    }
    if (event->key() == Qt::Key_F12) {
      toggleInspectorPanel();
      return;
    }

    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
      QMainWindow::keyPressEvent(event);
      return;
    }

    switch (event->key()) {
    case Qt::Key_0:
      triggerKey(Key::D0);
      return;
    case Qt::Key_1:
      triggerKey(Key::D1);
      return;
    case Qt::Key_2:
      triggerKey(Key::D2);
      return;
    case Qt::Key_3:
      triggerKey(Key::D3);
      return;
    case Qt::Key_4:
      triggerKey(Key::D4);
      return;
    case Qt::Key_5:
      triggerKey(Key::D5);
      return;
    case Qt::Key_6:
      triggerKey(Key::D6);
      return;
    case Qt::Key_7:
      triggerKey(Key::D7);
      return;
    case Qt::Key_8:
      triggerKey(Key::D8);
      return;
    case Qt::Key_9:
      triggerKey(Key::D9);
      return;
    case Qt::Key_Period:
    case Qt::Key_Comma:
      triggerKey(Key::Dot);
      return;
    case Qt::Key_Plus:
      triggerKey(Key::Plus);
      return;
    case Qt::Key_Minus:
      triggerKey(Key::Minus);
      return;
    case Qt::Key_Asterisk:
      triggerKey(Key::Multiply);
      return;
    case Qt::Key_Slash:
      triggerKey(Key::Divide);
      return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      triggerKey(Key::Enter);
      return;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Escape:
      stopRunningTimer();
      triggerKey(Key::ClearX);
      return;
    case Qt::Key_F:
      triggerKey(Key::F);
      return;
    case Qt::Key_K:
      triggerKey(Key::K);
      return;
    case Qt::Key_V:
    case Qt::Key_E:
      triggerKey(Key::Exp);
      return;
    case Qt::Key_M:
      triggerKey(Key::Sign);
      return;
    case Qt::Key_X:
      triggerKey(Key::Swap);
      return;
    case Qt::Key_S:
      triggerKey(Key::Store);
      return;
    case Qt::Key_R:
      triggerKey(Key::Recall);
      return;
    case Qt::Key_B:
    case Qt::Key_G:
      triggerKey(Key::Goto);
      return;
    case Qt::Key_P:
      triggerKey(Key::Gosub);
      return;
    case Qt::Key_Home:
      triggerKey(Key::Return);
      return;
    case Qt::Key_Space:
      onRunStopButton();
      return;
    case Qt::Key_Right:
      triggerKey(Key::StepFwd);
      return;
    case Qt::Key_Left:
      triggerKey(Key::StepBack);
      return;
    default:
      QMainWindow::keyPressEvent(event);
    }
  }

private:
  Calculator calc_;
  QFont vfd_font_;
  QTimer *run_timer_{nullptr};
  bool updating_ui_{false};
  std::set<int> breakpoints_;

  // Елементи лівої панелі (корпус калькулятора)
  VfdScreenWidget *vfd_screen_{nullptr};
  QLabel *status_badge_label_{nullptr};
  QRadioButton *rad_btn_{nullptr};
  QRadioButton *deg_btn_{nullptr};
  QRadioButton *grad_btn_{nullptr};
  QRadioButton *avt_btn_{nullptr};
  QRadioButton *prg_btn_{nullptr};
  QPushButton *toggle_panel_btn_{nullptr};
  QWidget *inspector_panel_{nullptr};
  QSize expanded_size_{1020, 660};

  // Елементи панелі відлагодження
  QPushButton *dbg_play_btn_{nullptr};
  QComboBox *dbg_speed_combo_{nullptr};
  QLabel *dbg_current_step_label_{nullptr};

  // Елементи правої панелі (налагоджувач, регістри, програми)
  std::array<QLabel *, 5> stack_labels_{};
  QLabel *return_stack_label_{nullptr};
  QTableWidget *regs_table_{nullptr};
  QTableWidget *prog_table_{nullptr};
  QComboBox *preset_combo_{nullptr};
  QLabel *preset_hint_label_{nullptr};
  QPlainTextEdit *asm_editor_{nullptr};

  void setupUi() {
    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *main_layout = new QHBoxLayout(central);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(10);

    // Зліва корпус фіксованого розміру, справа панель забирає решту вікна.
    auto *left_container = new QWidget(central);
    auto *left_vbox = new QVBoxLayout(left_container);
    left_vbox->setContentsMargins(0, 0, 0, 0);
    left_vbox->setSpacing(0);
    left_vbox->addWidget(createCalculatorFaceplate(left_container), 0, Qt::AlignTop);
    left_vbox->addStretch(1);

    main_layout->addWidget(left_container, 0);
    inspector_panel_ = createInspectorPanel(central);
    main_layout->addWidget(inspector_panel_, 1);

    statusBar()->showMessage(
      QStringLiteral("Готово. Клавіші: 0-9, +-*/, Enter=В↑, Esc=Сх, F, K, Пробіл=С/П | "
                     "F10=Крок, F5=Пуск/Пауза, F9=Точка зупину, F12=Сховати/показати панель."));
  }

  QWidget *createCalculatorFaceplate(QWidget *parent) {
    auto *face = new QFrame(parent);
    // Розмір корпусу фіксований, інакше сітка клавіш роз'їжджається.
    face->setFixedSize(376, 594);
    face->setStyleSheet(
      QStringLiteral("QFrame#mk61Face { background-color: #26292e; border: 3px solid #3d424a; "
                     "border-radius: 12px; }"));
    face->setObjectName(QStringLiteral("mk61Face"));

    auto *vbox = new QVBoxLayout(face);
    vbox->setContentsMargins(12, 10, 12, 10);
    vbox->setSpacing(7);

    // 1. Шильдик «ЕЛЕКТРОНІКА МК-61» + кнопки керування
    auto *header_box = new QHBoxLayout();
    header_box->setSpacing(6);
    auto *brand_label = new QLabel(QStringLiteral("ЕЛЕКТРОНІКА МК-61"), face);
    brand_label->setStyleSheet(
      QStringLiteral("color: #e8ecf2; font-weight: bold; font-size: 14px; letter-spacing: 1px;"));
    header_box->addWidget(brand_label);
    header_box->addStretch();

    auto *reset_btn = new QPushButton(QStringLiteral("Скидання"), face);
    reset_btn->setToolTip(QStringLiteral("Повне скидання живлення калькулятора"));
    reset_btn->setCursor(Qt::PointingHandCursor);
    reset_btn->setFocusPolicy(Qt::NoFocus);
    reset_btn->setStyleSheet(
      QStringLiteral("QPushButton { background-color: #454a54; color: #f0f0f0; border-radius: 4px; "
                     "padding: 3px 7px; font-size: 11px; }"
                     "QPushButton:hover { background-color: #59606d; }"));
    QObject::connect(reset_btn, &QPushButton::clicked, this, [this]() {
      stopRunningTimer();
      calc_.reset();
      refreshAll();
      statusBar()->showMessage(QStringLiteral("Калькулятор перезавантажено (стан скинуто)."));
    });
    header_box->addWidget(reset_btn);

    toggle_panel_btn_ = new QPushButton(QStringLiteral("◀ Тільки МК-61"), face);
    toggle_panel_btn_->setToolTip(
      QStringLiteral("Приховати або показати праву панель регістрів і програм (F12)"));
    toggle_panel_btn_->setCursor(Qt::PointingHandCursor);
    toggle_panel_btn_->setFocusPolicy(Qt::NoFocus);
    toggle_panel_btn_->setStyleSheet(
      QStringLiteral("QPushButton { background-color: #1e5638; color: #a7ffeb; border-radius: 4px; "
                     "padding: 3px 7px; font-size: 11px; font-weight: bold; }"
                     "QPushButton:hover { background-color: #276e48; }"));
    QObject::connect(toggle_panel_btn_, &QPushButton::clicked, this,
                     &Mk61MainWindow::toggleInspectorPanel);
    header_box->addWidget(toggle_panel_btn_);
    vbox->addLayout(header_box);

    // 2. Вакуумно-люмінесцентний індикатор (ВЛІ ІЛЦ2-12/8Л)
    auto *vfd_frame = new QFrame(face);
    vfd_frame->setFixedHeight(84);
    vfd_frame->setObjectName(QStringLiteral("vfdFrame"));
    vfd_frame->setStyleSheet(
      QStringLiteral("QFrame#vfdFrame { background-color: #05120c; border: 2px inset #163826; "
                     "border-radius: 8px; }"));
    auto *vfd_layout = new QVBoxLayout(vfd_frame);
    vfd_layout->setContentsMargins(8, 4, 8, 6);
    vfd_layout->setSpacing(2);

    status_badge_label_ = new QLabel(vfd_frame);
    status_badge_label_->setFixedHeight(16);
    status_badge_label_->setStyleSheet(
      QStringLiteral("color: #2bbd7e; font-size: 11px; font-weight: bold;"));
    auto *badge_row = new QHBoxLayout();
    badge_row->setContentsMargins(0, 0, 0, 0);
    badge_row->setSpacing(4);
    badge_row->addWidget(status_badge_label_, 1);
    auto *about_btn = new QPushButton(QStringLiteral("Про"), vfd_frame);
    about_btn->setCursor(Qt::PointingHandCursor);
    about_btn->setFocusPolicy(Qt::NoFocus);
    about_btn->setFixedHeight(16);
    about_btn->setToolTip(QStringLiteral("Версія, ліцензія GPL, Qt і шрифт індикатора"));
    about_btn->setStyleSheet(QStringLiteral(
      "QPushButton { background: transparent; color: #2bbd7e; border: 1px solid #1b5c3a; "
      "border-radius: 3px; font-size: 10px; padding: 0 5px; }"
      "QPushButton:hover { background-color: #123824; }"));
    QObject::connect(about_btn, &QPushButton::clicked, this, &Mk61MainWindow::showAbout);
    badge_row->addWidget(about_btn);
    vfd_layout->addLayout(badge_row);

    vfd_screen_ = new VfdScreenWidget(vfd_font_, vfd_frame);
    vfd_layout->addWidget(vfd_screen_);
    vbox->addWidget(vfd_frame);

    // 3. Панель перемикачів (Р - ГРД - Г та АВТ - ПРГ)
    auto *switches_frame = new QFrame(face);
    switches_frame->setFixedHeight(30);
    switches_frame->setStyleSheet(
      QStringLiteral("QFrame { background-color: #1f2226; border-radius: 6px; }"
                     "QRadioButton { color: #d8dee9; font-size: 12px; font-weight: bold; }"));
    auto *sw_layout = new QHBoxLayout(switches_frame);
    sw_layout->setContentsMargins(10, 3, 10, 3);

    auto *angle_group = new QWidget(switches_frame);
    auto *angle_layout = new QHBoxLayout(angle_group);
    angle_layout->setContentsMargins(0, 0, 0, 0);
    angle_layout->setSpacing(10);
    rad_btn_ = new QRadioButton(QStringLiteral("Р"), angle_group);
    deg_btn_ = new QRadioButton(QStringLiteral("ГРД"), angle_group);
    grad_btn_ = new QRadioButton(QStringLiteral("Г"), angle_group);
    rad_btn_->setFocusPolicy(Qt::NoFocus);
    deg_btn_->setFocusPolicy(Qt::NoFocus);
    grad_btn_->setFocusPolicy(Qt::NoFocus);
    rad_btn_->setChecked(true);
    angle_layout->addWidget(rad_btn_);
    angle_layout->addWidget(deg_btn_);
    angle_layout->addWidget(grad_btn_);

    QObject::connect(rad_btn_, &QRadioButton::toggled, this, [this](bool checked) {
      if (checked && !updating_ui_) {
        calc_.set_angle_mode(AngleMode::Radians);
        refreshAll();
      }
    });
    QObject::connect(deg_btn_, &QRadioButton::toggled, this, [this](bool checked) {
      if (checked && !updating_ui_) {
        calc_.set_angle_mode(AngleMode::Degrees);
        refreshAll();
      }
    });
    QObject::connect(grad_btn_, &QRadioButton::toggled, this, [this](bool checked) {
      if (checked && !updating_ui_) {
        calc_.set_angle_mode(AngleMode::Grads);
        refreshAll();
      }
    });

    auto *mode_group = new QWidget(switches_frame);
    auto *mode_layout = new QHBoxLayout(mode_group);
    mode_layout->setContentsMargins(0, 0, 0, 0);
    mode_layout->setSpacing(10);
    avt_btn_ = new QRadioButton(QStringLiteral("АВТ"), mode_group);
    prg_btn_ = new QRadioButton(QStringLiteral("ПРГ"), mode_group);
    avt_btn_->setFocusPolicy(Qt::NoFocus);
    prg_btn_->setFocusPolicy(Qt::NoFocus);
    avt_btn_->setChecked(true);
    mode_layout->addWidget(avt_btn_);
    mode_layout->addWidget(prg_btn_);

    QObject::connect(avt_btn_, &QRadioButton::toggled, this, [this](bool checked) {
      if (checked && !updating_ui_) {
        calc_.set_operating_mode(OperatingMode::Automatic);
        refreshAll();
      }
    });
    QObject::connect(prg_btn_, &QRadioButton::toggled, this, [this](bool checked) {
      if (checked && !updating_ui_) {
        stopRunningTimer();
        calc_.set_operating_mode(OperatingMode::Programming);
        refreshAll();
      }
    });

    sw_layout->addWidget(angle_group);
    sw_layout->addStretch();
    sw_layout->addWidget(mode_group);
    vbox->addWidget(switches_frame);

    // 4. Сітка 30 клавіш МК-61 (6 рядів x 5 стовпців) фіксованої висоти
    auto *keypad_frame = new QFrame(face);
    keypad_frame->setFixedHeight(422);
    keypad_frame->setStyleSheet(
      QStringLiteral("QFrame { background-color: #1b1e22; border-radius: 8px; }"));
    auto *grid = new QGridLayout(keypad_frame);
    grid->setContentsMargins(8, 6, 8, 6);
    grid->setHorizontalSpacing(7);
    grid->setVerticalSpacing(4);

    const auto &keys = mk61::core::all_keys();
    for (const auto &kinfo : keys) {
      auto *cell = new QWidget(keypad_frame);
      cell->setFixedHeight(64);
      auto *cell_layout = new QVBoxLayout(cell);
      cell_layout->setContentsMargins(0, 0, 0, 0);
      cell_layout->setSpacing(1);

      // Рядок надписів над клавішею: зліва жовтий (F), справа блакитний (К)
      auto *top_labels = new QHBoxLayout();
      top_labels->setContentsMargins(2, 0, 2, 0);
      auto *f_lbl = new QLabel(to_qstr(kinfo.f_label), cell);
      f_lbl->setFixedHeight(14);
      f_lbl->setStyleSheet(QStringLiteral("color: #f5c542; font-size: 11px; font-weight: bold;"));
      auto *k_lbl = new QLabel(to_qstr(kinfo.k_label), cell);
      k_lbl->setFixedHeight(14);
      k_lbl->setStyleSheet(QStringLiteral("color: #4fc3f7; font-size: 11px; font-weight: bold;"));
      k_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      top_labels->addWidget(f_lbl);
      top_labels->addWidget(k_lbl);
      cell_layout->addLayout(top_labels);

      // Сама кнопка фіксованої зручної висоти (36 px)
      auto *btn = new QPushButton(to_qstr(kinfo.primary_label), cell);
      btn->setFixedHeight(36);
      btn->setCursor(Qt::PointingHandCursor);
      btn->setFocusPolicy(Qt::NoFocus);
      btn->setStyleSheet(buttonStyleForKey(kinfo.key));

      const Key key_val = kinfo.key;
      if (key_val == Key::RunStop) {
        QObject::connect(btn, &QPushButton::clicked, this, &Mk61MainWindow::onRunStopButton);
      } else {
        QObject::connect(btn, &QPushButton::clicked, this,
                         [this, key_val]() { triggerKey(key_val); });
      }
      cell_layout->addWidget(btn);

      // Підпис буквеного регістра (a..e) під кнопкою (лише для рядів із регістрами a..e)
      const bool has_bot_reg = (!kinfo.reg_label.empty() && kinfo.reg_index.value_or(0) >= 10);
      auto *bot_lbl = new QLabel(has_bot_reg ? to_qstr(kinfo.reg_label) : QStringLiteral(""), cell);
      bot_lbl->setFixedHeight(11);
      bot_lbl->setAlignment(Qt::AlignCenter);
      bot_lbl->setStyleSheet(QStringLiteral("color: #ff8a65; font-size: 10px; font-weight: bold;"));
      cell_layout->addWidget(bot_lbl);

      grid->addWidget(cell, kinfo.row, kinfo.col);
    }

    vbox->addWidget(keypad_frame);
    return face;
  }

  static QString buttonStyleForKey(Key key) {
    if (key == Key::F) {
      return QStringLiteral(
        "QPushButton { background-color: #f0b90b; color: #1a1a1a; font-weight: bold; "
        "font-size: 14px; border-radius: 5px; border: 1px solid #c99700; }"
        "QPushButton:pressed { background-color: #c99700; }");
    }
    if (key == Key::K) {
      return QStringLiteral(
        "QPushButton { background-color: #1e88e5; color: #ffffff; font-weight: bold; "
        "font-size: 14px; border-radius: 5px; border: 1px solid #1565c0; }"
        "QPushButton:pressed { background-color: #1565c0; }");
    }
    if (key == Key::ClearX) {
      return QStringLiteral(
        "QPushButton { background-color: #d84315; color: #ffffff; font-weight: bold; "
        "font-size: 13px; border-radius: 5px; border: 1px solid #bf360c; }"
        "QPushButton:pressed { background-color: #bf360c; }");
    }
    switch (key) {
    case Key::D0:
    case Key::D1:
    case Key::D2:
    case Key::D3:
    case Key::D4:
    case Key::D5:
    case Key::D6:
    case Key::D7:
    case Key::D8:
    case Key::D9:
    case Key::Dot:
    case Key::Sign:
    case Key::Exp:
      return QStringLiteral(
        "QPushButton { background-color: #e0e3e8; color: #181a1d; font-weight: bold; "
        "font-size: 14px; border-radius: 5px; border: 1px solid #b0b6bf; }"
        "QPushButton:pressed { background-color: #c2c7d0; }");
    default:
      return QStringLiteral(
        "QPushButton { background-color: #383e47; color: #f0f4f8; font-weight: bold; "
        "font-size: 12px; border-radius: 5px; border: 1px solid #4f5663; }"
        "QPushButton:pressed { background-color: #292d34; }");
    }
  }

  QWidget *createInspectorPanel(QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *vbox = new QVBoxLayout(panel);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(8);

    QFont vfd_small = vfd_font_;
    vfd_small.setPixelSize(18);

    // Верхній ряд: операційний стек X, Y, Z, T, X1 та регістри R0..RE
    auto *top_split = new QHBoxLayout();
    top_split->setSpacing(8);

    // 1. Група операційного стека з мінімальною шириною колонки підписів і максимальною для значень
    auto *stack_group =
      new QGroupBox(QStringLiteral("Операційний стек ОПЗ (X, Y, Z, T, X1)"), panel);
    stack_group->setMinimumWidth(195);
    stack_group->setMaximumWidth(240);
    auto *stack_layout = new QGridLayout(stack_group);
    stack_layout->setContentsMargins(8, 8, 8, 8);
    stack_layout->setHorizontalSpacing(6);
    stack_layout->setVerticalSpacing(4);
    stack_layout->setColumnStretch(0, 0); // Підписи займають мінімум місця
    stack_layout->setColumnStretch(1, 1); // Поля значень розтягуються на максимум

    constexpr std::array<const char *, 5> kStackNames = {"T:", "Z:", "Y:", "X:", "X1:"};

    for (std::size_t i = 0; i < 5; ++i) {
      auto *name_lbl = new QLabel(QString::fromUtf8(kStackNames[i]), stack_group);
      name_lbl->setStyleSheet(QStringLiteral("font-weight: bold; color: #cfd8dc;"));
      name_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

      auto *val_lbl = new QLabel(QStringLiteral("0."), stack_group);
      val_lbl->setFont(vfd_small);
      val_lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
      val_lbl->setStyleSheet(
        (i == 3) ? QStringLiteral("color: #52ffaa; background-color: #0e291c; padding: 3px 8px; "
                                  "border: 1px solid #2bbd7e; border-radius: 4px;")
                 : QStringLiteral("color: #38e590; background-color: #081710; padding: 2px 8px; "
                                  "border: 1px solid #163626; border-radius: 4px;"));
      stack_labels_[i] = val_lbl;
      stack_layout->addWidget(name_lbl, static_cast<int>(i), 0);
      stack_layout->addWidget(val_lbl, static_cast<int>(i), 1);
    }
    return_stack_label_ = new QLabel(QStringLiteral("Стек ПП (SP=0): —"), stack_group);
    return_stack_label_->setStyleSheet(
      QStringLiteral("font-size: 11px; font-weight: bold; color: #90a4ae;"));
    stack_layout->addWidget(return_stack_label_, 5, 0, 1, 2);
    top_split->addWidget(stack_group, 0);

    // 2. Група 15 адресних регістрів R0..RE (стовпці Рег — компактні за вмістом, стовпці Значення —
    // однакової максимальної ширини!)
    auto *regs_group = new QGroupBox(
      QStringLiteral("Регістри пам'яті R0..RE (подвійний клік для редагування значення)"), panel);
    auto *regs_vbox = new QVBoxLayout(regs_group);
    regs_vbox->setContentsMargins(8, 8, 8, 8);
    regs_table_ = new QTableWidget(5, 6, regs_group);
    regs_table_->setHorizontalHeaderLabels({QStringLiteral("Рег"), QStringLiteral("Значення"),
                                            QStringLiteral("Рег"), QStringLiteral("Значення"),
                                            QStringLiteral("Рег"), QStringLiteral("Значення")});
    regs_table_->verticalHeader()->setVisible(false);
    regs_table_->verticalHeader()->setDefaultSectionSize(28);
    for (int col_group = 0; col_group < 3; ++col_group) {
      // Стовпчик підпису регістра — мінімальної достатньої ширини
      regs_table_->horizontalHeader()->setSectionResizeMode(col_group * 2,
                                                            QHeaderView::ResizeToContents);
      // Стовпчики значень — однакової максимальної ширини (Stretch ділить простір порівну між ними)
      regs_table_->horizontalHeader()->setSectionResizeMode(col_group * 2 + 1,
                                                            QHeaderView::Stretch);
    }
    regs_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    regs_table_->setFixedHeight(172);
    regs_table_->setStyleSheet(QStringLiteral(
      "QTableWidget { background-color: #0b1611; gridline-color: #1f3329; }"
      "QHeaderView::section { background-color: #1e242b; color: #dce3ea; font-weight: bold; "
      "padding: 2px 6px; border: 1px solid #2c3540; }"
      "QTableWidget::item:selected { background-color: #16462e; color: #69f0ae; }"));

    QFont hdr_font = font();
    hdr_font.setBold(true);

    for (int row = 0; row < 5; ++row) {
      for (int col_group = 0; col_group < 3; ++col_group) {
        const auto reg_idx = static_cast<std::uint8_t>(row + col_group * 5);
        const QString reg_title = (reg_idx < 10)
                                    ? QStringLiteral(" R%1 ").arg(reg_idx)
                                    : QStringLiteral(" R%1(%2) ")
                                        .arg(QChar(static_cast<char>('A' + reg_idx - 10)))
                                        .arg(to_qstr(mk61::core::reg_index_to_mk61_glyph(reg_idx)));
        auto *hdr_item = new QTableWidgetItem(reg_title);
        hdr_item->setFlags(Qt::ItemIsEnabled);
        hdr_item->setFont(hdr_font);
        hdr_item->setBackground(QBrush(QColor(0x1f, 0x26, 0x30)));
        hdr_item->setForeground(QBrush(QColor(0xff, 0xd5, 0x4f)));
        hdr_item->setTextAlignment(Qt::AlignCenter);
        regs_table_->setItem(row, col_group * 2, hdr_item);

        auto *val_item = new QTableWidgetItem(QStringLiteral("0."));
        val_item->setFont(vfd_small);
        val_item->setBackground(QBrush(QColor(0x09, 0x17, 0x10)));
        val_item->setForeground(QBrush(QColor(0x4a, 0xff, 0x9e)));
        val_item->setData(Qt::UserRole, static_cast<int>(reg_idx));
        regs_table_->setItem(row, col_group * 2 + 1, val_item);
      }
    }
    QObject::connect(regs_table_, &QTableWidget::itemChanged, this,
                     &Mk61MainWindow::onRegisterItemChanged);
    regs_vbox->addWidget(regs_table_);
    top_split->addWidget(regs_group, 1);
    vbox->addLayout(top_split);

    // Крок, автокрок і точки зупину.
    vbox->addWidget(createDebuggerBar(panel));

    // 4. Нижня частина: вкладки
    auto *tabs = new QTabWidget(panel);
    tabs->addTab(createAssemblerTab(tabs), QStringLiteral("Редактор програм & Приклади"));
    tabs->addTab(createProgramMemoryTab(tabs), QStringLiteral("Таблиця пам'яті та Відлагодження"));
    tabs->addTab(createInstructionTab(tabs), QStringLiteral("Інструкція МК-61"));
    vbox->addWidget(tabs, 1);

    return panel;
  }

  QWidget *createDebuggerBar(QWidget *parent) {
    auto *bar =
      new QGroupBox(QStringLiteral("Відлагодження та покрокове виконання програм"), parent);
    auto *hbox = new QHBoxLayout(bar);
    hbox->setContentsMargins(8, 4, 8, 4);
    hbox->setSpacing(6);

    auto *step_btn = new QPushButton(QStringLiteral("⏭ Крок (F10)"), bar);
    step_btn->setToolTip(
      QStringLiteral("Виконати рівно 1 крок програми з поточної адреси PC (гаряча клавіша F10 "
                     "або ШГ→ у режимі АВТ)"));
    step_btn->setStyleSheet(
      QStringLiteral("QPushButton { background-color: #1565c0; color: white; font-weight: bold; "
                     "padding: 4px 10px; border-radius: 4px; }"
                     "QPushButton:hover { background-color: #1976d2; }"));
    QObject::connect(step_btn, &QPushButton::clicked, this, &Mk61MainWindow::onDebugStepClicked);

    dbg_play_btn_ = new QPushButton(QStringLiteral("▶ Автокрок (F5)"), bar);
    dbg_play_btn_->setToolTip(QStringLiteral(
      "Запустити/зупинити покрокове виконання з вибраною швидкістю та зупинкою на точках зупину "
      "(F5)"));
    dbg_play_btn_->setStyleSheet(
      QStringLiteral("QPushButton { background-color: #2e7d32; color: white; font-weight: bold; "
                     "padding: 4px 10px; border-radius: 4px; }"
                     "QPushButton:hover { background-color: #388e3c; }"));
    QObject::connect(dbg_play_btn_, &QPushButton::clicked, this,
                     &Mk61MainWindow::onDebugPlayPauseClicked);

    auto *reset_pc_btn = new QPushButton(QStringLiteral("⏮ На 00 (В/О)"), bar);
    reset_pc_btn->setToolTip(QStringLiteral("Скинути лічильник команд PC на 00 та стек ПП"));
    QObject::connect(reset_pc_btn, &QPushButton::clicked, this, [this]() {
      stopRunningTimer();
      calc_.set_pc(0);
      refreshAll();
    });

    auto *bp_btn = new QPushButton(QStringLiteral("● Точка зупину (F9)"), bar);
    bp_btn->setToolTip(QStringLiteral(
      "Поставити або зняти точку зупину на поточному кроці (або двічі клікніть рядок у таблиці "
      "пам'яті)"));
    QObject::connect(bp_btn, &QPushButton::clicked, this, &Mk61MainWindow::toggleBreakpointAtPc);

    dbg_speed_combo_ = new QComboBox(bar);
    dbg_speed_combo_->addItem(QStringLiteral("Повільно (3 кр/с)"), 330);
    dbg_speed_combo_->addItem(QStringLiteral("Середньо (10 кр/с)"), 100);
    dbg_speed_combo_->addItem(QStringLiteral("Реальний МК-61 (25 кр/с)"), 40);
    dbg_speed_combo_->addItem(QStringLiteral("Швидко (100 кр/с)"), 10);
    dbg_speed_combo_->setCurrentIndex(2);
    QObject::connect(dbg_speed_combo_, &QComboBox::currentIndexChanged, this, [this](int /*idx*/) {
      run_timer_->setInterval(dbg_speed_combo_->currentData().toInt());
    });

    dbg_current_step_label_ = new QLabel(bar);
    dbg_current_step_label_->setStyleSheet(
      QStringLiteral("color: #80cbc4; font-weight: bold; font-size: 12px; padding-left: 6px;"));

    hbox->addWidget(step_btn);
    hbox->addWidget(dbg_play_btn_);
    hbox->addWidget(reset_pc_btn);
    hbox->addWidget(bp_btn);
    hbox->addWidget(dbg_speed_combo_);
    hbox->addWidget(dbg_current_step_label_, 1);
    return bar;
  }

  QWidget *createAssemblerTab(QWidget *parent) {
    auto *tab = new QWidget(parent);
    auto *vbox = new QVBoxLayout(tab);

    auto *preset_row = new QHBoxLayout();
    preset_row->addWidget(new QLabel(QStringLiteral("Приклади:"), tab));
    preset_combo_ = new QComboBox(tab);
    preset_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    preset_combo_->setMinimumContentsLength(16);
    preset_combo_->addItem(QStringLiteral("— Оберіть класичну програму —"), QStringLiteral(""));
    for (std::size_t idx = 0; idx < mk61::core::builtin_programs().size(); ++idx) {
      const auto &p = mk61::core::builtin_programs()[idx];
      preset_combo_->addItem(to_qstr(p.title), to_qstr(p.id));
      preset_combo_->setItemData(static_cast<int>(idx + 1), to_qstr(p.description),
                                 Qt::ToolTipRole);
    }
    preset_row->addWidget(preset_combo_, 1);

    auto *load_preset_btn = new QPushButton(QStringLiteral("Завантажити"), tab);
    QObject::connect(load_preset_btn, &QPushButton::clicked, this,
                     &Mk61MainWindow::onLoadPresetClicked);
    preset_row->addWidget(load_preset_btn);
    vbox->addLayout(preset_row);

    preset_hint_label_ = new QLabel(
      QStringLiteral("Підказка: пишіть команди мнемоніками МК-61, у форматі таблиці 10×10 або "
                     "оберіть приклад вище. Для покрокового відлагодження натискайте F10 (Крок)."),
      tab);
    preset_hint_label_->setWordWrap(true);
    preset_hint_label_->setStyleSheet(QStringLiteral(
      "background-color: #242014; color: #ffe082; padding: 5px 8px; border-radius: 4px; "
      "border: 1px solid #66521e;"));
    vbox->addWidget(preset_hint_label_);

    asm_editor_ = new QPlainTextEdit(tab);
    QFont editor_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    editor_font.setPointSize(11);
    asm_editor_->setFont(editor_font);
    asm_editor_->setPlaceholderText(
      QStringLiteral("# Введіть програму мнемоніками, таблицею 10x10 або HEX-кодами:\n"
                     "Х→П 0\n"
                     "1\n"
                     "loop:\n"
                     "П→Х 0\n"
                     "×\n"
                     "F L0 loop\n"
                     "С/П\n"));
    vbox->addWidget(asm_editor_, 1);

    auto *btn_row = new QHBoxLayout();
    auto *assemble_btn = new QPushButton(QStringLiteral("Записати в МК-61"), tab);
    assemble_btn->setStyleSheet(
      QStringLiteral("QPushButton { background-color: #2e7d32; color: white; font-weight: bold; "
                     "padding: 6px 12px; border-radius: 4px; }"));
    QObject::connect(assemble_btn, &QPushButton::clicked, this, &Mk61MainWindow::onAssembleClicked);

    auto *disasm_btn = new QPushButton(QStringLiteral("Лістинг"), tab);
    QObject::connect(disasm_btn, &QPushButton::clicked, this,
                     &Mk61MainWindow::onDisassembleToEditorClicked);

    auto *grid_btn = new QPushButton(QStringLiteral("Таблиця 10×10"), tab);
    QObject::connect(grid_btn, &QPushButton::clicked, this,
                     &Mk61MainWindow::onDisassembleGridToEditorClicked);

    auto *open_btn = new QPushButton(QStringLiteral("Відкрити..."), tab);
    QObject::connect(open_btn, &QPushButton::clicked, this, &Mk61MainWindow::onOpenFileClicked);

    auto *save_btn = new QPushButton(QStringLiteral("Зберегти..."), tab);
    QObject::connect(save_btn, &QPushButton::clicked, this, &Mk61MainWindow::onSaveFileClicked);

    btn_row->addWidget(assemble_btn);
    btn_row->addWidget(disasm_btn);
    btn_row->addWidget(grid_btn);
    btn_row->addStretch();
    btn_row->addWidget(open_btn);
    btn_row->addWidget(save_btn);
    vbox->addLayout(btn_row);

    return tab;
  }

  QWidget *createProgramMemoryTab(QWidget *parent) {
    auto *tab = new QWidget(parent);
    auto *vbox = new QVBoxLayout(tab);

    auto *info_lbl = new QLabel(
      QStringLiteral("Одинарний клік — переставити PC на крок. Подвійний клік — поставити/зняти "
                     "точку зупину (●). Покрокове виконання: кнопка «⏭ Крок (F10)»."),
      tab);
    vbox->addWidget(info_lbl);

    prog_table_ = new QTableWidget(0, 4, tab);
    prog_table_->setHorizontalHeaderLabels({QStringLiteral("Крок"), QStringLiteral("Код на ВЛІ"),
                                            QStringLiteral("Мнемоніка"),
                                            QStringLiteral("Опис операції")});
    prog_table_->verticalHeader()->setVisible(false);
    prog_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    prog_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    prog_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    prog_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    prog_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    prog_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    prog_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    prog_table_->setStyleSheet(
      QStringLiteral("QTableWidget::item:selected { background-color: #16462e; color: #69f0ae; }"));

    QObject::connect(prog_table_, &QTableWidget::cellClicked, this, [this](int row, int /*col*/) {
      if (auto *item = prog_table_->item(row, 0)) {
        const int step = item->data(Qt::UserRole).toInt();
        calc_.set_pc(step);
        refreshAll();
      }
    });

    QObject::connect(prog_table_, &QTableWidget::cellDoubleClicked, this,
                     [this](int row, int /*col*/) {
                       if (auto *item = prog_table_->item(row, 0)) {
                         const int step = item->data(Qt::UserRole).toInt();
                         if (breakpoints_.contains(step)) {
                           breakpoints_.erase(step);
                         } else {
                           breakpoints_.insert(step);
                         }
                         refreshAll();
                       }
                     });

    vbox->addWidget(prog_table_, 1);
    return tab;
  }

  QWidget *createInstructionTab(QWidget *parent) {
    auto *browser = new QTextBrowser(parent);
    browser->setStyleSheet(QStringLiteral(
      "QTextBrowser { background-color: #12161c; color: #e2e8f0; border: 1px solid #2c3540; "
      "padding: 8px; selection-background-color: #1e5638; selection-color: #ffffff; }"));

    browser->setHtml(QStringLiteral(R"(
      <html>
      <head>
      <style>
        body {
          background-color: #12161c;
          color: #e2e8f0;
          font-size: 13px;
          line-height: 1.45;
        }
        h1 {
          color: #4aff9e;
          font-size: 19px;
          border-bottom: 2px solid #1f4d36;
          padding-bottom: 6px;
          margin-top: 4px;
        }
        h2 {
          color: #ffd54f;
          background-color: #1b222c;
          font-size: 15px;
          padding: 6px 10px;
          border-left: 4px solid #f0b90b;
          margin-top: 18px;
          margin-bottom: 8px;
        }
        h3 {
          color: #80deea;
          font-size: 13px;
          margin-top: 12px;
          margin-bottom: 4px;
        }
        p, li {
          color: #dce3ea;
          margin-top: 4px;
          margin-bottom: 4px;
        }
        /* Бейджі фізичних клавіш МК-61 */
        .kf {
          background-color: #f0b90b;
          color: #141619;
          font-weight: bold;
          padding: 1px 6px;
        }
        .kk {
          background-color: #1e88e5;
          color: #ffffff;
          font-weight: bold;
          padding: 1px 6px;
        }
        .kcx {
          background-color: #d84315;
          color: #ffffff;
          font-weight: bold;
          padding: 1px 6px;
        }
        .knum {
          background-color: #e0e3e8;
          color: #141619;
          font-weight: bold;
          padding: 1px 6px;
        }
        .kop {
          background-color: #383e47;
          color: #ffffff;
          font-weight: bold;
          padding: 1px 6px;
        }
        /* Надклавішні позначення F (жовті) та К (блакитні) */
        .ffn {
          background-color: #2b230f;
          color: #ffd54f;
          font-weight: bold;
          padding: 1px 5px;
        }
        .kfn {
          background-color: #0d2538;
          color: #4fc3f7;
          font-weight: bold;
          padding: 1px 5px;
        }
        /* Регістри стека та пам'яті */
        .reg {
          background-color: #0e291c;
          color: #69f0ae;
          font-weight: bold;
          font-family: monospace;
          padding: 1px 5px;
        }
        /* Показання вакуумно-люмінесцентного індикатора (ВЛІ) */
        .vfd {
          background-color: #05120c;
          color: #4aff9e;
          font-weight: bold;
          font-family: monospace;
          padding: 1px 6px;
        }
        /* Важливі попередження та примітки */
        .note {
          background-color: #1a2430;
          color: #b3e5fc;
          padding: 8px 10px;
          margin-top: 6px;
          margin-bottom: 8px;
        }
        .warn {
          background-color: #2b1d16;
          color: #ffcc80;
          padding: 8px 10px;
          margin-top: 6px;
          margin-bottom: 8px;
        }
        table {
          border-collapse: collapse;
          width: 100%;
          margin-top: 6px;
          margin-bottom: 10px;
        }
        th {
          background-color: #1f2833;
          color: #ffd54f;
          font-weight: bold;
          padding: 5px 7px;
          border: 1px solid #323f4f;
          text-align: left;
        }
        td {
          background-color: #161c24;
          color: #e2e8f0;
          padding: 4px 7px;
          border: 1px solid #283240;
        }
      </style>
      </head>
      <body>

      <h1>Електроніка МК-61</h1>
      <p>Це емулятор того калькулятора, який стояв на столі: ті самі клавіші, стек і коди команд.
      Руками рахується так само, програма вміщує до <b>105 кроків</b>.</p>
      <p>Бічна панель показує стек, регістри і пам'ять програм. <span class="kop">F12</span>
      (кнопка <span class="kop">Тільки МК-61</span>) її ховає. Кнопка <b>Про</b> біля рядка режиму
      на індикаторі — версія і ліцензії. Готові приклади і свій текст програми — на вкладці
      «Редактор програм»: мнемоніки, мітки або таблиця 10×10, потім «Записати в МК-61».</p>
      <p>З клавіатури комп'ютера цифри і <span class="kop">+ − * /</span> збігаються з корпусом.
      <span class="kop">Enter</span> — <span class="kop">В↑</span>, <span class="kcx">Esc</span> і
      Backspace — <span class="kcx">Сх</span>, пробіл — <span class="kop">С/П</span>, Home —
      <span class="kop">В/О</span>. <span class="kop">F10</span> робить один крок,
      <span class="kop">F5</span> пускає автокрок, <span class="kop">F9</span> ставить точку зупину.
      Подвійний клік по кроку у вкладці пам'яті робить те саме.</p>
      <p>Далі — як влаштований сам МК-61. Якщо колись працювали з Б3-34, більшість буде знайома;
      є одна пастка в умовних переходах, про неї в розділі 6.</p>

      <h2>1. Загальні відомості, індикатор та перемикачі</h2>
      <ul>
        <li><b>Індикатор ВЛІ (12 розрядів)</b>: 8 розрядів відведено під мантису числа (разом із
            десятковою крапкою), 1 розряд зліва — під знак мантиси, 2 розряди справа — під порядок
            числа (від <span class="vfd">-99</span> до <span class="vfd">+99</span>) або двозначну
            адресу кроку програми (<span class="vfd">00</span>..<span class="vfd">99</span>,
            <span class="vfd">-0</span>..<span class="vfd">-4</span>), і ще 1 розряд перед порядком —
            під знак порядку.</li>
        <li><b>Діапазон чисел</b>: за абсолютною величиною від <span class="vfd">10⁻⁹⁹</span> до
            <span class="vfd">9.9999999 × 10⁹⁹</span>. При виході результату за верхню межу або при
            неприпустимій математичній операції (ділення на нуль, корінь із від'ємного числа,
            логарифм числа ≤ 0, переповнення стека підпрограм) на індикаторі висвічується аварійний
            сигнал <span class="vfd">&nbsp;ЕГГОГ&nbsp;</span>. Для скидання помилки натисніть
            червону клавішу <span class="kcx">Сх</span>.</li>
        <li><b>Перемикач одиниць кутів</b>:
            <span class="kop">Р</span> — радіани,
            <span class="kop">ГРД</span> — градуси (360° у колі),
            <span class="kop">Г</span> — гради (400 град у колі, 100 град = 90°).</li>
        <li><b>Режими роботи калькулятора</b>:
            <ul>
              <li><b>Автоматичний режим (<span class="ffn">АВТ</span>)</b> — вмикається під час
                  запуску або комбінацією <span class="kf">F</span> <span class="knum">/-/</span>
                  (<span class="ffn">АВТ</span>). Слугує для ручних обчислень та виконання програм.</li>
              <li><b>Режим програмування (<span class="ffn">ПРГ</span>)</b> — вмикається комбінацією
                  <span class="kf">F</span> <span class="knum">ВП</span>
                  (<span class="ffn">ПРГ</span>). Слугує для запису, перегляду та редагування
                  кроків програми в пам'яті.</li>
            </ul>
        </li>
        <li><b>Модифікатори (префіксні клавіші)</b>:
            більшість із 30 клавіш мають три призначення:
            <ol>
              <li><b>Основне</b> — нанесене безпосередньо на поверхні клавіші;</li>
              <li><b>Жовте (префікс <span class="kf">F</span>)</b> — викликається попереднім
                  натисканням жовтої клавіші <span class="kf">F</span> (надписи над клавішами
                  ліворуч, напр. <span class="kf">F</span> <span class="knum">7</span> →
                  <span class="ffn">sin</span>);</li>
              <li><b>Синє (префікс <span class="kk">К</span>)</b> — викликається попереднім
                  натисканням синьої клавіші <span class="kk">К</span> (надписи над клавішами
                  праворуч, напр. <span class="kk">К</span> <span class="knum">7</span> →
                  <span class="kfn">[x]</span>).</li>
            </ol>
            Якщо префікс <span class="kf">F</span> або <span class="kk">К</span> натиснуто помилково,
            його можна скасувати комбінацією <span class="kf">F</span> <span class="kcx">Сх</span>
            (<span class="ffn">CF</span>).</li>
      </ul>

      <h2>2. Введення чисел та виправлення помилок</h2>
      <ul>
        <li><b>Цілі та дробові числа</b> набираються цифровими клавішами
            <span class="knum">0</span>..<span class="knum">9</span> та клавішею десяткової крапки
            <span class="knum">.</span> (наприклад: <span class="knum">3</span>
            <span class="knum">.</span> <span class="knum">1</span> <span class="knum">4</span>).</li>
        <li><b>Зміна знака числа</b> виконується клавішею <span class="knum">/-/</span>
            <b>після</b> введення мантиси (наприклад, для введення <code>-25.4</code> натисніть
            <span class="knum">2</span> <span class="knum">5</span> <span class="knum">.</span>
            <span class="knum">4</span> <span class="knum">/-/</span>).</li>
        <li><b>Введення числа з порядком (експонентою)</b>:
            введіть мантису, натисніть <span class="knum">ВП</span> («Введення порядку»), введіть
            дві цифри порядку і, якщо порядок від'ємний, натисніть <span class="knum">/-/</span>.
            <br><i>Приклад</i>: для введення <code>-1.6 × 10⁻¹⁹</code> натисніть:
            <span class="knum">1</span> <span class="knum">.</span> <span class="knum">6</span>
            <span class="knum">/-/</span> <span class="knum">ВП</span> <span class="knum">1</span>
            <span class="knum">9</span> <span class="knum">/-/</span>
            (на індикаторі: <span class="vfd">-1.6 &nbsp;&nbsp;&nbsp;&nbsp;-19</span>).</li>
        <li><b>Очищення поточного числа</b>: клавіша <span class="kcx">Сх</span> («Скидання X»)
            обнуляє тільки видимий регістр <span class="reg">X</span> (<span class="vfd">0.</span>),
            не змінюючи вміст інших регістрів стека (<span class="reg">Y</span>,
            <span class="reg">Z</span>, <span class="reg">T</span>) та пам'яті.</li>
      </ul>

      <h2>3. Операційний стек ОПЗ (X, Y, Z, T) та регістр X1</h2>
      <p>У мікрокалькуляторі МК-61 використовується <b>зворотний польський запис (ОПЗ)</b> і
      чотирирегістровий автоматичний стек (<span class="reg">X</span>, <span class="reg">Y</span>,
      <span class="reg">Z</span>, <span class="reg">T</span>) плюс допоміжний регістр попереднього
      аргументу <span class="reg">X1</span>. Клавіші <code>=</code> та дужок немає — кожна операція
      виконується одразу над числами у вершині стека.</p>

      <table>
        <tr>
          <th>Дія / Клавіша</th>
          <th>Як змінюються регістри стека</th>
          <th>Особливості автопідняття стека</th>
        </tr>
        <tr>
          <td>Натискання <span class="kop">В↑</span></td>
          <td><span class="reg">T</span> ← <span class="reg">Z</span> ← <span class="reg">Y</span> ← <span class="reg">X</span>
              (у <span class="reg">X</span> та <span class="reg">Y</span> опиняється одне й те саме число)</td>
          <td>Наступне введене з клавіатури число <b>заміщує</b> <span class="reg">X</span> без повторного підйому стека.</td>
        </tr>
        <tr>
          <td>Одномісна функція (<span class="ffn">sin</span>, <span class="ffn">√</span>, <span class="ffn">x²</span>, <span class="ffn">1/x</span>, <span class="kfn">[x]</span> тощо)</td>
          <td>Старе значення <span class="reg">X</span> зберігається в <span class="reg">X1</span>;
              результат записується в <span class="reg">X</span>. Регістри <span class="reg">Y</span>,
              <span class="reg">Z</span>, <span class="reg">T</span> не змінюються.</td>
          <td>Вмикає автопідняття: наступне введене число автоматично підніме стек.</td>
        </tr>
        <tr>
          <td>Двомісна операція (<span class="kop">+</span>, <span class="kop">-</span>,
              <span class="kop">×</span>, <span class="kop">÷</span>, <span class="ffn">xʸ</span>)</td>
          <td>Старе значення <span class="reg">X</span> йде в <span class="reg">X1</span>;
              результат операції над <span class="reg">Y</span> та <span class="reg">X</span>
              записується в <span class="reg">X</span>. Стек опускається:
              <span class="reg">Y</span> ← <span class="reg">Z</span>,
              <span class="reg">Z</span> ← <span class="reg">T</span>, а в
              <span class="reg">T</span> лишається те саме число: воно копіюється вниз.</td>
          <td>Вмикає автопідняття стека для наступного числа.</td>
        </tr>
        <tr>
          <td>Обмін <span class="kop">↔</span></td>
          <td>Міняє місцями <span class="reg">X</span> ↔ <span class="reg">Y</span>
              (<span class="reg">Z</span>, <span class="reg">T</span>, <span class="reg">X1</span> без змін).</td>
          <td>Вмикає автопідняття стека.</td>
        </tr>
        <tr>
          <td>Кільцевий зсув <span class="kf">F</span> <span class="kop">В↑</span> (<span class="ffn">В↻</span>)</td>
          <td>Кільцеве прокручування стека вниз:
              <span class="reg">X</span> → <span class="reg">T</span> → <span class="reg">Z</span> →
              <span class="reg">Y</span> → <span class="reg">X</span>.</td>
          <td>Дозволяє переглянути всі 4 регістри стека по колу.</td>
        </tr>
        <tr>
          <td>Попередній результат <span class="kf">F</span> <span class="kop">+</span> (<span class="ffn">Вх</span>)</td>
          <td>Піднімає стек (якщо автопідняття активне) і копіює вміст <span class="reg">X1</span> у <span class="reg">X</span>.</td>
          <td>Зручно для виправлення помилок і повторного використання аргументу.</td>
        </tr>
      </table>

      <div class="note">
        <b>Приклади обчислень у стеку ОПЗ:</b><br>
        • <b>Обчислити <code>(2 + 3) × (7 - 4) = 15</code></b>:<br>
          <span class="knum">2</span> <span class="kop">В↑</span> <span class="knum">3</span>
          <span class="kop">+</span> (у <span class="reg">X</span> = <span class="vfd">5.</span>) →
          <span class="knum">7</span> <span class="kop">В↑</span> <span class="knum">4</span>
          <span class="kop">-</span> (у <span class="reg">Y</span> = <span class="vfd">5.</span>,
          у <span class="reg">X</span> = <span class="vfd">3.</span>) →
          <span class="kop">×</span> → результат <span class="vfd">15.</span><br>
        • <b>Розмноження константи через регістр <span class="reg">T</span></b>:
          якщо заповнити стек константою <code>c</code> (<span class="knum">c</span>
          <span class="kop">В↑</span> <span class="kop">В↑</span> <span class="kop">В↑</span>),
          то при кожній бінарній операції значення з <span class="reg">T</span> буде автоматично
          тиражуватися вниз у стек!
      </div>

      <h2>4. Повний довідник математичних, кутових і логічних операцій</h2>

      <h3>4.1. Степеневі, показнимі, логарифмічні та тригонометричні функції (префікс F)</h3>
      <table>
        <tr>
          <th>Натискання клавіш</th>
          <th>Позначення</th>
          <th>Код</th>
          <th>Формула та особливості виконання</th>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="knum">.</span></td>
          <td><span class="ffn">π</span></td>
          <td><span class="vfd">20</span></td>
          <td>Заносить у стек <span class="reg">X</span> константу <b>π = 3.1415926</b> (піднімаючи стек).</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="kop">-</span></td>
          <td><span class="ffn">√</span></td>
          <td><span class="vfd">21</span></td>
          <td>Квадратний корінь <b>√X</b> (при <span class="reg">X</span> &lt; 0 видає <span class="vfd">ЕГГОГ</span>).</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="kop">×</span></td>
          <td><span class="ffn">x²</span></td>
          <td><span class="vfd">22</span></td>
          <td>Піднесення до квадрата <b>X²</b>.</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="kop">÷</span></td>
          <td><span class="ffn">1/x</span></td>
          <td><span class="vfd">23</span></td>
          <td>Обернена величина <b>1 / X</b> (при <span class="reg">X</span> = 0 видає <span class="vfd">ЕГГОГ</span>).</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="kop">↔</span></td>
          <td><span class="ffn">xʸ</span></td>
          <td><span class="vfd">24</span></td>
          <td>Піднесення до степеня <b>Y<sup>X</sup></b>: основа в <span class="reg">Y</span>, показник у <span class="reg">X</span>.<br>
              <i>Приклад 2¹⁰ = 1024</i>: <span class="knum">2</span> <span class="kop">В↑</span> <span class="knum">1</span> <span class="knum">0</span> <span class="kf">F</span> <span class="kop">↔</span>. (При <span class="reg">Y</span> ≤ 0 видає <span class="vfd">ЕГГОГ</span>).</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="knum">0</span> / <span class="kf">F</span> <span class="knum">1</span></td>
          <td><span class="ffn">10ˣ</span> / <span class="ffn">eˣ</span></td>
          <td><span class="vfd">15</span> / <span class="vfd">16</span></td>
          <td>Десяткова експонента <b>10<sup>X</sup></b> та натуральна експонента <b>e<sup>X</sup></b>.</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="knum">2</span> / <span class="kf">F</span> <span class="knum">3</span></td>
          <td><span class="ffn">lg</span> / <span class="ffn">ln</span></td>
          <td><span class="vfd">17</span> / <span class="vfd">18</span></td>
          <td>Десятковий логарифм <b>lg(X)</b> та натуральний логарифм <b>ln(X)</b> (при <span class="reg">X</span> ≤ 0 — <span class="vfd">ЕГГОГ</span>).</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="knum">7</span> / <span class="knum">8</span> / <span class="knum">9</span></td>
          <td><span class="ffn">sin</span> / <span class="ffn">cos</span> / <span class="ffn">tg</span></td>
          <td><span class="vfd">1C</span> / <span class="vfd">1D</span> / <span class="vfd">1E</span></td>
          <td>Тригонометричні функції кута <span class="reg">X</span> згідно з перемикачем <b>Р / ГРД / Г</b>.</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="knum">4</span> / <span class="knum">5</span> / <span class="knum">6</span></td>
          <td><span class="ffn">sin⁻¹</span> / <span class="ffn">cos⁻¹</span> / <span class="ffn">tg⁻¹</span></td>
          <td><span class="vfd">19</span> / <span class="vfd">1A</span> / <span class="vfd">1B</span></td>
          <td>Арксинус, арккосинус (при |<span class="reg">X</span>| &gt; 1 — <span class="vfd">ЕГГОГ</span>) та арктангенс; результат у вибраній мірі кутів.</td>
        </tr>
      </table>

      <h3>4.2. Спеціальні функції, переведення кутів і часу та побітова логіка (префікс К)</h3>
      <table>
        <tr>
          <th>Натискання клавіш</th>
          <th>Позначення</th>
          <th>Код</th>
          <th>Опис операції та формат даних</th>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="knum">7</span> / <span class="kk">К</span> <span class="knum">8</span></td>
          <td><span class="kfn">[x]</span> / <span class="kfn">{x}</span></td>
          <td><span class="vfd">34</span> / <span class="vfd">35</span></td>
          <td>Виділення цілої частини числа <b>[X]</b> (напр. <code>-3.7 → -3.</code>) та дробової частини <b>{X}</b> (<code>-3.7 → -0.7</code>).</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="knum">4</span> / <span class="kk">К</span> <span class="knum">5</span></td>
          <td><span class="kfn">|x|</span> / <span class="kfn">ЗН</span></td>
          <td><span class="vfd">31</span> / <span class="vfd">32</span></td>
          <td>Модуль числа <b>|X|</b> та знак числа <b>sign(X)</b> (повертає <code>-1</code>, <code>0</code> або <code>1</code>).</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="knum">9</span></td>
          <td><span class="kfn">max</span></td>
          <td><span class="vfd">36</span></td>
          <td>Вибирає більше зі значень <span class="reg">X</span> та <span class="reg">Y</span> і записує в <span class="reg">X</span>. Стек при цьому не опускається, <span class="reg">Y</span> лишається.</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="knum">6</span></td>
          <td><span class="kfn">° ′</span> (Г→М)</td>
          <td><span class="vfd">33</span></td>
          <td>Переведення градусів (годин) із десятковими частками (<code>ГГ.гггг</code>) у градуси, мінути й частки мінути (<code>ГГ.ММмм</code>).</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="kop">+</span></td>
          <td><span class="kfn">° ′</span> (М→Г)</td>
          <td><span class="vfd">26</span></td>
          <td>Переведення градусів і мінут (<code>ГГ.ММмм</code>) у десяткові частки градуса (<code>ГГ.гггг</code>). При мінутах ≥ 60 — <span class="vfd">ЕГГОГ</span>.</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="knum">3</span></td>
          <td><span class="kfn">° ′ ″</span> (Г→МС)</td>
          <td><span class="vfd">30</span></td>
          <td>Переведення десяткових часток градуса (<code>ГГ.гггг</code>) у градуси, мінути й секунди (<code>ГГ.ММССс</code>).</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="kop">↔</span></td>
          <td><span class="kfn">° ′ ″</span> (МС→Г)</td>
          <td><span class="vfd">2A</span></td>
          <td>Переведення градусів, мінут і секунд (<code>ГГ.ММССс</code>) у десяткові частки градуса (<code>ГГ.гггг</code>). При мінутах чи секундах ≥ 60 — <span class="vfd">ЕГГОГ</span>.</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="kop">В↑</span></td>
          <td><span class="kfn">СЧ</span></td>
          <td><span class="vfd">3B</span></td>
          <td>Генератор псевдовипадкового числа в діапазоні <b>[0; 1)</b> (після ввімкнення перше число — <span class="vfd">0.404067</span>).</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="knum">0</span></td>
          <td><span class="kfn">НОП</span></td>
          <td><span class="vfd">54</span></td>
          <td>Порожня операція («Немає операції»); коди <span class="vfd">55</span> (<span class="kk">К</span> <span class="knum">1</span>) і <span class="vfd">56</span> (<span class="kk">К</span> <span class="knum">2</span>) діють аналогічно.</td>
        </tr>
        <tr>
          <td><span class="kk">К</span> <span class="knum">.</span> / <span class="knum">/-/</span> / <span class="knum">ВП</span> / <span class="kcx">Сх</span></td>
          <td><span class="kfn">∧</span> / <span class="kfn">∨</span> / <span class="kfn">⊕</span> / <span class="kfn">инв</span></td>
          <td><span class="vfd">37</span>..<span class="vfd">3A</span></td>
          <td>Порозрядні логічні операції <b>І (AND)</b>, <b>АБО (OR)</b>, <b>Виключне АБО (XOR)</b> та унарна <b>Інверсія (NOT)</b> над 7 шістнадцятковими тетрадами мантиси у форматі <span class="vfd">8.xxxxxxx</span>.<br>
              Шістнадцяткові цифри 10..15 на ВЛІ: <code>10(A)</code>=<span class="vfd">-</span>, <code>11(B)</code>=<span class="vfd">L</span>, <code>12(C)</code>=<span class="vfd">C</span>, <code>13(D)</code>=<span class="vfd">Г</span>, <code>14(E)</code>=<span class="vfd">E</span>, <code>15(F)</code>=пробіл.<br>
              <i>Приклад</i>: <span class="knum">123456</span> <span class="kk">К</span> <span class="kcx">Сх</span> (<span class="kfn">инв</span>) → <span class="vfd">8.ГCL-9 &nbsp;</span>.</td>
        </tr>
      </table>

      <h2>5. Регістри пам'яті (R0..R9, RA..RE)</h2>
      <p>Калькулятор має <b>15 незалежних адресних регістрів пам'яті</b>: десять цифрових
      (<span class="reg">R0</span>..<span class="reg">R9</span>) та п'ять буквених
      (<span class="reg">RA</span>..<span class="reg">RE</span>), позначених червоними літерами під
      нижнім рядом клавіш:</p>
      <ul>
        <li><span class="knum">.</span> — регістр <span class="reg">a</span> (<span class="reg">RA</span>, на ВЛІ відображається як <span class="vfd">-</span>);</li>
        <li><span class="knum">/-/</span> — регістр <span class="reg">b</span> (<span class="reg">RB</span>, на ВЛІ <span class="vfd">L</span>);</li>
        <li><span class="knum">ВП</span> — регістр <span class="reg">c</span> (<span class="reg">RC</span>, на ВЛІ <span class="vfd">C</span>);</li>
        <li><span class="kcx">Сх</span> — регістр <span class="reg">d</span> (<span class="reg">RD</span>, на ВЛІ <span class="vfd">Г</span>);</li>
        <li><span class="kop">В↑</span> — регістр <span class="reg">e</span> (<span class="reg">RE</span>, на ВЛІ <span class="vfd">E</span>).</li>
      </ul>
      <p><b>Прямий запис у пам'ять</b>: натисніть <span class="kop">Х→П</span> (у літературі також
      позначається <code>П</code>) та клавішу номера регістра (коди <span class="vfd">40</span>..<span class="vfd">4E</span>). Число в <span class="reg">X</span> зберігається.</p>
      <p><b>Пряме читання з пам'яті</b>: натисніть <span class="kop">П→Х</span> (у літературі
      <code>ИП</code>) та клавішу номера регістра (коди <span class="vfd">60</span>..<span class="vfd">6E</span>). Якщо автопідняття стека активне, стек попередньо піднімається; проте якщо перед <span class="kop">П→Х</span> було натиснуто <span class="kop">В↑</span>, стек вдруге <b>не піднімається</b>.</p>

      <h2>6. Програмування (ПРГ), умовні переходи, цикли та непряма адресація</h2>
      <ul>
        <li>Пам'ять програм вміщує <b>105 кроків</b> із адресами від <span class="vfd">00</span> до
            <span class="vfd">99</span> та від <span class="vfd">-0</span> (крок 100) до
            <span class="vfd">-4</span> (крок 104). Після виконання кроку 104 лічильник команд
            <code>PC</code> автоматично переходить на <span class="vfd">00</span>.</li>
        <li>У режимі <span class="ffn">ПРГ</span> праворуч на індикаторі виводиться поточна адреса
            кроку <code>PC</code>, а ліворуч — коди трьох попередніх записаних кроків
            (<code>[PC-1] [PC-2] [PC-3]</code>). Переміщення по кроках у режимі <span class="ffn">ПРГ</span>
            здійснюється клавішами <span class="kop">ШГ→</span> (вперед) та <span class="kop">←ШГ</span> (назад).</li>
        <li>Команди переходів <span class="kop">БП</span>, <span class="kop">ПП</span>, умовні
            переходи <span class="kf">F</span> <span class="ffn">x...</span> та цикли
            <span class="kf">F</span> <span class="ffn">L0..L3</span> займають у пам'яті
            <b>2 кроки (2 байти)</b>: перший крок — код самої операції, другий крок — двозначна адреса
            переходу (для переходів на кроки 100..104 першою цифрою адреси натискається
            <span class="knum">.</span> (<span class="reg">a</span>), а другою — <span class="knum">0</span>..<span class="knum">4</span>).</li>
      </ul>

      <div class="warn">
        <b>Умовний перехід тут працює навпаки, ніж у звичних мовах.</b><br>
        На Б3-34 і МК-61 команда дивиться на регістр <span class="reg">X</span> і робить так:
        <ul>
          <li>умова істинна — переходу немає, виконується команда одразу за адресою (<code>PC + 2</code>);</li>
          <li>умова хибна — стрибок на вказану адресу.</li>
        </ul>
        Тобто гілка «якщо так» пишеться слідом за командою, а «якщо ні» — за адресою.
      </div>

      <table>
        <tr>
          <th>Комбінація клавіш</th>
          <th>Мнемоніка</th>
          <th>Код</th>
          <th>Коли продовжує далі (PC+2)</th>
          <th>Коли стрибає на адресу XX</th>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="kop">ШГ→</span> + адреса</td>
          <td><span class="ffn">F x&lt;0</span></td>
          <td><span class="vfd">5C XX</span></td>
          <td>Якщо <span class="reg">X</span> &lt; 0</td>
          <td>Якщо <span class="reg">X</span> ≥ 0 (умова x&lt;0 хибна)</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="kop">←ШГ</span> + адреса</td>
          <td><span class="ffn">F x=0</span></td>
          <td><span class="vfd">5E XX</span></td>
          <td>Якщо <span class="reg">X</span> = 0</td>
          <td>Якщо <span class="reg">X</span> ≠ 0 (умова x=0 хибна)</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="kop">В/О</span> + адреса</td>
          <td><span class="ffn">F x≥0</span></td>
          <td><span class="vfd">59 XX</span></td>
          <td>Якщо <span class="reg">X</span> ≥ 0</td>
          <td>Якщо <span class="reg">X</span> &lt; 0 (умова x≥0 хибна)</td>
        </tr>
        <tr>
          <td><span class="kf">F</span> <span class="kop">С/П</span> + адреса</td>
          <td><span class="ffn">F x≠0</span></td>
          <td><span class="vfd">57 XX</span></td>
          <td>Якщо <span class="reg">X</span> ≠ 0</td>
          <td>Якщо <span class="reg">X</span> = 0 (умова x≠0 хибна)</td>
        </tr>
      </table>

      <h3>6.1. Організація циклів (F L0, F L1, F L2, F L3) та підпрограм (ПП / В/О)</h3>
      <ul>
        <li><b>Цикли</b>:
            <span class="kf">F</span> <span class="kop">П→Х</span> (<span class="ffn">L0</span>, код <span class="vfd">5D</span> — регістр <span class="reg">R0</span>),
            <span class="kf">F</span> <span class="kop">Х→П</span> (<span class="ffn">L1</span>, код <span class="vfd">5B</span> — регістр <span class="reg">R1</span>),
            <span class="kf">F</span> <span class="kop">БП</span> (<span class="ffn">L2</span>, код <span class="vfd">58</span> — регістр <span class="reg">R2</span>),
            <span class="kf">F</span> <span class="kop">ПП</span> (<span class="ffn">L3</span>, код <span class="vfd">5A</span> — регістр <span class="reg">R3</span>).
            При виконанні ціла частина відповідного регістра (<span class="reg">R0</span>..<span class="reg">R3</span>)
            <b>зменшується на 1</b>; якщо нове значення <b>&gt; 0</b>, відбувається перехід на вказану адресу початку циклу, інакше цикл завершується.</li>
        <li><b>Підпрограми</b>: команда <span class="kop">ПП</span> <code>XX</code> (код <span class="vfd">53 XX</span>)
            запам'ятовує адресу повернення у спеціальному стеку повернень (глибина — до <b>5 рівнів вкладеності</b>)
            та передає керування на адресу <code>XX</code>. Команда <span class="kop">В/О</span> (код <span class="vfd">52</span>)
            повертає керування на збережену адресу (а якщо стек повернень порожній — встановлює <code>PC = 00</code>).</li>
      </ul>

      <h3>6.2. Непряма адресація (префікс К) та автоінкремент/автодекремент</h3>
      <p>Якщо перед командами пам'яті або переходів натиснути синю клавішу <span class="kk">К</span>,
      а потім указати регістр-вказівник <code>n</code> (<span class="reg">0</span>..<span class="reg">E</span>),
      то номер цільового регістра або адреса переходу береться з <b>цілої частини вмісту регістра <code>Rn</code></b>
      (займає <b>1 крок</b> у пам'яті програм):</p>
      <ul>
        <li><b>Регістри <span class="reg">R0</span>, <span class="reg">R1</span>, <span class="reg">R2</span>, <span class="reg">R3</span> (Автодекремент)</b>:
            ціла частина регістра <b>спочатку зменшується на 1</b>, а вже потім нове значення використовується як адреса або номер регістра!</li>
        <li><b>Регістри <span class="reg">R4</span>, <span class="reg">R5</span>, <span class="reg">R6</span> (Автоінкремент)</b>:
            ціла частина регістра <b>спочатку збільшується на 1</b>, а потім використовується як адреса або номер регістра!
            <i>(Прийом: команда <code>КИП4</code> часто використовується програмістами просто для збільшення лічильника в <span class="reg">R4</span> на 1 за один крок програми).</i></li>
        <li><b>Регістри <span class="reg">R7</span>..<span class="reg">RE</span></b>: свого значення при непрямій адресації не змінюють.</li>
      </ul>

      <h2>7. Зведена таблиця всіх кодів команд МК-61 (00..EE)</h2>
      <table>
        <tr>
          <th>Діапазон кодів</th>
          <th>Мнемоніка / Клавіші</th>
          <th>Кроків</th>
          <th>Призначення</th>
        </tr>
        <tr>
          <td><span class="vfd">00</span> – <span class="vfd">09</span></td>
          <td><span class="knum">0</span> – <span class="knum">9</span></td>
          <td>1</td>
          <td>Введення цифр від 0 до 9</td>
        </tr>
        <tr>
          <td><span class="vfd">0A</span>.. <span class="vfd">0E</span></td>
          <td><span class="knum">.</span> (<span class="vfd">0-</span>), <span class="knum">/-/</span> (<span class="vfd">0L</span>), <span class="knum">ВП</span> (<span class="vfd">0C</span>), <span class="kcx">Сх</span> (<span class="vfd">0Г</span>), <span class="kop">В↑</span> (<span class="vfd">0E</span>)</td>
          <td>1</td>
          <td>Крапка, зміна знака, введення порядку, скидання X, підйом стека</td>
        </tr>
        <tr>
          <td><span class="vfd">0F</span></td>
          <td><span class="kf">F</span> <span class="ffn">Вх</span> (<span class="vfd">0 </span>)</td>
          <td>1</td>
          <td>Виклик попереднього значення з регістра <span class="reg">X1</span> у <span class="reg">X</span></td>
        </tr>
        <tr>
          <td><span class="vfd">10</span> – <span class="vfd">14</span></td>
          <td><span class="kop">+</span>, <span class="kop">-</span>, <span class="kop">×</span>, <span class="kop">÷</span>, <span class="kop">↔</span></td>
          <td>1</td>
          <td>Арифметичні операції над <span class="reg">Y</span> та <span class="reg">X</span> і обмін <span class="reg">X</span> ↔ <span class="reg">Y</span></td>
        </tr>
        <tr>
          <td><span class="vfd">15</span> – <span class="vfd">1E</span></td>
          <td><span class="ffn">10ˣ</span>, <span class="ffn">eˣ</span>, <span class="ffn">lg</span>, <span class="ffn">ln</span>, <span class="ffn">arcsin</span>, <span class="ffn">arccos</span>, <span class="ffn">arctg</span>, <span class="ffn">sin</span>, <span class="ffn">cos</span>, <span class="ffn">tg</span></td>
          <td>1</td>
          <td>Показникові, логарифмічні та тригонометричні функції (<span class="kf">F</span> + <span class="knum">0</span>..<span class="knum">9</span>)</td>
        </tr>
        <tr>
          <td><span class="vfd">20</span> – <span class="vfd">25</span></td>
          <td><span class="ffn">π</span>, <span class="ffn">√</span>, <span class="ffn">x²</span>, <span class="ffn">1/x</span>, <span class="ffn">xʸ</span>, <span class="ffn">В↻</span></td>
          <td>1</td>
          <td>Константа π, корінь, квадрат, 1/x, степінь Y<sup>X</sup>, кільцевий зсув стека</td>
        </tr>
        <tr>
          <td><span class="vfd">26</span>, <span class="vfd">2A</span>, <span class="vfd">30</span>, <span class="vfd">33</span></td>
          <td><span class="kfn">К М→Г</span>, <span class="kfn">К МС→Г</span>, <span class="kfn">К Г→МС</span>, <span class="kfn">К Г→М</span></td>
          <td>1</td>
          <td>Переведення кутових і часових величин (градуси, мінути, секунди)</td>
        </tr>
        <tr>
          <td><span class="vfd">31</span>, <span class="vfd">32</span>, <span class="vfd">34</span>–<span class="vfd">36</span></td>
          <td><span class="kfn">К |x|</span>, <span class="kfn">К ЗН</span>, <span class="kfn">К [x]</span>, <span class="kfn">К {x}</span>, <span class="kfn">К max</span></td>
          <td>1</td>
          <td>Модуль, знак, ціла частина, дробова частина, максимум із X і Y</td>
        </tr>
        <tr>
          <td><span class="vfd">37</span> – <span class="vfd">3B</span></td>
          <td><span class="kfn">К ∧</span>, <span class="kfn">К ∨</span>, <span class="kfn">К ⊕</span>, <span class="kfn">К инв</span>, <span class="kfn">К СЧ</span></td>
          <td>1</td>
          <td>Побітові логічні операції у форматі <span class="vfd">8.xxxxxxx</span> та випадкове число [0; 1)</td>
        </tr>
        <tr>
          <td><span class="vfd">40</span> – <span class="vfd">4E</span></td>
          <td><span class="kop">Х→П</span> <code>0..E</code> (<code>П0..ПE</code>)</td>
          <td>1</td>
          <td>Прямий запис числа з <span class="reg">X</span> у регістр пам'яті <span class="reg">R0</span>..<span class="reg">RE</span></td>
        </tr>
        <tr>
          <td><span class="vfd">50</span>, <span class="vfd">52</span>, <span class="vfd">54</span></td>
          <td><span class="kop">С/П</span>, <span class="kop">В/О</span>, <span class="kfn">К НОП</span></td>
          <td>1</td>
          <td>Зупинка/пуск програми, повернення з підпрограми, порожня операція</td>
        </tr>
        <tr>
          <td><span class="vfd">51</span>, <span class="vfd">53</span></td>
          <td><span class="kop">БП</span> <code>XX</code>, <span class="kop">ПП</span> <code>XX</code></td>
          <td>2</td>
          <td>Безумовний перехід на адресу <code>XX</code> та виклик підпрограми за адресою <code>XX</code></td>
        </tr>
        <tr>
          <td><span class="vfd">57</span>, <span class="vfd">59</span>, <span class="vfd">5C</span>, <span class="vfd">5E</span></td>
          <td><span class="ffn">F x≠0</span>, <span class="ffn">F x≥0</span>, <span class="ffn">F x&lt;0</span>, <span class="ffn">F x=0</span> + <code>XX</code></td>
          <td>2</td>
          <td>Умовні переходи (стрибок на <code>XX</code>, якщо умова НЕ виконується)</td>
        </tr>
        <tr>
          <td><span class="vfd">58</span>, <span class="vfd">5A</span>, <span class="vfd">5B</span>, <span class="vfd">5D</span></td>
          <td><span class="ffn">F L2</span>, <span class="ffn">F L3</span>, <span class="ffn">F L1</span>, <span class="ffn">F L0</span> + <code>XX</code></td>
          <td>2</td>
          <td>Цикли з декрементом регістрів <span class="reg">R2</span>, <span class="reg">R3</span>, <span class="reg">R1</span>, <span class="reg">R0</span> (стрибок на <code>XX</code>, доки R &gt; 0)</td>
        </tr>
        <tr>
          <td><span class="vfd">60</span> – <span class="vfd">6E</span></td>
          <td><span class="kop">П→Х</span> <code>0..E</code> (<code>ИП0..ИПE</code>)</td>
          <td>1</td>
          <td>Пряме читання числа з регістра пам'яті <span class="reg">R0</span>..<span class="reg">RE</span> у регістр <span class="reg">X</span></td>
        </tr>
        <tr>
          <td><span class="vfd">70</span>–<span class="vfd">7E</span>, <span class="vfd">90</span>–<span class="vfd">9E</span>, <span class="vfd">C0</span>–<span class="vfd">CE</span>, <span class="vfd">E0</span>–<span class="vfd">EE</span></td>
          <td><span class="kfn">К x≠0</span>, <span class="kfn">К x≥0</span>, <span class="kfn">К x&lt;0</span>, <span class="kfn">К x=0</span> <code>0..E</code></td>
          <td>1</td>
          <td>Непрямі умовні переходи за адресою в регістрі <span class="reg">R0</span>..<span class="reg">RE</span></td>
        </tr>
        <tr>
          <td><span class="vfd">80</span>–<span class="vfd">8E</span>, <span class="vfd">A0</span>–<span class="vfd">AE</span></td>
          <td><span class="kfn">К БП</span> <code>0..E</code>, <span class="kfn">К ПП</span> <code>0..E</code></td>
          <td>1</td>
          <td>Непрямий безумовний перехід та непрямий виклик підпрограми за регістром</td>
        </tr>
        <tr>
          <td><span class="vfd">B0</span>–<span class="vfd">BE</span>, <span class="vfd">D0</span>–<span class="vfd">DE</span></td>
          <td><span class="kfn">К Х→П</span> <code>0..E</code> (<code>КП</code>), <span class="kfn">К П→Х</span> <code>0..E</code> (<code>КИП</code>)</td>
          <td>1</td>
          <td>Непрямий запис у пам'ять та непряме читання з пам'яті за регістром</td>
        </tr>
      </table>

      <h2>8. Крок, автокрок і точки зупину</h2>
      <ul>
        <li>Один крок — кнопка <span class="kk">⏭ Крок (F10)</span> або <span class="kop">ШГ→</span>
            в режимі <span class="ffn">АВТ</span>.</li>
        <li>Автокрок — <span class="kop">▶ Автокрок (F5)</span>. Швидкість: 3, 10, 25 або 100 кроків/с.
            25 кроків/с близькі до живого МК-61.</li>
        <li>Точка зупину (<code>●</code>) — подвійний клік по кроку у вкладці пам'яті або
            <span class="kop">● Точка зупину (F9)</span> на поточному PC. І звичайний пуск, і автокрок
            на ній зупиняються.</li>
        <li>Лишити на екрані тільки корпус — <span class="kop">◀ Тільки МК-61</span> або
            <span class="knum">F12</span>.</li>
      </ul>
      <p>Програма поширюється під GPL-3.0. Qt і шрифт індикатора згадані у вікні «Про».</p>

      </body>
      </html>
    )"));
    return browser;
  }

  void showAbout() {
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Про програму"));
    box.setTextFormat(Qt::RichText);
    box.setText(
      QStringLiteral(
        "<p><b>Електроніка МК-61</b>, емулятор %1</p>"
        "<p>Стек, регістри, 105 кроків і клавіші як на «Електроніці МК-61». "
        "Програма під GNU GPL версії 3. Повний текст — у файлі LICENSE.</p>"
        "<p>Вікно зібрано на Qt %2 (The Qt Company). Qt іде під LGPL-3.0 або GPL-3.0, "
        "залежно від збірки, якою користувалися. Кнопка нижче відкриває їхнє вікно.</p>"
        "<p>Цифри на індикаторі малює шрифт MK61 VFD з цього ж проєкту, теж під GPL-3.0.</p>")
        .arg(QStringLiteral(MK61EMU_VERSION), QString::fromUtf8(qVersion())));
    auto *qt_btn = box.addButton(QStringLiteral("Про Qt…"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    box.exec();
    if (box.clickedButton() == qt_btn) {
      QMessageBox::aboutQt(this, QStringLiteral("Qt"));
    }
  }

  void triggerKey(Key key) {
    stopRunningTimer();
    calc_.press_key(key);
    refreshAll();
  }

  void toggleInspectorPanel() {
    if (inspector_panel_ == nullptr) {
      return;
    }
    const bool hide_inspector = inspector_panel_->isVisible();
    if (hide_inspector) {
      if (!isMaximized() && !isFullScreen()) {
        expanded_size_ = size();
      } else {
        showNormal();
      }
      inspector_panel_->setVisible(false);
      statusBar()->setVisible(false);
      toggle_panel_btn_->setText(QStringLiteral("▶ Панель"));
      setMinimumSize(392, 610);
      setMaximumSize(392, 610);
      resize(392, 610);
    } else {
      setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      setMinimumSize(820, 640);
      inspector_panel_->setVisible(true);
      statusBar()->setVisible(true);
      toggle_panel_btn_->setText(QStringLiteral("◀ Тільки МК-61"));
      resize(expanded_size_.width() >= 820 ? expanded_size_ : QSize(1020, 660));
    }
  }

  void onDebugStepClicked() {
    stopRunningTimer();
    calc_.set_operating_mode(OperatingMode::Automatic);
    const int prev_pc = calc_.pc();
    const std::uint8_t op = calc_.program_memory()[static_cast<std::size_t>(prev_pc)];
    const auto info = mk61::core::opcode_info(op);
    calc_.step_program();
    refreshAll();
    statusBar()->showMessage(
      QStringLiteral("Виконано крок %1: [%2] %3 -> новий PC=%4, X=%5")
        .arg(to_qstr(mk61::core::format_step_address(prev_pc)), to_qstr(info.display_code),
             to_qstr(info.mnemonic), to_qstr(mk61::core::format_step_address(calc_.pc())),
             to_qstr(Calculator::format_register_value(calc_.raw_x())).trimmed()));
  }

  void onDebugPlayPauseClicked() {
    if (run_timer_->isActive()) {
      stopRunningTimer();
      statusBar()->showMessage(QStringLiteral("Покрокове виконання призупинено."));
      refreshAll();
      return;
    }
    calc_.set_operating_mode(OperatingMode::Automatic);
    run_timer_->setInterval(dbg_speed_combo_->currentData().toInt());
    run_timer_->start();
    statusBar()->showMessage(
      QStringLiteral("Запущено покрокове виконання (F5 або С/П для паузи)..."));
    refreshAll();
  }

  void toggleBreakpointAtPc() {
    const int cur = calc_.pc();
    if (breakpoints_.contains(cur)) {
      breakpoints_.erase(cur);
      statusBar()->showMessage(QStringLiteral("Знято точку зупину на кроці %1.")
                                 .arg(to_qstr(mk61::core::format_step_address(cur))));
    } else {
      breakpoints_.insert(cur);
      statusBar()->showMessage(QStringLiteral("Встановлено точку зупину (●) на кроці %1.")
                                 .arg(to_qstr(mk61::core::format_step_address(cur))));
    }
    refreshAll();
  }

  void onRunStopButton() {
    if (calc_.operating_mode() == OperatingMode::Programming) {
      calc_.press_key(Key::RunStop);
      refreshAll();
      return;
    }
    if (calc_.prefix_state() != PrefixState::None) {
      calc_.press_key(Key::RunStop);
      refreshAll();
      return;
    }
    if (run_timer_->isActive()) {
      stopRunningTimer();
      statusBar()->showMessage(QStringLiteral("Виконання програми зупинено користувачем (С/П)."));
      refreshAll();
      return;
    }

    // Якщо є точки зупину, виконуємо з перевіркою точок зупину
    for (std::size_t s = 0; s < 100000; ++s) {
      if (s > 0 && breakpoints_.contains(calc_.pc())) {
        refreshAll();
        statusBar()->showMessage(QStringLiteral("Зупинено на точці зупину (●) на кроці %1.")
                                   .arg(to_qstr(mk61::core::format_step_address(calc_.pc()))));
        return;
      }
      const std::uint8_t op = calc_.program_memory()[static_cast<std::size_t>(calc_.pc())];
      const bool keep_going = calc_.step_program();
      if (!keep_going) {
        refreshAll();
        if (calc_.is_error()) {
          statusBar()->showMessage(QStringLiteral("Аварійна зупинка програми: ЕГГОГ."));
        } else if (op == 0x50U) {
          statusBar()->showMessage(QStringLiteral("Програму виконано до команди С/П."));
        }
        return;
      }
    }
    refreshAll();
    statusBar()->showMessage(
      QStringLiteral("Увага: зупинено через досягнення ліміту 100 000 кроків."));
  }

  void onRunTimerTick() {
    const std::uint8_t op = calc_.program_memory()[static_cast<std::size_t>(calc_.pc())];
    const bool keep_going = calc_.step_program();
    refreshAll();
    if (!keep_going) {
      stopRunningTimer();
      if (calc_.is_error()) {
        statusBar()->showMessage(QStringLiteral("Аварійна зупинка програми: ЕГГОГ."));
      } else if (op == 0x50U) {
        statusBar()->showMessage(QStringLiteral("Програма зупинилася на команді С/П."));
      }
      return;
    }
    if (breakpoints_.contains(calc_.pc())) {
      stopRunningTimer();
      refreshAll();
      statusBar()->showMessage(QStringLiteral("Досягнуто точку зупину (●) на кроці %1.")
                                 .arg(to_qstr(mk61::core::format_step_address(calc_.pc()))));
    }
  }

  void stopRunningTimer() {
    if (run_timer_->isActive()) {
      run_timer_->stop();
    }
  }

  void onRegisterItemChanged(QTableWidgetItem *item) {
    if (updating_ui_ || item == nullptr) {
      return;
    }
    const QVariant reg_var = item->data(Qt::UserRole);
    if (!reg_var.isValid()) {
      return;
    }
    const int reg_idx = reg_var.toInt();
    bool ok = false;
    const double val = item->text().trimmed().toDouble(&ok);
    if (ok) {
      calc_.set_memory_reg(static_cast<std::size_t>(reg_idx), val);
    }
    refreshAll();
  }

  void onLoadPresetClicked() {
    const QString id = preset_combo_->currentData().toString();
    if (id.isEmpty()) {
      return;
    }
    const auto *preset = mk61::core::find_builtin_program(id.toStdString());
    if (preset == nullptr) {
      return;
    }
    stopRunningTimer();
    breakpoints_.clear();
    asm_editor_->setPlainText(to_qstr(preset->source_code));
    preset_hint_label_->setText(
      QStringLiteral("<b>%1:</b> %2").arg(to_qstr(preset->title), to_qstr(preset->usage_hint)));
    calc_.assemble_and_load(preset->source_code);

    if (preset->id == "moon_lander") {
      calc_.set_memory_reg(1, 1000.0);
      calc_.set_memory_reg(2, -50.0);
      calc_.set_memory_reg(3, 250.0);
    } else if (preset->id == "indirect_sum") {
      calc_.set_memory_reg(5, 10.0);
      calc_.set_memory_reg(6, 20.0);
      calc_.set_memory_reg(7, 30.0);
      calc_.set_memory_reg(8, 40.0);
      calc_.set_memory_reg(9, 50.0);
    } else if (preset->id == "quadratic") {
      calc_.set_memory_reg(1, 1.0);
      calc_.set_memory_reg(2, -5.0);
      calc_.set_memory_reg(3, 6.0);
    }

    refreshAll();
    statusBar()->showMessage(QStringLiteral("Завантажено приклад «%1». %2")
                               .arg(to_qstr(preset->title), to_qstr(preset->usage_hint)));
  }

  void onAssembleClicked() {
    stopRunningTimer();
    const std::string src = asm_editor_->toPlainText().toStdString();
    const auto res = calc_.assemble_and_load(src);
    if (!res.ok) {
      QMessageBox::warning(this, QStringLiteral("Помилка асемблювання"),
                           to_qstr(res.error_message));
      return;
    }
    refreshAll();
    statusBar()->showMessage(
      QStringLiteral("Програму успішно записано в пам'ять МК-61 (%1 кроків). Натисніть В/О та С/П.")
        .arg(res.steps_used));
  }

  void onDisassembleToEditorClicked() {
    const auto steps = mk61::core::disassemble_program(calc_.program_memory(), false);
    std::ostringstream oss;
    oss << "# Дизасембльована програма з пам'яті МК-61\n";
    for (const auto &s : steps) {
      oss << s.step_str << ": " << s.mnemonic << "    # " << s.description << "\n";
    }
    asm_editor_->setPlainText(to_qstr(oss.str()));
  }

  void onDisassembleGridToEditorClicked() {
    const std::string grid = mk61::core::format_program_compact_grid(calc_.program_memory());
    asm_editor_->setPlainText(to_qstr(grid));
  }

  void onOpenFileClicked() {
    const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Відкрити програму або стан МК-61"), QString(),
      QStringLiteral("Файли МК-61 (*.mk61 *.txt *.asm);;Усі файли (*)"));
    if (path.isEmpty()) {
      return;
    }
    std::ifstream ifs(path.toStdString());
    if (!ifs) {
      QMessageBox::critical(this, QStringLiteral("Помилка"),
                            QStringLiteral("Не вдалося відкрити вибраний файл."));
      return;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    const std::string content = oss.str();

    if (content.find("PROGRAM=") != std::string::npos) {
      std::string err;
      if (!calc_.deserialize_state(content, err)) {
        QMessageBox::warning(this, QStringLiteral("Помилка"), to_qstr(err));
        return;
      }
      onDisassembleToEditorClicked();
    } else {
      asm_editor_->setPlainText(to_qstr(content));
      const auto res = calc_.assemble_and_load(content);
      if (!res.ok) {
        QMessageBox::warning(this, QStringLiteral("Помилка асемблювання"),
                             to_qstr(res.error_message));
        return;
      }
    }
    refreshAll();
    statusBar()->showMessage(QStringLiteral("Завантажено файл: %1").arg(path));
  }

  void onSaveFileClicked() {
    const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Зберегти стан МК-61"), QStringLiteral("program.mk61"),
      QStringLiteral("Стан МК-61 (*.mk61);;Текстовий лістинг (*.txt)"));
    if (path.isEmpty()) {
      return;
    }
    std::ofstream ofs(path.toStdString());
    if (!ofs) {
      QMessageBox::critical(this, QStringLiteral("Помилка"),
                            QStringLiteral("Не вдалося зберегти файл."));
      return;
    }
    if (path.endsWith(QStringLiteral(".txt"))) {
      ofs << asm_editor_->toPlainText().toStdString();
    } else {
      ofs << calc_.serialize_state();
    }
    statusBar()->showMessage(QStringLiteral("Збережено у файл: %1").arg(path));
  }

  void refreshAll() {
    updating_ui_ = true;

    // 1. Індикатор ВЛІ
    const auto disp = calc_.display();
    vfd_screen_->setDisplayState(disp);

    const QString mode_str = (calc_.operating_mode() == OperatingMode::Automatic)
                               ? QStringLiteral("АВТ")
                               : QStringLiteral("ПРГ");
    const QString pc_str = to_qstr(mk61::core::format_step_address(calc_.pc()));
    QString prefix_str = QStringLiteral("—");
    switch (calc_.prefix_state()) {
    case PrefixState::None:
      break;
    case PrefixState::F:
      prefix_str = QStringLiteral("F");
      break;
    case PrefixState::K:
      prefix_str = QStringLiteral("К");
      break;
    case PrefixState::WaitAddr1:
    case PrefixState::WaitAddr2:
      prefix_str = QStringLiteral("Адреса...");
      break;
    default:
      prefix_str = QStringLiteral("Регістр...");
      break;
    }
    status_badge_label_->setText(
      QStringLiteral("Режим: %1 | PC: %2 | Мод: %3%4")
        .arg(mode_str, pc_str, prefix_str,
             run_timer_->isActive() ? QStringLiteral(" | [РАХУЄ]") : QStringLiteral("")));

    // Панель відлагодження
    dbg_play_btn_->setText(run_timer_->isActive() ? QStringLiteral("⏸ Пауза (F5)")
                                                  : QStringLiteral("▶ Автокрок (F5)"));
    const std::uint8_t cur_op = calc_.program_memory()[static_cast<std::size_t>(calc_.pc())];
    const auto cur_info = mk61::core::opcode_info(cur_op);
    dbg_current_step_label_->setText(
      QStringLiteral("PC=%1: [%2] %3")
        .arg(pc_str, to_qstr(cur_info.display_code), to_qstr(cur_info.mnemonic)));

    // 2. Перемикачі
    rad_btn_->setChecked(calc_.angle_mode() == AngleMode::Radians);
    deg_btn_->setChecked(calc_.angle_mode() == AngleMode::Degrees);
    grad_btn_->setChecked(calc_.angle_mode() == AngleMode::Grads);
    avt_btn_->setChecked(calc_.operating_mode() == OperatingMode::Automatic);
    prg_btn_->setChecked(calc_.operating_mode() == OperatingMode::Programming);

    // 3. Операційний стек
    stack_labels_[0]->setText(to_qstr(Calculator::format_register_value(calc_.raw_t())).trimmed());
    stack_labels_[1]->setText(to_qstr(Calculator::format_register_value(calc_.raw_z())).trimmed());
    stack_labels_[2]->setText(to_qstr(Calculator::format_register_value(calc_.raw_y())).trimmed());
    stack_labels_[3]->setText(to_qstr(Calculator::format_register_value(calc_.raw_x())).trimmed());
    stack_labels_[4]->setText(to_qstr(Calculator::format_register_value(calc_.raw_x1())).trimmed());

    QString ret_str = QStringLiteral("Стек ПП (SP=%1): ").arg(calc_.return_sp());
    if (calc_.return_sp() == 0) {
      ret_str += QStringLiteral("—");
    } else {
      for (std::size_t i = 0; i < calc_.return_sp(); ++i) {
        ret_str +=
          to_qstr(mk61::core::format_step_address(calc_.return_stack()[i])) + QStringLiteral(" ");
      }
    }
    return_stack_label_->setText(ret_str);

    // 4. Регістри пам'яті R0..RE
    for (int row = 0; row < 5; ++row) {
      for (int col_group = 0; col_group < 3; ++col_group) {
        const auto reg_idx = static_cast<std::size_t>(row + col_group * 5);
        if (auto *item = regs_table_->item(row, col_group * 2 + 1)) {
          item->setText(
            to_qstr(Calculator::format_register_value(calc_.raw_memory_reg(reg_idx))).trimmed());
        }
      }
    }

    // 5. Таблиця пам'яті програм
    QFont vfd_code_font = vfd_font_;
    vfd_code_font.setPixelSize(16);

    const auto steps = mk61::core::disassemble_program(calc_.program_memory(), true);
    prog_table_->setRowCount(static_cast<int>(steps.size()));
    int active_row = 0;
    for (std::size_t i = 0; i < steps.size(); ++i) {
      const auto &s = steps[i];
      const bool is_cur =
        (s.step == calc_.pc()) || (s.arg_byte.has_value() && s.step + 1 == calc_.pc());
      const bool has_bp = breakpoints_.contains(s.step);
      if (is_cur) {
        active_row = static_cast<int>(i);
      }
      const QString prefix_mark = has_bp ? (is_cur ? QStringLiteral("●►") : QStringLiteral("● "))
                                         : (is_cur ? QStringLiteral(" ►") : QStringLiteral("  "));
      const QString step_txt = prefix_mark + to_qstr(s.step_str);
      const QString code_txt =
        s.arg_byte.has_value()
          ? QStringLiteral("%1 %2").arg(
              to_qstr(s.opcode_str),
              QStringLiteral("%1").arg(*s.arg_byte, 2, 16, QLatin1Char('0')).toUpper())
          : to_qstr(s.opcode_str);

      auto *c0 = new QTableWidgetItem(step_txt);
      c0->setData(Qt::UserRole, s.step);
      auto *c1 = new QTableWidgetItem(code_txt);
      c1->setFont(vfd_code_font);
      c1->setForeground(QBrush(QColor(0x4a, 0xff, 0x9e)));
      auto *c2 = new QTableWidgetItem(to_qstr(s.mnemonic));
      auto *c3 = new QTableWidgetItem(to_qstr(s.description));

      if (has_bp && !is_cur) {
        const QBrush bp_bg(QColor(0x3e, 0x16, 0x16));
        const QBrush bp_fg(QColor(0xff, 0x8a, 0x80));
        c0->setBackground(bp_bg);
        c0->setForeground(bp_fg);
      }

      if (is_cur) {
        const QBrush hi_bg(QColor(0x14, 0x3b, 0x27));
        const QBrush hi_fg(QColor(0x69, 0xf0, 0xae));
        c0->setBackground(hi_bg);
        c1->setBackground(hi_bg);
        c2->setBackground(hi_bg);
        c3->setBackground(hi_bg);
        c0->setForeground(has_bp ? QBrush(QColor(0xff, 0x52, 0x52)) : hi_fg);
        c2->setForeground(hi_fg);
        c3->setForeground(hi_fg);
      }

      const auto r = static_cast<int>(i);
      prog_table_->setItem(r, 0, c0);
      prog_table_->setItem(r, 1, c1);
      prog_table_->setItem(r, 2, c2);
      prog_table_->setItem(r, 3, c3);
    }
    prog_table_->selectRow(active_row);

    updating_ui_ = false;
  }
};

} // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("mk61emu"));
  QApplication::setApplicationDisplayName(QStringLiteral("Електроніка МК-61"));
  QApplication::setApplicationVersion(QStringLiteral(MK61EMU_VERSION));
  QGuiApplication::setDesktopFileName(QStringLiteral("mk61emu"));

  QFont vfd_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  const int font_id =
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/MK61VFD-Regular.ttf"));
  if (font_id != -1) {
    const QStringList families = QFontDatabase::applicationFontFamilies(font_id);
    if (!families.isEmpty()) {
      vfd_font = QFont(families.first());
    }
  }

  Mk61MainWindow window(vfd_font);
  window.show();
  return QApplication::exec();
}
