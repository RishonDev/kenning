/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "faceunlockkcm.h"
#include "camerafinder.h"
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
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QUrl>

K_PLUGIN_CLASS_WITH_JSON(FaceUnlockKcm, "kcm_kenning.json")

namespace
{
const QString helperId = QStringLiteral("org.kde.kcontrol.kcmkenning");

// Values from config.ini keyed "section/key". Multi-line values are not needed here.
QVariantMap readEngineConfig()
{
    QVariantMap values;
    QFile file(FaceUnlock::engineConfigPath);
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

bool pamUsesEngine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    return std::any_of(lines.cbegin(), lines.cend(), FaceUnlock::isPamLine);
}

KConfigGroup lockScreenGroup(const KSharedConfig::Ptr &config)
{
    return config->group(QStringLiteral("Greeter")).group(QStringLiteral("LnF")).group(QStringLiteral("General"));
}

// Whether fprintd has a fingerprint of this user on any reader. Without fprintd the
// system bus answers at once that the service is unknown.
bool hasFingerprint(const QString &user)
{
    const QString service = QStringLiteral("net.reactivated.Fprint");
    QDBusMessage devices = QDBusConnection::systemBus().call(
        QDBusMessage::createMethodCall(service, QStringLiteral("/net/reactivated/Fprint/Manager"), service + QStringLiteral(".Manager"), QStringLiteral("GetDevices")),
        QDBus::Block,
        3000);
    if (devices.type() != QDBusMessage::ReplyMessage || devices.arguments().isEmpty()) {
        return false;
    }
    const auto paths = qdbus_cast<QList<QDBusObjectPath>>(devices.arguments().constFirst());
    return std::any_of(paths.cbegin(), paths.cend(), [&](const QDBusObjectPath &path) {
        QDBusMessage call = QDBusMessage::createMethodCall(service, path.path(), service + QStringLiteral(".Device"), QStringLiteral("ListEnrolledFingers"));
        call << user;
        // No fingerprints is an error reply (NoEnrolledPrints), not an empty list.
        const QDBusMessage fingers = QDBusConnection::systemBus().call(call, QDBus::Block, 3000);
        return fingers.type() == QDBusMessage::ReplyMessage && !fingers.arguments().isEmpty() && !fingers.arguments().constFirst().toStringList().isEmpty();
    });
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
    qmlRegisterType<CameraPreview>("org.kde.kenning", 1, 0, "CameraPreview");
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
        {QStringLiteral("video/certainty"), 6.0},
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
        {QStringLiteral("lock/preferredMethod"), QStringLiteral("face")},
        {QStringLiteral("lock/useLastMethod"), false},
    };
}

