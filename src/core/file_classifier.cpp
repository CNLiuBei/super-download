#include "file_classifier.h"
#include <algorithm>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

// ── helpers ────────────────────────────────────────────────────

/// Return the file extension in lower-case (e.g. ".mp4").
/// Handles compound extensions like ".tar.gz".
static std::string extractExtension(const std::string& filename) {
    // Check for ".tar.gz" first (the only compound extension in default rules)
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.size() > 7) {  // ".tar.gz" is 7 chars
        auto suffix = lower.substr(lower.size() - 7);
        if (suffix == ".tar.gz") {
            return ".tar.gz";
        }
    }

    // Fall back to std::filesystem for simple extensions
    auto ext = fs::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

static std::map<std::string, std::vector<std::string>> defaultRules() {
    return {
        {"\xe8\xa7\x86\xe9\xa2\x91",   {".mp4", ".avi", ".mkv", ".mov"}},          // 视频
        {"\xe9\x9f\xb3\xe9\xa2\x91",   {".mp3", ".flac", ".wav", ".aac"}},         // 音频
        {"\xe6\x96\x87\xe6\xa1\xa3",   {".pdf", ".doc", ".docx", ".xls", ".xlsx"}},// 文档
        {"\xe5\x8e\x8b\xe7\xbc\xa9\xe5\x8c\x85", {".zip", ".rar", ".7z", ".tar.gz"}}, // 压缩包
        {"\xe7\xa8\x8b\xe5\xba\x8f",   {".exe", ".msi"}},                          // 程序
        {"\xe5\x9b\xbe\xe7\x89\x87",   {".jpg", ".png", ".gif", ".bmp", ".webp"}}  // 图片
    };
}

// ── FileClassifier implementation ──────────────────────────────

FileClassifier::FileClassifier()
    : rules_(defaultRules()) {}

FileClassifier::FileClassifier(const std::map<std::string, std::vector<std::string>>& rules)
    : rules_(rules) {}

std::string FileClassifier::classify(const std::string& filename) const {
    std::string ext = extractExtension(filename);
    if (ext.empty()) {
        return "\xe5\x85\xb6\xe4\xbb\x96"; // 其他
    }

    for (const auto& [category, extensions] : rules_) {
        for (const auto& rule_ext : extensions) {
            // Compare case-insensitively (rule_ext is already lower-case by convention)
            std::string rule_lower = rule_ext;
            std::transform(rule_lower.begin(), rule_lower.end(), rule_lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == rule_lower) {
                return category;
            }
        }
    }

    return "\xe5\x85\xb6\xe4\xbb\x96"; // 其他
}

bool FileClassifier::moveToCategory(const std::string& file_path, const std::string& base_dir) const {
    try {
        fs::path src(file_path);
        if (!fs::exists(src)) {
            return false;
        }

        std::string category = classify(src.filename().string());
        fs::path dest_dir = fs::path(base_dir) / category;

        // Create category subdirectory if needed
        if (!fs::exists(dest_dir)) {
            fs::create_directories(dest_dir);
        }

        fs::path dest = dest_dir / src.filename();
        fs::rename(src, dest);
        return true;
    } catch (...) {
        return false;
    }
}

void FileClassifier::updateRules(const std::map<std::string, std::vector<std::string>>& rules) {
    rules_ = rules;
}

const std::map<std::string, std::vector<std::string>>& FileClassifier::getRules() const {
    return rules_;
}
