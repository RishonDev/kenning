/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "signedin.h"

#include <QTest>

using namespace FaceUnlock;

class SignedInTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void remembersUsers()
    {
        SignedInUsers users;
        QVERIFY(!users.contains(1000));
        QVERIFY(users.add(1000));
        QVERIFY(!users.add(1000));
        QVERIFY(users.contains(1000));
        QVERIFY(!users.contains(1001));
    }

    void survivesARestartOfTheService()
    {
        SignedInUsers before;
        before.add(1000);
        before.add(1002);
        SignedInUsers after;
        after.restore(before.save());
        QVERIFY(after.contains(1000));
        QVERIFY(after.contains(1002));
        QVERIFY(!after.contains(1001));
    }

    void ignoresAGarbledFile()
    {
        SignedInUsers users;
        users.restore("1000\nnot a number\n1001\n");
        QVERIFY(users.contains(1000));
        QVERIFY(!users.contains(1001));
    }
};

QTEST_GUILESS_MAIN(SignedInTest)
#include "signedintest.moc"