void FaceUnlockKcm::load()
{
    const QVariantMap defaults = defaultSettings();
    QVariantMap settings = defaults;

    const QVariantMap engine = readEngineConfig();
    for (auto it = engine.cbegin(); it != engine.cend(); ++it) {
        if (defaults.contains(it.key())) {
            settings.insert(it.key(), typed(defaults.value(it.key()), it.value().toString()));
        }
    }
    const QString configured = engine.value(QStringLiteral("video/device_path")).toString();
    if (configured.isEmpty() || configured == QLatin1String("none") || !QFile::exists(configured)) {
        const FaceUnlock::Camera camera = FaceUnlock::findCamera();
        m_devicePath = camera.path;
        m_colourCamera = camera.colour;
    } else {
        m_devicePath = configured;
        m_colourCamera = engine.value(QStringLiteral("video/colour")).toString() == QLatin1String("true");
    }

    for (const auto &service : FaceUnlock::pamServices()) {
        settings.insert(QStringLiteral("pam/") + service.id, pamUsesEngine(service.path));
    }
    const FaceUnlock::LockScreenState lockScreen = FaceUnlock::lockScreenState();
    m_lockScreenFaceService = lockScreen.faceService;
    if (lockScreen.known) {
        settings.insert(QStringLiteral("pam/lockscreen"), lockScreen.enabled);
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
    // Saved empty while no fingerprint is enrolled; face is what the page offers first.
    const QString preferred = lock.readEntry("preferredMethod", QString());
    settings.insert(QStringLiteral("lock/preferredMethod"), preferred == QLatin1String("fingerprint") ? preferred : QStringLiteral("face"));
    settings.insert(QStringLiteral("lock/useLastMethod"), lock.readEntry("useLastMethod", false));
    m_fingerprintEnrolled = hasFingerprint(userName());

    m_settings = settings;
    m_loaded = settings;
    Q_EMIT settingsChanged();
    updateState();
    refreshFaces();
    refreshThemes();
}

void FaceUnlockKcm::save()
{
    QVariantMap engine;
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
        } else if (FaceUnlock::engineSetting(key)) {
            engine.insert(key, asText(it.value()));
        }
    }

    // Per-user lock screen options; the style is mirrored so the lock screen can read it.
    auto config = KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc"));
    KConfigGroup lock = lockScreenGroup(config);
    lock.writeEntry("faceUnlockShowButton", m_settings.value(QStringLiteral("lock/faceUnlockShowButton")).toBool());
    lock.writeEntry("unlockDelay", m_settings.value(QStringLiteral("lock/unlockDelay")).toInt());
    lock.writeEntry("faceAnimation", m_settings.value(QStringLiteral("Appearance/AnimationStyle")).toString());
    // The lock screen only applies a preferred method while there is a fingerprint to choose;
    // otherwise it starts with the method used last, as Plasma does.
    const QString preferred = m_settings.value(QStringLiteral("lock/preferredMethod")).toString();
    const bool useLast = m_settings.value(QStringLiteral("lock/useLastMethod")).toBool();
    lock.writeEntry("preferredMethod", m_fingerprintEnrolled ? preferred : QString());
    lock.writeEntry("useLastMethod", useLast);
    if (m_fingerprintEnrolled && !useLast && preferred == QLatin1String("fingerprint")) {
        // Plasma only offers fingerprint as a method of its own when this is on.
        config->group(QStringLiteral("Authenticators")).writeEntry("Fingerprint", true);
    }
    if (m_lockScreenFaceService) {
        // Plasma's Face method is switched on per user.
        const bool face = m_settings.value(QStringLiteral("pam/lockscreen")).toBool();
        config->group(QStringLiteral("Authenticators")).writeEntry("Face", face);
        if (!face) {
            // A remembered method that is switched off crashes the lock screen
            // (https://bugs.kde.org/528042), so forget it too.
            auto state = KSharedConfig::openStateConfig(QStringLiteral("kscreenlockerrc"));
            KConfigGroup greeter = state->group(QStringLiteral("Greeter"));
            if (greeter.readEntry("Authenticator") == QLatin1String("Face")) {
                greeter.deleteEntry("Authenticator");
                state->sync();
            }
        }
    }
    config->sync();

    if (engine.isEmpty() && pam.isEmpty() && system.isEmpty()) {
        m_loaded = m_settings;
        updateState();
        return;
    }

    KAuth::Action action(helperId + QStringLiteral(".save"));
    action.setHelperId(helperId);
    action.setArguments({{QStringLiteral("engine"), engine}, {QStringLiteral("pam"), pam}, {QStringLiteral("system"), system}});
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

bool FaceUnlockKcm::colourCamera() const
{
    return m_colourCamera;
}

bool FaceUnlockKcm::engineInstalled() const
{
    return QFile::exists(QStringLiteral("/usr/lib/security/pam_kenning.so")) && QFile::exists(FaceUnlock::engineBinary);
}

bool FaceUnlockKcm::lockScreenPatched() const
{
    QFile file(FaceUnlock::lockScreenPath);
    return file.open(QIODevice::ReadOnly) && file.readAll().contains(FaceUnlock::lockScreenMarker.toUtf8());
}

bool FaceUnlockKcm::loginScreenSupported() const
{
    return QFile::exists(FaceUnlock::loginManagerSupportPath);
}

bool FaceUnlockKcm::fingerprintEnrolled() const
{
    return m_fingerprintEnrolled;
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
    // Testing your own face needs no administrator: the engine asks the Kenning
    // service, which answers for the user's own account.
    if (m_busy) {
        return;
    }
    setBusy(true);
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        setBusy(false);
        const QStringList lines = QString::fromUtf8(process->readAll()).trimmed().split(QLatin1Char('\n'));
        Q_EMIT operationFinished(QStringLiteral("test"), status == QProcess::NormalExit ? exitCode : -1, lines.value(0));
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setBusy(false);
            Q_EMIT operationFinished(QStringLiteral("test"), -1, process->errorString());
            process->deleteLater();
        }
    });
    process->start(FaceUnlock::engineBinary, {QStringLiteral("test")});
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
