/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "config.h"

#include <QTemporaryDir>
#include <QTest>

#include <fstream>

using FaceUnlock::Config;

class ConfigTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    std::string write(const char *contents)
    {
        const std::string path = m_directory.filePath(QStringLiteral("config.ini")).toStdString();
        std::ofstream(path) << contents;
        return path;
    }

private Q_SLOTS:
    void readsSectionsAndKeys()
    {
        const Config config(write("[core]\ndisabled = true\n\n[video]\n  timeout=7  \n"));
        QCOMPARE(config.string("core/disabled"), std::string("true"));
        QCOMPARE(config.string("video/timeout"), std::string("7"));
    }

    void skipsComments()
    {
        const Config config(write("[core]\n# disabled = true\n; detection_notice = false\n"));
        QVERIFY(config.values().empty());
    }

    void fallsBackForMissingKeys()
    {
        const Config config(write("[core]\n"));
        QCOMPARE(config.string("core/missing", "fallback"), std::string("fallback"));
        QCOMPARE(config.number("video/timeout", 4), 4.0);
        QCOMPARE(config.boolean("core/disabled", true), true);
    }

    void parsesNumbers()
    {
        const Config config(write("[video]\ncertainty = 6.5\ntimeout = 4 seconds\nrotate = none\n"));
        QCOMPARE(config.number("video/certainty", 0), 6.5);
        QCOMPARE(config.number("video/timeout", 0), 4.0);
        QCOMPARE(config.number("video/rotate", 1), 1.0);
    }

    void parsesBooleans()
    {
        const Config config(write("[core]\non = true\noff = false\nother = yes\n"));
        QCOMPARE(config.boolean("core/on", false), true);
        QCOMPARE(config.boolean("core/off", true), false);
        QCOMPARE(config.boolean("core/other", true), false);
    }

    void missingFileIsEmpty()
    {
        const Config config(m_directory.filePath(QStringLiteral("absent.ini")).toStdString());
        QVERIFY(config.values().empty());
    }
};

QTEST_GUILESS_MAIN(ConfigTest)
#include "configtest.moc"
