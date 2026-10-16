/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IniFileTests.h"

#include "common/IniFile.h"

#include <QFile>
#include <QSettings>

using deskflow::IniFile;
using deskflow::SettingValue;

namespace {

std::filesystem::path toPath(const QString &file)
{
  return std::filesystem::path(file.toStdU16String());
}

// Values that need QSettings' escaping, quoting and special prefixes
const QList<QPair<QString, QVariant>> &sampleValues()
{
  static const QList<QPair<QString, QVariant>> values = {
      {QStringLiteral("plain/text"), QStringLiteral("hello")},
      {QStringLiteral("plain/empty"), QString()},
      {QStringLiteral("plain/spaces"), QStringLiteral("  padded both ends  ")},
      {QStringLiteral("plain/comma"), QStringLiteral("a, b; c = d")},
      {QStringLiteral("plain/quotes"), QStringLiteral("say \"hi\"")},
      {QStringLiteral("plain/backslash"), QStringLiteral("C:\\Users\\me\\deskflow.conf")},
      {QStringLiteral("plain/controls"), QStringLiteral("tab\there\nnew line\x01"
                                                        "1")},
      {QStringLiteral("plain/unicode"), QStringLiteral("Ñandú – 日本")},
      {QStringLiteral("plain/at"), QStringLiteral("@not a type")},
      {QStringLiteral("plain/number"), 24800},
      {QStringLiteral("plain/negative"), -3},
      {QStringLiteral("plain/real"), 1.5},
      {QStringLiteral("plain/yes"), true},
      {QStringLiteral("plain/no"), false},
      {QStringLiteral("list/two"), QStringList{QStringLiteral("one"), QStringLiteral("two, too")}},
      {QStringLiteral("list/one"), QStringList{QStringLiteral("only")}},
      {QStringLiteral("list/empty"), QStringList{}},
      {QStringLiteral("list/at"), QStringList{QStringLiteral("@a"), QStringLiteral("b")}},
      {QStringLiteral("rootKey"), QStringLiteral("outside any group")},
      {QStringLiteral("General/key"), QStringLiteral("group called General")},
      {QStringLiteral("computer_my pc/aliases"), QStringList{QStringLiteral("x"), QStringLiteral("y")}},
      {QStringLiteral("internalConfig/screens/1/name"), QStringLiteral("array item")},
  };
  return values;
}

std::vector<std::string> toStd(const QStringList &list)
{
  std::vector<std::string> result;
  for (const auto &item : list)
    result.push_back(item.toStdString());
  return result;
}

bool matches(const SettingValue &actual, const QVariant &expected)
{
  if (expected.metaType().id() == QMetaType::QStringList)
    return actual.toStringList() == toStd(expected.toStringList());
  return QString::fromStdString(actual.toString()) == expected.toString();
}

} // namespace

void IniFileTests::initTestCase()
{
  QVERIFY(m_dir.isValid());
}

void IniFileTests::valueConversions()
{
  QVERIFY(!SettingValue::fromIni("false").toBool());
  QVERIFY(!SettingValue::fromIni("0").toBool());
  QVERIFY(!SettingValue::fromIni("").toBool());
  QVERIFY(SettingValue::fromIni("true").toBool());
  QVERIFY(SettingValue::fromIni("yes").toBool());
  QCOMPARE(SettingValue::fromIni(" 42 ").toInt(), 42);
  QCOMPARE(SettingValue::fromIni("4x").toInt(), 0);
  QCOMPARE(SettingValue::fromIni("3").toUInt(), 3u);
  QCOMPARE(SettingValue::fromIni("1.25").toDouble(), 1.25);
  QVERIFY(!SettingValue::fromIni("@Invalid()").isValid());
  QCOMPARE(SettingValue::fromIni("@@x").toString(), "@x");
  QVERIFY(SettingValue::fromIni("a, b").toStringList() == (std::vector<std::string>{"a", "b"}));
  QCOMPARE(SettingValue::fromIni("\"a, b\"").toString(), "a, b");

  QCOMPARE(SettingValue(true).toIni(), "true");
  QCOMPARE(SettingValue(250).toIni(), "250");
  QCOMPARE(SettingValue(1.0).toIni(), "1");
  QCOMPARE(SettingValue(std::vector<std::string>{}).toIni(), "@Invalid()");
  QCOMPARE(SettingValue().toIni(), "@Invalid()");
}

