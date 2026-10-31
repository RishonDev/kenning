/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "engine.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace FaceUnlock;

class FaceModelsTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    std::string path() const
    {
        return m_directory.filePath(QStringLiteral("user.dat")).toStdString();
    }

private Q_SLOTS:
    void roundTrips()
    {
        cv::Mat feature(1, 128, CV_32F);
        for (int i = 0; i < feature.cols; ++i) {
            feature.at<float>(i) = float(i) / 128;
        }
        const std::vector<FaceModel> written = {
            {.id = 0, .label = "Glasses", .time = 1790000000, .encodings = {feature, feature * 2}},
            {.id = 3, .label = "No glasses", .time = 1790000100, .encodings = {}},
        };
        QVERIFY(writeModels(path(), written));

        const std::vector<FaceModel> read = readModels(path());
        QCOMPARE(read.size(), written.size());
        QCOMPARE(read[0].label, std::string("Glasses"));
        QCOMPARE(read[0].time, 1790000000LL);
        QCOMPARE(read[0].encodings.size(), std::size_t(2));
        QCOMPARE(cv::norm(read[0].encodings[1], feature * 2), 0.0);
        QCOMPARE(read[1].id, 3);
        QVERIFY(read[1].encodings.empty());
    }

    void onlyTheOwnerCanRead()
    {
        QVERIFY(writeModels(path(), {}));
        // Qt repeats the owner's bits as "User" bits for the current user.
        const auto ownerGroupOther =
            QFile::permissions(QString::fromStdString(path())) & ~(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
        QCOMPARE(ownerGroupOther, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    void missingFileHasNoModels()
    {
        QVERIFY(readModels(m_directory.filePath(QStringLiteral("absent.dat")).toStdString()).empty());
    }

    void scoresLikeHowdy()
    {
        QCOMPARE(matchScore(1.0), 0.0);
        QCOMPARE(matchScore(0.363), 6.37);
    }
};

QTEST_GUILESS_MAIN(FaceModelsTest)
#include "facemodelstest.moc"
