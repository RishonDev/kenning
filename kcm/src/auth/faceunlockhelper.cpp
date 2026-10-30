/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "faceunlockhelper.h"
#include "faceunlockpaths.h"

#include <KAuth/HelperSupport>
#include <KConfig>
#include <KConfigGroup>
#include <KUser>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
QString callerName()
{
    return KUser(KUserId(HelperSupport::callerUid())).loginName();
}

ActionReply failure(const QString &message)
{
    ActionReply reply = ActionReply::HelperErrorReply();
    reply.setErrorDescription(message);
    return reply;
}

// Runs a command, forwarding each output line to the settings page as it arrives.
ActionReply runStreaming(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted()) {
        return failure(QStringLiteral("Could not start %1").arg(program));
    }

    QString output;
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200);
        while (process.canReadLine()) {
            const QString line = QString::fromUtf8(process.readLine()).trimmed();
            output += line + QLatin1Char('\n');
            if (!line.isEmpty()) {
                HelperSupport::progressStep(QVariantMap{{QStringLiteral("line"), line}});
            }
        }
        if (HelperSupport::isStopped()) {
            process.kill();
        }
    }
    output += QString::fromUtf8(process.readAll());

    ActionReply reply = ActionReply::SuccessReply();
    reply.addData(QStringLiteral("exitCode"), process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1);
    reply.addData(QStringLiteral("output"), output);
    return reply;
}

bool writeFile(const QString &path, const QByteArray &contents, QFileDevice::Permissions permissions)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(contents);
    return file.commit() && QFile::setPermissions(path, permissions);
}

constexpr auto standardPermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther;

// Replaces "key = value" lines inside [section] of config.ini, keeping comments and layout.
bool updateEngineConfig(const QVariantMap &values)
{
    QFile file(FaceUnlock::engineConfigPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    QString section;
    for (QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2);
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0 || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QString key = section + QLatin1Char('/') + line.left(equals).trimmed();
        if (values.contains(key)) {
            line = line.left(equals).trimmed() + QStringLiteral(" = ") + values.value(key).toString();
        }
    }
    return writeFile(FaceUnlock::engineConfigPath, lines.join(QLatin1Char('\n')).toUtf8(), standardPermissions);
}

// Adds or removes the pam_faceunlock.so line (and drops any pam_howdy.so line). A service without an /etc override is
// first copied from its packaged file in /usr/lib/pam.d.
bool setPamFace(const FaceUnlock::PamService &service, bool enabled)
{
    QString source = service.path;
    if (!QFile::exists(source)) {
        if (!enabled) {
            return true;
        }
        source = QStringLiteral("/usr/lib/pam.d/") + QFileInfo(service.path).fileName();
    }
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        return !enabled;
    }
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    const bool present = std::any_of(lines.cbegin(), lines.cend(), FaceUnlock::isPamLine);
    const bool leftover = std::any_of(lines.cbegin(), lines.cend(), [](const QString &line) {
        return FaceUnlock::isFacePamLine(line) && !FaceUnlock::isPamLine(line);
    });
    if (present == enabled && !leftover && source == service.path) {
        return true;
    }

    lines.erase(std::remove_if(lines.begin(), lines.end(), FaceUnlock::isFacePamLine), lines.end());
    if (enabled) {
        // Face first, everything else stays as the fallback.
        const auto firstAuth = std::find_if(lines.begin(), lines.end(), [](const QString &line) {
            return line.trimmed().startsWith(QLatin1String("auth"));
        });
        lines.insert(firstAuth, FaceUnlock::pamLine);
    }
    return writeFile(service.path, lines.join(QLatin1Char('\n')).toUtf8(), standardPermissions);
}
}

