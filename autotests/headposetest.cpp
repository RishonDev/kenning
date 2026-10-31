/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "headpose.h"
#include "pipeline.h"

#include <QTest>

using namespace FaceUnlock;

namespace
{
// A YuNet row with eyes 40 px apart at y 100 and the mouth at y 160.
cv::Mat face(float noseX, float noseY)
{
    cv::Mat row = cv::Mat::zeros(1, 15, CV_32F);
    row.at<float>(YuNet::rightEyeX) = 80;
    row.at<float>(YuNet::rightEyeY) = 100;
    row.at<float>(YuNet::leftEyeX) = 120;
    row.at<float>(YuNet::leftEyeY) = 100;
    row.at<float>(YuNet::noseX) = noseX;
    row.at<float>(YuNet::noseY) = noseY;
    row.at<float>(YuNet::rightMouthY) = 160;
    row.at<float>(YuNet::leftMouthY) = 160;
    return row;
}

constexpr TurnLimits limits{.minimum = 0.12, .maximum = 0.45, .maxDrift = 0.06};
}

class HeadPoseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void measuresYawAndPitch()
    {
        const HeadPose pose = headPose(face(110, 130));
        QCOMPARE(pose.yaw, 0.25); // 10 px right of the eye midpoint, eyes 40 px apart
        QCOMPARE(pose.pitch, 0.5); // halfway between eyes and mouth
    }

    void looksStraightWithinTolerance()
    {
        const HeadPose straight{.yaw = 0, .pitch = 0.5};
        QVERIFY(isLookingStraight({.yaw = 0.05, .pitch = 0.53}, straight));
        QVERIFY(!isLookingStraight({.yaw = 0.1, .pitch = 0.5}, straight));
        QVERIFY(!isLookingStraight({.yaw = 0, .pitch = 0.6}, straight));
    }

    void judgesTurns_data()
    {
        QTest::addColumn<double>("turn");
        QTest::addColumn<double>("drift");
        QTest::addColumn<bool>("mustOppose");
        QTest::addColumn<double>("previousTurn");
        QTest::addColumn<int>("verdict");

        QTest::newRow("accepted") << 0.2 << 0.0 << false << 0.0 << int(TurnVerdict::Accepted);
        QTest::newRow("too small") << 0.1 << 0.0 << false << 0.0 << int(TurnVerdict::TooSmall);
        QTest::newRow("too far") << 0.5 << 0.0 << false << 0.0 << int(TurnVerdict::TooFar);
        QTest::newRow("drifting") << 0.2 << 0.1 << false << 0.0 << int(TurnVerdict::Drifting);
        QTest::newRow("drift before halfway is ignored") << 0.05 << 0.1 << false << 0.0 << int(TurnVerdict::TooSmall);
        QTest::newRow("same way again") << 0.2 << 0.0 << true << 0.3 << int(TurnVerdict::WrongWay);
        QTest::newRow("other way") << -0.2 << 0.0 << true << 0.3 << int(TurnVerdict::Accepted);
        QTest::newRow("first turn was skipped") << 0.2 << 0.0 << true << 0.0 << int(TurnVerdict::Accepted);
    }

    void judgesTurns()
    {
        QFETCH(double, turn);
        QFETCH(double, drift);
        QFETCH(bool, mustOppose);
        QFETCH(double, previousTurn);
        QFETCH(int, verdict);
        QCOMPARE(int(judgeTurn(turn, drift, limits, mustOppose, previousTurn)), verdict);
    }
};

QTEST_GUILESS_MAIN(HeadPoseTest)
#include "headposetest.moc"
