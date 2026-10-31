/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "facemodels.h"
#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <pwd.h>
#include <unistd.h>

namespace FaceUnlock
{
namespace
{
const QString idKey = QStringLiteral("id");
const QString labelKey = QStringLiteral("label");
const QString timeKey = QStringLiteral("time");
const QString encodingsKey = QStringLiteral("data");

cv::Mat featureFromJson(const QJsonArray &numbers)
{
    cv::Mat feature(1, int(numbers.size()), CV_32F);
    for (int i = 0; i < numbers.size(); ++i) {
        feature.at<float>(i) = float(numbers.at(i).toDouble());
    }
    return feature;
}

QJsonArray featureToJson(const cv::Mat &feature)
{
    QJsonArray numbers;
    for (int i = 0; i < feature.cols; ++i) {
        numbers.append(double(feature.at<float>(i)));
    }
    return numbers;
}
}

std::vector<FaceModel> readModels(const std::string &path)
{
    std::vector<FaceModel> models;
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return models;
    }
    for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject object = value.toObject();
        FaceModel model{
            .id = object.value(idKey).toInt(),
            .label = object.value(labelKey).toString().toStdString(),
            .time = object.value(timeKey).toInteger(),
            .encodings = {},
        };
        for (const auto &encoding : object.value(encodingsKey).toArray()) {
            model.encodings.push_back(featureFromJson(encoding.toArray()));
        }
        models.push_back(model);
    }
    return models;
}

bool writeModels(const std::string &path, const std::vector<FaceModel> &models)
{
    QJsonArray array;
    for (const FaceModel &model : models) {
        QJsonArray encodings;
        for (const cv::Mat &feature : model.encodings) {
            encodings.append(featureToJson(feature));
        }
        array.append(QJsonObject{
            {idKey, model.id},
            {labelKey, QString::fromStdString(model.label)},
            {timeKey, model.time},
            {encodingsKey, encodings},
        });
    }
    const QString filePath = QString::fromStdString(path);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    // Set on the path: after commit() the QSaveFile no longer refers to the saved file.
    return file.commit() && QFile::setPermissions(filePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

std::vector<FaceModel> loadModels(const std::string &user)
{
    return readModels(modelPath(user));
}

bool saveModels(const std::string &user, const std::vector<FaceModel> &models)
{
    const QString directory = QString::fromUtf8(modelsDir);
    if (QDir().mkpath(directory)) {
        QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    const std::string path = modelPath(user);
    if (!writeModels(path, models)) {
        return false;
    }
    giveToService(path);
    return true;
}

void giveToService(const std::string &path)
{
    // Only root can hand files over; a test build run as a user keeps its own.
    if (geteuid() != 0) {
        return;
    }
    if (const passwd *account = getpwnam(serviceUser)) {
        chown(path.c_str(), account->pw_uid, account->pw_gid);
        chown(modelsDir, account->pw_uid, account->pw_gid);
    }
}
}
