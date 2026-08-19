#include "drumgen_template.hpp"
#include "drumgen_serialization.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <string>

namespace downspout::drumgen {
namespace {

// Mirror of the private arrays in drumgen_pattern.cpp
constexpr int kFluesDrumkitNotes[kLaneCount] = {36, 39, 40, 41, 42, 45, 46, 50, 51, 52, 53};
constexpr int kGMNotes[kLaneCount]            = {36, 39, 38, 49, 42, 45, 46, 50, 57, 56, 75};

}  // namespace

int midiNoteForLane(LaneId lane, KitMapId kitMap) {
    const int idx = static_cast<int>(lane);
    if (idx < 0 || idx >= kLaneCount) {
        return 36;
    }
    return (kitMap == KitMapId::gm) ? kGMNotes[idx] : kFluesDrumkitNotes[idx];
}

void applyKitNotes(PatternState& pattern, KitMapId kitMap) {
    for (int i = 0; i < kLaneCount; ++i) {
        pattern.lanes[i].midiNote = midiNoteForLane(static_cast<LaneId>(i), kitMap);
    }
}

std::optional<PatternTemplate> TemplateLibrary::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    PatternTemplate tmpl;
    std::string bodyText;
    bool inBody = false;
    std::string line;

    while (std::getline(file, line)) {
        if (inBody) {
            bodyText += line + '\n';
            continue;
        }

        if (line == "# ---") {
            inBody = true;
            continue;
        }

        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        const auto key = line.substr(0, sep);
        const auto val = line.substr(sep + 1);

        if (key == "name")        tmpl.name        = val;
        else if (key == "source")      tmpl.source      = val;
        else if (key == "time_sig")    tmpl.timeSig     = val;
        else if (key == "description") tmpl.description = val;
    }

    if (tmpl.name.empty() || bodyText.empty()) {
        return std::nullopt;
    }

    const auto pattern = deserializePatternState(bodyText);
    if (!pattern.has_value()) {
        return std::nullopt;
    }

    tmpl.pattern = *pattern;
    return tmpl;
}

void TemplateLibrary::scanDirectory(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        return;
    }

    std::vector<std::string> filePaths;
    std::vector<std::string> subDirs;

    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        const std::string full = directory + "/" + name;
        struct stat st {};
        if (stat(full.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            subDirs.push_back(full);
        } else if (S_ISREG(st.st_mode) && name.size() > 11u &&
                   name.substr(name.size() - 11u) == ".dg-pattern") {
            filePaths.push_back(full);
        }
    }
    closedir(dir);

    std::sort(filePaths.begin(), filePaths.end());
    for (const auto& p : filePaths) {
        if (auto t = load(p)) {
            templates_.push_back(std::move(*t));
        }
    }

    std::sort(subDirs.begin(), subDirs.end());
    for (const auto& d : subDirs) {
        scanDirectory(d);
    }
}

void TemplateLibrary::scan(const std::string& directory) {
    scanDirectory(directory);
    std::stable_sort(templates_.begin(), templates_.end(),
                     [](const PatternTemplate& a, const PatternTemplate& b) {
                         return a.name < b.name;
                     });
}

const PatternTemplate* TemplateLibrary::get(int index) const {
    if (index < 0 || index >= static_cast<int>(templates_.size())) {
        return nullptr;
    }
    return &templates_[static_cast<std::size_t>(index)];
}

const PatternTemplate* TemplateLibrary::findByName(const std::string& name) const {
    for (const auto& t : templates_) {
        if (t.name == name) {
            return &t;
        }
    }
    return nullptr;
}

}  // namespace downspout::drumgen
