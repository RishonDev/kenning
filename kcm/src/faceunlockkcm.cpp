/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "faceunlockkcm.h"
#include "camerapreview.h"
#include "faceunlockpaths.h"

#include <KAuth/Action>
#include <KAuth/ExecuteJob>
#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KSharedConfig>
#include <KUser>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QUrl>

K_PLUGIN_CLASS_WITH_JSON(FaceUnlockKcm, "kcm_faceunlock.json")

namespace
{
const QString helperId = QStringLiteral("org.kde.kcontrol.kcmfaceunlock");

// Values from config.ini keyed "section/key". Multi-line values are not needed here.
QVariantMap readHowdyConfig()
{
    QVariantMap values;
    QFile file(FaceUnlock::howdyConfigPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return values;
    }
    QString section;
    for (const QString &raw : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
        } else if (const int equals = line.indexOf(QLatin1Char('=')); equals > 0 && !line.startsWith(QLatin1Char('#'))) {
            values.insert(section + QLatin1Char('/') + line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }
    return values;
}

bool pamUsesHowdy(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    return std::any_of(lines.cbegin(), lines.cend(), FaceUnlock::isPamHowdyLine);
}

KConfigGroup lockScreenGroup(const KSharedConfig::Ptr &config)
{
    return config->group(QStringLiteral("Greeter")).group(QStringLiteral("LnF")).group(QStringLiteral("General"));
}

QVariant typed(const QVariant &defaultValue, const QString &text)
{
    if (defaultValue.typeId() == QMetaType::Bool) {
        return text == QLatin1String("true");
    }
    if (defaultValue.typeId() == QMetaType::Int) {
        return text.toInt();
    }
    if (defaultValue.typeId() == QMetaType::Double) {
        return text.toDouble();
    }
    return text;
}

QString asText(const QVariant &value)
{
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return value.toString();
}
}

FaceUnlockKcm::FaceUnlockKcm(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
{
    qmlRegisterType<CameraPreview>("org.kde.faceunlock", 1, 0, "CameraPreview");
    setButtons(Apply | Default);
}

QVariantMap FaceUnlockKcm::defaultSettings()
{
    return {
        {QStringLiteral("core/disabled"), false},
        {QStringLiteral("core/detection_notice"), true},
        {QStringLiteral("core/no_confirmation"), false},
        {QStringLiteral("core/abort_if_ssh"), true},
        {QStringLiteral("core/abort_if_lid_closed"), true},
        {QStringLiteral("video/certainty"), 3.5},
        {QStringLiteral("video/timeout"), 4},
        {QStringLiteral("video/dark_threshold"), 60},
        {QStringLiteral("snapshots/save_failed"), false},
        {QStringLiteral("pam/login"), true},
        {QStringLiteral("pam/lockscreen"), true},
        {QStringLiteral("pam/sudo"), true},
        {QStringLiteral("pam/polkit"), false},
        {QStringLiteral("Appearance/AnimationStyle"), QStringLiteral("hello")},
        {QStringLiteral("LoginScreen/AutoScan"), true},
        {QStringLiteral("LoginScreen/WelcomePause"), 1000},
        {QStringLiteral("lock/faceUnlockShowButton"), false},
        {QStringLiteral("lock/unlockDelay"), 500},
    };
}

void FaceUnlockKcm::load()
{
    const QVariantMap defaults = defaultSettings();
    QVariantMap settings = defaults;

    const QVariantMap howdy = readHowdyConfig();
    for (auto it = howdy.cbegin(); it != howdy.cend(); ++it) {
        if (defaults.contains(it.key())) {
            settings.insert(it.key(), typed(defaults.value(it.key()), it.value().toString()));
        }
    }
    m_devicePath = howdy.value(QStringLiteral("video/device_path")).toString();

    for (const auto &service : FaceUnlock::pamServices()) {
        settings.insert(QStringLiteral("pam/") + service.id, pamUsesHowdy(service.path));
    }

    KConfig system(FaceUnlock::systemConfigPath, KConfig::SimpleConfig);
    for (const QString &key : {QStringLiteral("Appearance/AnimationStyle"), QStringLiteral("LoginScreen/AutoScan"), QStringLiteral("LoginScreen/WelcomePause")}) {
        const auto setting = FaceUnlock::systemSetting(key);
        const QString text = system.group(setting->group).readEntry(setting->key, asText(defaults.value(key)));
        settings.insert(key, typed(defaults.value(key), text));
    }

    const KConfigGroup lock = lockScreenGroup(KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc")));
    settings.insert(QStringLiteral("lock/faceUnlockShowButton"), lock.readEntry("faceUnlockShowButton", false));
    settings.insert(QStringLiteral("lock/unlockDelay"), lock.readEntry("unlockDelay", 500));

    m_settings = settings;
    m_loaded = settings;
    Q_EMIT settingsChanged();
    updateState();
    refreshFaces();
    refreshThemes();
}

void FaceUnlockKcm::save()
{
    QVariantMap howdy;
    QVariantMap pam;
    QVariantMap system;
    for (auto it = m_settings.cbegin(); it != m_settings.cend(); ++it) {
        if (m_loaded.value(it.key()) == it.value()) {
            continue;
        }
        const QString key = it.key();
        if (key.startsWith(QLatin1String("pam/"))) {
            pam.insert(key.mid(4), it.value());
        } else if (FaceUnlock::systemSetting(key)) {
            system.insert(key, asText(it.value()));
        } else if (FaceUnlock::howdySetting(key)) {
            howdy.insert(key, asText(it.value()));
        }
    }

    // Per-user lock screen options; the style is mirrored so the lock screen can read it.
    auto config = KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc"));
    KConfigGroup lock = lockScreenGroup(config);
    lock.writeEntry("faceUnlockShowButton", m_settings.value(QStringLiteral("lock/faceUnlockShowButton")).toBool());
    lock.writeEntry("unlockDelay", m_settings.value(QStringLiteral("lock/unlockDelay")).toInt());
    lock.writeEntry("faceAnimation", m_settings.value(QStringLiteral("Appearance/AnimationStyle")).toString());
    config->sync();

    if (howdy.isEmpty() && pam.isEmpty() && system.isEmpty()) {
        m_loaded = m_settings;
        updateState();
        return;
    }

    KAuth::Action action(helperId + QStringLiteral(".save"));
    action.setHelperId(helperId);
    action.setArguments({{QStringLiteral("howdy"), howdy}, {QStringLiteral("pam"), pam}, {QStringLiteral("system"), system}});
    auto *job = action.execute();
    connect(job, &KJob::result, this, [this, job] {
        if (job->error()) {
            Q_EMIT errorOccurred(job->errorString());
            setNeedsSave(true);
        } else {
            load();
        }
    });
    job->start();
}

void FaceUnlockKcm::defaults()
{
    const QVariantMap defaults = defaultSettings();
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
        m_settings.insert(it.key(), it.value());
    }
    Q_EMIT settingsChanged();
    updateState();
}

void FaceUnlockKcm::updateState()
{
    setNeedsSave(m_settings != m_loaded);
    setRepresentsDefaults(m_settings == defaultSettings());
}

QVariantMap FaceUnlockKcm::settings() const
{
    return m_settings;
}

void FaceUnlockKcm::setValue(const QString &key, const QVariant &value)
{
    if (!m_settings.contains(key) || m_settings.value(key) == value) {
        return;
    }
    // Keep the type the defaults use, so QML numbers compare equal to loaded ones.
    QVariant converted = value;
    converted.convert(defaultSettings().value(key).metaType());
    m_settings.insert(key, converted);
    Q_EMIT settingsChanged();
    updateState();
}

QVariantList FaceUnlockKcm::faces() const
{
    return m_faces;
}

bool FaceUnlockKcm::busy() const
{
    return m_busy;
}

void FaceUnlockKcm::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

QString FaceUnlockKcm::userName() const
{
    return KUser().loginName();
}

QString FaceUnlockKcm::devicePath() const
{
    return m_devicePath;
}

bool FaceUnlockKcm::howdyInstalled() const
{
    return QFile::exists(QStringLiteral("/usr/lib/security/pam_howdy.so")) && QFile::exists(FaceUnlock::howdyConfigPath);
}

bool FaceUnlockKcm::lockScreenPatched() const
{
    QFile file(FaceUnlock::lockScreenPath);
    return file.open(QIODevice::ReadOnly) && file.readAll().contains(FaceUnlock::lockScreenMarker.toUtf8());
}

void FaceUnlockKcm::refreshFaces()
{
    KAuth::Action action(helperId + QStringLiteral(".list"));
    action.setHelperId(helperId);
    auto *job = action.execute();
    connect(job, &KJob::result, this, [this, job] {
        if (job->error()) {
            return;
        }
        m_faces.clear();
        const auto models = QJsonDocument::fromJson(job->data().value(QStringLiteral("models")).toString().toUtf8()).array();
        for (const auto &value : models) {
            const QJsonObject model = value.toObject();
            m_faces.append(QVariantMap{
                {QStringLiteral("id"), model.value(QStringLiteral("id")).toInt()},
                {QStringLiteral("label"), model.value(QStringLiteral("label")).toString()},
                {QStringLiteral("added"), QDateTime::fromSecsSinceEpoch(model.value(QStringLiteral("time")).toInteger())},
            });
        }
        Q_EMIT facesChanged();
    });
    job->start();
}

QVariantList FaceUnlockKcm::themes() const
{
    return m_themes;
}

void FaceUnlockKcm::refreshThemes()
{
    m_themes.clear();
    QStringList seen;
    for (const QString &base : {FaceUnlock::userThemesPath, FaceUnlock::systemThemesPath}) {
        const QDir dir(base);
        for (const QString &id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QFile metadataFile(dir.filePath(id + QStringLiteral("/metadata.json")));
            if (seen.contains(id) || !FaceUnlock::isThemeId(id) || !metadataFile.open(QIODevice::ReadOnly)
                || !QFile::exists(dir.filePath(id + QStringLiteral("/contents/ui/main.qml")))) {
                continue;
            }
            const QJsonObject plugin = QJsonDocument::fromJson(metadataFile.readAll()).object().value(QStringLiteral("KPlugin")).toObject();
            const QJsonArray authors = plugin.value(QStringLiteral("Authors")).toArray();
            seen.append(id);
            m_themes.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), plugin.value(QStringLiteral("Name")).toString(id)},
                {QStringLiteral("description"), plugin.value(QStringLiteral("Description")).toString()},
                {QStringLiteral("author"), authors.isEmpty() ? QString() : authors.first().toObject().value(QStringLiteral("Name")).toString()},
                {QStringLiteral("removable"), base == FaceUnlock::userThemesPath},
            });
        }
    }
    Q_EMIT themesChanged();
}

