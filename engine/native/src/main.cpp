/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// faceunlock: manage face models and run recognition. Arguments follow Howdy's CLI:
//   faceunlock [--plain] [-y] [-U USER] add [LABEL] | remove ID | clear | list | test
//   faceunlock compare USER     what pam_faceunlock runs; exit code as Howdy's compare.py

#include "engine.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <pwd.h>
#include <unistd.h>

using namespace FaceUnlock;

namespace
{
std::string defaultUser()
{
    if (const char *sudoUser = std::getenv("SUDO_USER")) {
        return sudoUser;
    }
    const passwd *entry = getpwuid(getuid());
    return entry ? entry->pw_name : "";
}

bool validUser(const std::string &user)
{
    return !user.empty() && user.find('/') == std::string::npos && getpwnam(user.c_str());
}

int usage()
{
    std::cerr << "usage: faceunlock [--plain] [-y] [-U USER] add [LABEL] | remove ID | clear | list | test\n"
                 "       faceunlock compare USER\n";
    return Error;
}

int requireRoot()
{
    if (geteuid() != 0) {
        std::cerr << "This command changes face models and needs root, e.g. sudo faceunlock ...\n";
        return Error;
    }
    return 0;
}

int add(const std::string &user, std::string label, bool plain)
{
    if (const int error = requireRoot()) {
        return error;
    }
    std::vector<FaceModel> models = loadModels(user);
    if (models.size() > 3) {
        std::cout << "NOTICE: Each additional model slows down the face recognition engine slightly\n";
    }
    const int nextId = models.empty() ? 0 : models.back().id + 1;
    std::erase(label, ',');
    if (label.empty()) {
        label = "Model #" + std::to_string(nextId);
    }
    label = label.substr(0, 24);
    if (!plain) {
        std::cout << "Adding face model for the user " << user << '\n';
    }

    FaceModel model{nextId, label, std::time(nullptr), {}};
    const ScanResult result = enroll(Config(), model.encodings, [](const std::string &line) {
        std::cout << line << std::endl;
    });
    if (result.code != Recognized) {
        std::cout << result.message << std::endl;
        return Error;
    }

    std::filesystem::create_directories(modelsDir);
    std::filesystem::permissions(modelsDir, std::filesystem::perms(0755));
    models.push_back(model);
    if (!saveModels(user, models)) {
        std::cout << "Could not save the face model" << std::endl;
        return Error;
    }
    std::cout << "Face model saved as \"" << label << "\" (ID " << nextId << ")" << std::endl;
    return 0;
}

int remove(const std::string &user, const std::string &idText)
{
    if (const int error = requireRoot()) {
        return error;
    }
    int id = -1;
    try {
        id = std::stoi(idText);
    } catch (...) {
        return usage();
    }
    std::vector<FaceModel> models = loadModels(user);
    const auto removed = std::erase_if(models, [id](const FaceModel &model) {
        return model.id == id;
    });
    if (removed == 0) {
        std::cout << "No face model with the ID " << id << " exists for " << user << std::endl;
        return Error;
    }
    if (!saveModels(user, models)) {
        return Error;
    }
    std::cout << "Removed face model " << id << std::endl;
    return 0;
}

int list(const std::string &user, bool plain)
{
    const std::vector<FaceModel> models = loadModels(user);
    if (models.empty()) {
        if (!plain) {
            std::cout << "No face model known for the user " << user << ", please run:\n\n\tsudo faceunlock -U " << user << " add\n";
        }
        return Error;
    }
    if (!plain) {
        std::cout << "Known face models for " << user << ":\n\nID  Date                 Label\n";
    }
    for (const FaceModel &model : models) {
        char date[32];
        const std::time_t time = model.time;
        std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
        if (plain) {
            std::cout << model.id << ',' << date << ',' << model.label << '\n';
        } else {
            std::cout << model.id << std::string(std::max<int>(1, 4 - int(std::to_string(model.id).size())), ' ') << date << "  " << model.label << '\n';
        }
    }
    return 0;
}
}

int main(int argc, char **argv)
{
    bool plain = false;
    std::string user;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--plain") {
            plain = true;
        } else if (arg == "-y") {
            // Nothing asks questions; accepted for Howdy compatibility.
        } else if ((arg == "-U" || arg == "--user") && i + 1 < argc) {
            user = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.empty()) {
        return usage();
    }
    const std::string command = positional[0];

    if (command == "compare") {
        // Run by pam_faceunlock with the user being authenticated.
        if (positional.size() != 2 || !validUser(positional[1])) {
            return Aborted;
        }
        const ScanResult result = compare(Config(), positional[1]);
        std::cout << result.message << std::endl;
        return result.code;
    }

    if (user.empty()) {
        user = defaultUser();
    }
    if (!validUser(user)) {
        std::cerr << "Unknown user " << user << '\n';
        return Error;
    }

    if (command == "add") {
        return add(user, positional.size() > 1 ? positional[1] : std::string(), plain);
    }
    if (command == "remove" && positional.size() == 2) {
        return remove(user, positional[1]);
    }
    if (command == "clear") {
        if (const int error = requireRoot()) {
            return error;
        }
        std::filesystem::remove(modelPath(user));
        std::cout << "Removed all face models for " << user << std::endl;
        return 0;
    }
    if (command == "list") {
        return list(user, plain);
    }
    if (command == "test") {
        const Config config;
        const ScanResult result = compare(config, user);
        const ScanStats &s = result.stats;
        std::cout << result.message << '\n'
                  << "  best score       " << result.bestScore << " (needs " << config.number("video/certainty", 6.0) << " or lower)\n"
                  << "  frames           " << s.frames << " read, " << s.darkFrames << " too dark, " << s.faceFrames << " with a face\n"
                  << "  face confidence  " << s.bestDetection << " (detector keeps 0.8 or higher)\n"
                  << "  camera open      " << s.cameraMs << " ms\n"
                  << "  networks load    " << s.networksMs << " ms\n"
                  << "  first frame      " << s.firstFrameMs << " ms after start\n"
                  << "  detection        " << s.detectMs << " ms total\n"
                  << "  encoding         " << s.encodeMs << " ms total" << std::endl;
        return result.code;
    }
    if (command == "version") {
        std::cout << "faceunlock " << FACEUNLOCK_VERSION << '\n';
        return 0;
    }
    return usage();
}
