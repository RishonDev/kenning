/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "service.h"

#include <QTest>

using namespace FaceUnlock;

class ServiceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesRequests()
    {
        const std::optional<Request> request = parseRequest("compare alice\n");
        QVERIFY(request);
        QCOMPARE(request->command, std::string("compare"));
        QCOMPARE(request->user, std::string("alice"));
    }

    void rejectsMalformedRequests_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("no newline") << QStringLiteral("compare alice");
        QTest::newRow("no user") << QStringLiteral("compare\n");
        QTest::newRow("empty user") << QStringLiteral("compare \n");
        QTest::newRow("empty command") << QStringLiteral(" alice\n");
        QTest::newRow("space after the newline") << QStringLiteral("compare\n alice");
    }

    void rejectsMalformedRequests()
    {
        QFETCH(QString, text);
        QVERIFY(!parseRequest(text.toStdString()));
    }
};

QTEST_GUILESS_MAIN(ServiceTest)
#include "servicetest.moc"
