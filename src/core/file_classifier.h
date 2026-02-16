#pragma once
#include <string>
#include <map>
#include <vector>

class FileClassifier {
public:
    /// Initialize with default classification rules.
    FileClassifier();

    /// Initialize with custom classification rules.
    explicit FileClassifier(const std::map<std::string, std::vector<std::string>>& rules);

    /// Return the category name for a given filename based on its extension.
    /// Returns "其他" if the extension does not match any known category.
    std::string classify(const std::string& filename) const;

    /// Move a file into its category subdirectory under base_dir.
    /// Creates the subdirectory if it does not exist.
    /// Returns true on success.
    bool moveToCategory(const std::string& file_path, const std::string& base_dir) const;

    /// Replace the current rules with new ones.
    void updateRules(const std::map<std::string, std::vector<std::string>>& rules);

    /// Get the current classification rules.
    const std::map<std::string, std::vector<std::string>>& getRules() const;

private:
    // category_name -> [extensions]  (extensions include the dot, e.g. ".mp4")
    std::map<std::string, std::vector<std::string>> rules_;
};
