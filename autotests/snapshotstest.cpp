/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "snapshots.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace FaceUnlock;

namespace
{
// Qt repeats the owner's bits as "User" bits for the current user.
QFileDevice::Permissions ownerGroupOther(const QFileInfo &info)
{
    return info.permissions() & ~(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
}
}

class SnapshotsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void savesAPrivateJpeg()
    {
        QTemporaryDir directory;
        const QString userDirectory = directory.filePath(QStringLiteral("user"));
        QVERIFY(saveSnapshot(userDirectory.toStdString(), cv::Mat(48, 64, CV_8UC3, cv::Scalar(40, 80, 120))));

        const QFileInfoList photos = QDir(userDirectory).entryInfoList({QStringLiteral("*.jpg")}, QDir::Files);
        QCOMPARE(photos.size(), 1);
        QCOMPARE(ownerGroupOther(photos.first()), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(ownerGroupOther(QFileInfo(userDirectory)), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    void keepsTheNewest()
    {
        QTemporaryDir directory;
        for (const char *name : {"1790000001.jpg", "1790000003.jpg", "1790000002.jpg", "1790000004.jpg", "notes.txt"}) {
            QFile file(directory.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        pruneSnapshots(directory.path().toStdString(), 2);

        const QStringList left = QDir(directory.path()).entryList(QDir::Files, QDir::Name);
        QCOMPARE(left, QStringList({QStringLiteral("1790000003.jpg"), QStringLiteral("1790000004.jpg"), QStringLiteral("notes.txt")}));
    }
};

QTEST_GUILESS_MAIN(SnapshotsTest)
#include "snapshotstest.moc"