void FaceUnlockKcm::installTheme(const QUrl &archive)
{
    // Read as the user; the helper only ever sees the bytes.
    QFile file(archive.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT errorOccurred(i18n("Could not read %1", archive.toLocalFile()));
        return;
    }
    KAuth::Action action(helperId + QStringLiteral(".installtheme"));
    action.setHelperId(helperId);
    action.setArguments({{QStringLiteral("archive"), file.readAll()}});
    auto *job = action.execute();
    connect(job, &KJob::result, this, [this, job] {
        if (job->error()) {
            Q_EMIT errorOccurred(job->errorString());
            return;
        }
        refreshThemes();
        setValue(QStringLiteral("Appearance/AnimationStyle"), job->data().value(QStringLiteral("id")).toString());
    });
    job->start();
}

void FaceUnlockKcm::removeTheme(const QString &id)
{
    KAuth::Action action(helperId + QStringLiteral(".removetheme"));
    action.setHelperId(helperId);
    action.setArguments({{QStringLiteral("id"), id}});
    auto *job = action.execute();
    connect(job, &KJob::result, this, [this, job, id] {
        if (job->error()) {
            Q_EMIT errorOccurred(job->errorString());
            return;
        }
        refreshThemes();
        if (m_settings.value(QStringLiteral("Appearance/AnimationStyle")).toString() == id) {
            setValue(QStringLiteral("Appearance/AnimationStyle"), QStringLiteral("hello"));
        }
    });
    job->start();
}

