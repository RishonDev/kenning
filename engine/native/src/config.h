/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Paths, exit codes and config.ini reading, header-only so pam_faceunlock can
// use them without linking OpenCV or Qt.

#include <fstream>
#include <map>
#include <string>

namespace FaceUnlock
{
// Set at build time only: a runtime override would let a user point
// authentication at their own face models.
#ifndef FACEUNLOCK_SYSCONFDIR
#define FACEUNLOCK_SYSCONFDIR "/etc/faceunlock"
#endif
#ifndef FACEUNLOCK_DATADIR
#define FACEUNLOCK_DATADIR "/usr/share/plasma-faceunlock"
#endif

inline constexpr const char *configPath = FACEUNLOCK_SYSCONFDIR "/config.ini";
inline constexpr const char *modelsDir = FACEUNLOCK_SYSCONFDIR "/models";
inline constexpr const char *detectorModel = FACEUNLOCK_DATADIR "/models/face_detection_yunet_2023mar.onnx";
inline constexpr const char *recognizerModel = FACEUNLOCK_DATADIR "/models/face_recognition_sface_2021dec.onnx";

// Exit codes shared with Howdy's compare.py
enum ExitCode {
    Recognized = 0,
    Error = 1,
    NoModel = 10,
    Timeout = 11,
    Aborted = 12,
    TooDark = 13,
};

// config.ini values keyed "section/key"
class Config
{
public:
    explicit Config(const std::string &path = configPath)
    {
        std::ifstream file(path);
        std::string section;
        std::string line;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
            } else if (const auto equals = line.find('='); equals != std::string::npos) {
                m_values[section + '/' + trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
            }
        }
    }

    std::string string(const std::string &key, const std::string &fallback = {}) const
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? fallback : it->second;
    }

    double number(const std::string &key, double fallback) const
    {
        try {
            return std::stod(string(key));
        } catch (...) {
            return fallback;
        }
    }

    bool boolean(const std::string &key, bool fallback) const
    {
        const std::string value = string(key);
        return value.empty() ? fallback : value == "true";
    }

private:
    static std::string trim(const std::string &text)
    {
        const auto begin = text.find_first_not_of(" \t\r");
        const auto end = text.find_last_not_of(" \t\r");
        return begin == std::string::npos ? std::string() : text.substr(begin, end - begin + 1);
    }

    std::map<std::string, std::string> m_values;
};

inline std::string modelPath(const std::string &user)
{
    return std::string(modelsDir) + '/' + user + ".dat";
}
}