namespace
{
// The IR sensor is the capture device that only offers greyscale frames.
// by-path names survive reboots and replugging, unlike /dev/videoN.
QString findIrCamera()
{
    const QDir byPath(QStringLiteral("/dev/v4l/by-path"));
    for (const QFileInfo &entry : byPath.entryInfoList({QStringLiteral("*-video-index0")}, QDir::System)) {
        const int fd = open(entry.absoluteFilePath().toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        bool grey = false;
        bool colour = false;
        v4l2_fmtdesc format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        while (ioctl(fd, VIDIOC_ENUM_FMT, &format) == 0) {
            grey |= format.pixelformat == V4L2_PIX_FMT_GREY;
            colour |= format.pixelformat == V4L2_PIX_FMT_MJPEG || format.pixelformat == V4L2_PIX_FMT_YUYV;
            ++format.index;
        }
        close(fd);
        if (grey && !colour) {
            return entry.absoluteFilePath();
        }
    }
    return {};
}

// First-run setup so face unlock works without manual configuration: point the engine
// at the IR camera if it has none, and let the lock screen (which authenticates as
// the user) read the engine's config and face models.
void prepareEngine()
{
    QFile file(FaceUnlock::engineConfigPath);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression devicePath(QStringLiteral("^device_path\\s*=\\s*(.*)$"), QRegularExpression::MultilineOption);
        const QString configured = devicePath.match(QString::fromUtf8(file.readAll())).captured(1).trimmed();
        file.close();
        if (configured.isEmpty() || configured == QLatin1String("none") || !QFile::exists(configured)) {
            if (const QString camera = findIrCamera(); !camera.isEmpty()) {
                updateEngineConfig({{QStringLiteral("video/device_path"), camera}});
            }
        }
    }

    QDir().mkpath(QFileInfo(FaceUnlock::modelPath(QStringLiteral("x"))).absolutePath());
    QProcess::execute(QStringLiteral("/usr/bin/chmod"), {QStringLiteral("-R"), QStringLiteral("a+rX"), QStringLiteral("/etc/faceunlock")});
}
}

ActionReply FaceUnlockHelper::list(const QVariantMap &)
{
    QFile file(FaceUnlock::modelPath(callerName()));
    ActionReply reply = ActionReply::SuccessReply();
    reply.addData(QStringLiteral("models"), file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QStringLiteral("[]"));
    return reply;
}

ActionReply FaceUnlockHelper::add(const QVariantMap &args)
{
    // faceunlock drops commas itself; keep labels short as Howdy does.
    QString label = args.value(QStringLiteral("label")).toString().remove(QLatin1Char(',')).simplified().left(24);
    if (label.isEmpty()) {
        label = QStringLiteral("Face");
    }
    prepareEngine();
    ActionReply reply = runStreaming(FaceUnlock::engineBinary, {QStringLiteral("--plain"), QStringLiteral("-y"), QStringLiteral("-U"), callerName(), QStringLiteral("add"), label});
    // The new model file is created root-only; keep it readable for the lock screen.
    QProcess::execute(QStringLiteral("/usr/bin/chmod"), {QStringLiteral("-R"), QStringLiteral("a+rX"), QStringLiteral("/etc/faceunlock")});
    return reply;
}

ActionReply FaceUnlockHelper::remove(const QVariantMap &args)
{
    bool ok = false;
    const int id = args.value(QStringLiteral("id")).toInt(&ok);
    if (!ok || id < 0) {
        return failure(QStringLiteral("Invalid face model id"));
    }
    return runStreaming(FaceUnlock::engineBinary,
                        {QStringLiteral("--plain"), QStringLiteral("-y"), QStringLiteral("-U"), callerName(), QStringLiteral("remove"), QString::number(id)});
}

ActionReply FaceUnlockHelper::test(const QVariantMap &)
{
    // Same check pam_faceunlock runs: exit 0 match, 10 no model, 11 timeout, 12 abort, 13 too dark.
    return runStreaming(FaceUnlock::engineBinary, {QStringLiteral("compare"), callerName()});
}

ActionReply FaceUnlockHelper::save(const QVariantMap &args)
{
    const QVariantMap engine = args.value(QStringLiteral("engine")).toMap();
    QVariantMap accepted;
    for (auto it = engine.cbegin(); it != engine.cend(); ++it) {
        const auto setting = FaceUnlock::engineSetting(it.key());
        if (!setting || !setting->accepts(it.value().toString())) {
            return failure(QStringLiteral("Rejected engine setting %1").arg(it.key()));
        }
        accepted.insert(it.key(), it.value());
    }
    if (!accepted.isEmpty() && !updateEngineConfig(accepted)) {
        return failure(QStringLiteral("Could not write %1").arg(FaceUnlock::engineConfigPath));
    }

    const QVariantMap pam = args.value(QStringLiteral("pam")).toMap();
    for (const auto &service : FaceUnlock::pamServices()) {
        if (pam.contains(service.id) && !setPamFace(service, pam.value(service.id).toBool())) {
            return failure(QStringLiteral("Could not update %1").arg(service.path));
        }
    }

    const QVariantMap system = args.value(QStringLiteral("system")).toMap();
    if (!system.isEmpty()) {
        KConfig config(FaceUnlock::systemConfigPath, KConfig::SimpleConfig);
        for (auto it = system.cbegin(); it != system.cend(); ++it) {
            const auto setting = FaceUnlock::systemSetting(it.key());
            if (!setting || !setting->accepts(it.value().toString())) {
                return failure(QStringLiteral("Rejected setting %1").arg(it.key()));
            }
            config.group(setting->group).writeEntry(setting->key, it.value().toString());
        }
        if (!config.sync() || !QFile::setPermissions(FaceUnlock::systemConfigPath, standardPermissions)) {
            return failure(QStringLiteral("Could not write %1").arg(FaceUnlock::systemConfigPath));
        }
    }

    return ActionReply::SuccessReply();
}