void IniFileTests::readsWhatQSettingsWrites()
{
  const auto file = m_dir.filePath(QStringLiteral("written-by-qt.conf"));
  {
    QSettings settings(file, QSettings::IniFormat);
    for (const auto &[key, value] : sampleValues())
      settings.setValue(key, value);
  }

  const auto ini = IniFile::load(toPath(file));
  QSettings settings(file, QSettings::IniFormat);
  for (const auto &key : settings.allKeys()) {
    QVERIFY2(ini.contains(key.toStdString()), qPrintable(key));
    QVERIFY2(matches(ini.value(key.toStdString()), settings.value(key)), qPrintable(key));
  }
  QCOMPARE(ini.keys().size(), static_cast<size_t>(settings.allKeys().size()));
  QVERIFY(!ini.value("list/empty").isValid());
  QCOMPARE(ini.value("plain/number").toInt(), 24800);
  QVERIFY(ini.value("plain/yes").toBool());
  QVERIFY(!ini.value("plain/no").toBool());

  // keys match case the same way QSettings matches them on this system
  QCOMPARE(ini.contains("PLAIN/Text"), settings.contains(QStringLiteral("PLAIN/Text")));
}

void IniFileTests::qsettingsReadsWhatIniFileWrites()
{
  const auto file = m_dir.filePath(QStringLiteral("written-by-us.conf"));
  {
    // start from a file Qt wrote, so both new and existing sections get keys
    QSettings settings(file, QSettings::IniFormat);
    settings.setValue(QStringLiteral("plain/existing"), QStringLiteral("before"));
    settings.setValue(QStringLiteral("other/untouched"), QStringLiteral("kept"));
  }

  auto ini = IniFile::load(toPath(file));
  for (const auto &[key, value] : sampleValues()) {
    SettingValue converted;
    switch (value.metaType().id()) {
    case QMetaType::Bool:
      converted = value.toBool();
      break;
    case QMetaType::Int:
      converted = value.toInt();
      break;
    case QMetaType::Double:
      converted = value.toDouble();
      break;
    case QMetaType::QStringList:
      converted = toStd(value.toStringList());
      break;
    default:
      converted = value.toString().toStdString();
    }
    ini.setValue(key.toStdString(), converted);
  }
  ini.setValue("plain/existing", "after");
  QVERIFY(ini.save(toPath(file)));

  QSettings settings(file, QSettings::IniFormat);
  QCOMPARE(settings.status(), QSettings::NoError);
  for (const auto &[key, value] : sampleValues()) {
    if (value.metaType().id() == QMetaType::QStringList)
      QCOMPARE(settings.value(key).toStringList(), value.toStringList());
    else
      QCOMPARE(settings.value(key).toString(), value.toString());
  }
  QCOMPARE(settings.value(QStringLiteral("plain/existing")).toString(), QStringLiteral("after"));
  QCOMPARE(settings.value(QStringLiteral("other/untouched")).toString(), QStringLiteral("kept"));
}

void IniFileTests::editKeepsTheRestOfTheFile()
{
  const std::string text = "[core]\r\ncomputerName=alexis-server\r\ncoreMode=1\r\n\r\n"
                           "[internalConfig]\r\nscreens\\1\\name=\r\nscreens\\size=1\r\n\r\n"
                           "[client]\r\nremoteHost=192.168.1.60\r\n";
  IniFile ini(text);
  QCOMPARE(ini.text(), text);
  QCOMPARE(ini.value("internalConfig/screens/size").toInt(), 1);

  ini.setValue("core/coreMode", 2);
  ini.setValue("core/port", 24800);
  ini.setValue("daemon/logLevel", "DEBUG");
  QCOMPARE(
      ini.text(), "[core]\r\ncomputerName=alexis-server\r\ncoreMode=2\r\nport=24800\r\n\r\n"
                  "[internalConfig]\r\nscreens\\1\\name=\r\nscreens\\size=1\r\n\r\n"
                  "[client]\r\nremoteHost=192.168.1.60\r\n\r\n[daemon]\r\nlogLevel=DEBUG\r\n"
  );

  ini.setValue("rootKey", "x");
  QVERIFY(ini.text().starts_with("[General]\r\nrootKey=x\r\n\r\n[core]\r\n"));
}

void IniFileTests::removeTakesChildKeys()
{
  IniFile ini("[a]\nx=1\ny=2\n\n[b]\nz=3\n\n[c]\nw=4\n");
  ini.remove("b");
  QCOMPARE(ini.text(), "[a]\nx=1\ny=2\n\n[c]\nw=4\n");
  ini.remove("a/x");
  QCOMPARE(ini.text(), "[a]\ny=2\n\n[c]\nw=4\n");
  ini.remove("a/y");
  QCOMPARE(ini.text(), "[c]\nw=4\n");
  QVERIFY(!ini.contains("a/y"));
}

void IniFileTests::keysAndGroups()
{
  IniFile ini("[computer_a]\nname=a\n[core]\nport=1\n[computer_b]\nname=b\naliases=c, d\n");
  QVERIFY(ini.childGroups() == (std::vector<std::string>{"computer_a", "core", "computer_b"}));
  QCOMPARE(ini.keys().size(), size_t(4));
  QVERIFY(ini.value("computer_b/aliases").toStringList() == (std::vector<std::string>{"c", "d"}));
}

QTEST_MAIN(IniFileTests)
