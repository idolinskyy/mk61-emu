#include <core/greeter.hpp>

#include <QApplication>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QWidget>

int main(int argc, char *argv[]) {
  QApplication application(argc, argv);

  QWidget window;
  window.setWindowTitle(QStringLiteral("mk61emu"));
  window.resize(420, 120);

  auto *layout = new QFormLayout(&window);
  auto *nameInput = new QLineEdit(QStringLiteral("C++23"), &window);
  auto *greeting = new QLabel(&window);
  greeting->setTextFormat(Qt::PlainText);
  greeting->setWordWrap(true);

  layout->addRow(QStringLiteral("Name"), nameInput);
  layout->addRow(greeting);

  const auto updateGreeting = [greeting](const QString &name) {
    mk61::core::Greeter greeter(name.toStdString());
    greeting->setText(QString::fromStdString(greeter.greet()));
  };
  QObject::connect(nameInput, &QLineEdit::textChanged, &window, updateGreeting);
  updateGreeting(nameInput->text());

  window.show();
  return application.exec();
}