ActionReply FaceUnlockHelper::installtheme(const QVariantMap &args)
{
    // The settings page reads the archive as the user and sends its bytes, so the
    // helper never opens a path chosen by the caller.
    const QByteArray archive = args.value(QStringLiteral("archive")).toByteArray();
    if (archive.isEmpty() || archive.size() > 32 * 1024 * 1024) {
        return failure(QStringLiteral("The theme archive is empty or larger than 32 MB"));
    }

    // Unpack next to the themes directory so the final move stays on one filesystem.
    QDir().mkpath(FaceUnlock::userThemesPath);
    QTemporaryDir work(FaceUnlock::userThemesPath + QStringLiteral("/../.install-XXXXXX"));
    if (!work.isValid()) {
        return failure(QStringLiteral("Could not create a temporary directory"));
    }
    const QString archivePath = work.filePath(QStringLiteral("theme.archive"));
    const QString extractPath = work.filePath(QStringLiteral("theme"));
    QDir().mkpath(extractPath);
    if (!writeFile(archivePath, archive, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return failure(QStringLiteral("Could not store the theme archive"));
    }

    // bsdtar refuses absolute paths and ".." by default; don't keep owners or modes.
    QProcess bsdtar;
    bsdtar.start(QStringLiteral("/usr/bin/bsdtar"),
                 {QStringLiteral("-xf"), archivePath, QStringLiteral("-C"), extractPath, QStringLiteral("--no-same-owner"), QStringLiteral("--no-same-permissions")});
    if (!bsdtar.waitForFinished(60000) || bsdtar.exitStatus() != QProcess::NormalExit || bsdtar.exitCode() != 0) {
        return failure(QStringLiteral("The file is not a theme archive that can be unpacked"));
    }

    // Only plain files and directories: a symlink could point anywhere on the system.
    QDirIterator it(extractPath, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (info.isSymLink() || !(info.isFile() || info.isDir())) {
            return failure(QStringLiteral("Theme archives may only contain plain files and folders"));
        }
    }

    // A theme is one directory holding metadata.json and contents/ui/main.qml,
    // either at the top of the archive or as its only entry.
    QString themeRoot = extractPath;
    const QStringList entries = QDir(extractPath).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    if (!QFile::exists(extractPath + QStringLiteral("/metadata.json")) && entries.size() == 1) {
        themeRoot = extractPath + QLatin1Char('/') + entries.first();
    }
    QFile metadataFile(themeRoot + QStringLiteral("/metadata.json"));
    if (!metadataFile.open(QIODevice::ReadOnly) || !QFile::exists(themeRoot + QStringLiteral("/contents/ui/main.qml"))) {
        return failure(QStringLiteral("The archive has no metadata.json and contents/ui/main.qml"));
    }
    const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
    const QString id = metadata.value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Id")).toString();
    if (!FaceUnlock::isThemeId(id)) {
        return failure(QStringLiteral("The theme metadata has no valid KPlugin.Id"));
    }

    const QString destination = FaceUnlock::userThemesPath + QLatin1Char('/') + id;
    QDir(destination).removeRecursively();
    if (!QDir().rename(themeRoot, destination)) {
        return failure(QStringLiteral("Could not install the theme to %1").arg(destination));
    }
    // Readable by the login screen and lock screen, writable only by root.
    QProcess::execute(QStringLiteral("/usr/bin/chown"), {QStringLiteral("-R"), QStringLiteral("-h"), QStringLiteral("root:root"), destination});
    QProcess::execute(QStringLiteral("/usr/bin/chmod"), {QStringLiteral("-R"), QStringLiteral("u=rwX,go=rX"), destination});

    ActionReply reply = ActionReply::SuccessReply();
    reply.addData(QStringLiteral("id"), id);
    return reply;
}

ActionReply FaceUnlockHelper::removetheme(const QVariantMap &args)
{
    const QString id = args.value(QStringLiteral("id")).toString();
    if (!FaceUnlock::isThemeId(id)) {
        return failure(QStringLiteral("Invalid theme id"));
    }
    // Only themes the user installed; packaged ones belong to pacman.
    if (!QDir(FaceUnlock::userThemesPath + QLatin1Char('/') + id).removeRecursively()) {
        return failure(QStringLiteral("Could not remove the theme"));
    }
    return ActionReply::SuccessReply();
}

KAUTH_HELPER_MAIN("org.kde.kcontrol.kcmfaceunlock", FaceUnlockHelper)

#include "moc_faceunlockhelper.cpp"
