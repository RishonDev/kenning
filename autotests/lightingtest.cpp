/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pipeline.h"

#include <QTest>

using namespace FaceUnlock;

namespace
{
constexpr double darkThreshold = 60;

// A 100 pixel frame: the given shares (%) black and white, the rest mid grey.
cv::Mat frame(int blackPercent, int whitePercent)
{
    cv::Mat grey(10, 10, CV_8U, cv::Scalar(128));
    for (int i = 0; i < blackPercent; ++i) {
        grey.at<uchar>(i) = 0;
    }
    for (int i = 0; i < whitePercent; ++i) {
        grey.at<uchar>(99 - i) = 255;
    }
    return grey;
}
}

class LightingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void judgesLighting_data()
    {
        QTest::addColumn<int>("blackPercent");
        QTest::addColumn<int>("whitePercent");
        QTest::addColumn<int>("lighting");

        QTest::newRow("all black") << 100 << 0 << int(Lighting::Black);
        QTest::newRow("mostly dark") << 70 << 0 << int(Lighting::Dark);
        QTest::newRow("at the dark threshold") << 60 << 0 << int(Lighting::Usable);
        QTest::newRow("washed out") << 0 << 60 << int(Lighting::WashedOut);
        QTest::newRow("washed out beats dark") << 45 << 55 << int(Lighting::WashedOut);
        QTest::newRow("half bright") << 0 << 50 << int(Lighting::Usable);
        QTest::newRow("mid grey") << 0 << 0 << int(Lighting::Usable);
    }

    void judgesLighting()
    {
        QFETCH(int, blackPercent);
        QFETCH(int, whitePercent);
        QFETCH(int, lighting);
        QCOMPARE(int(judgeLighting(frame(blackPercent, whitePercent), darkThreshold)), lighting);
    }
};

QTEST_GUILESS_MAIN(LightingTest)
#include "lightingtest.moc"