void FaceUnlockKcm::addFace(const QString &label)
{
    runHelper(QStringLiteral("add"), {{QStringLiteral("label"), label}});
}

void FaceUnlockKcm::removeFace(int id)
{
    runHelper(QStringLiteral("remove"), {{QStringLiteral("id"), id}});
}

void FaceUnlockKcm::testRecognition()
{
    runHelper(QStringLiteral("test"), {});
}

void FaceUnlockKcm::runHelper(const QString &operation, const QVariantMap &args)
{
    if (m_busy) {
        return;
    }
    setBusy(true);

    KAuth::Action action(helperId + QLatin1Char('.') + operation);
    action.setHelperId(helperId);
    action.setArguments(args);
    // Capturing a face can take a while; don't let KAuth give up on the helper.
    action.setTimeout(120000);
    auto *job = action.execute();
    connect(job, &KAuth::ExecuteJob::newData, this, [this](const QVariantMap &data) {
        Q_EMIT progress(data.value(QStringLiteral("line")).toString());
    });
    connect(job, &KJob::result, this, [this, job, operation] {
        setBusy(false);
        if (job->error()) {
            Q_EMIT operationFinished(operation, -1, job->errorString());
            return;
        }
        const int exitCode = job->data().value(QStringLiteral("exitCode")).toInt();
        const QStringList lines = job->data().value(QStringLiteral("output")).toString().trimmed().split(QLatin1Char('\n'));
        Q_EMIT operationFinished(operation, exitCode, lines.isEmpty() ? QString() : lines.last());
        if (operation != QLatin1String("test")) {
            refreshFaces();
        }
    });
    job->start();
}

#include "faceunlockkcm.moc"
