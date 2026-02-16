#include <gtest/gtest.h>
#include "file_classifier.h"
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace fs = std::filesystem;

namespace {

// Helpers for UTF-8 category names
const std::string CAT_VIDEO    = "\xe8\xa7\x86\xe9\xa2\x91";       // 视频
const std::string CAT_AUDIO    = "\xe9\x9f\xb3\xe9\xa2\x91";       // 音频
const std::string CAT_DOC      = "\xe6\x96\x87\xe6\xa1\xa3";       // 文档
const std::string CAT_ARCHIVE  = "\xe5\x8e\x8b\xe7\xbc\xa9\xe5\x8c\x85"; // 压缩包
const std::string CAT_PROGRAM  = "\xe7\xa8\x8b\xe5\xba\x8f";       // 程序
const std::string CAT_IMAGE    = "\xe5\x9b\xbe\xe7\x89\x87";       // 图片
const std::string CAT_OTHER    = "\xe5\x85\xb6\xe4\xbb\x96";       // 其他

// ── Default rules completeness ─────────────────────────────────

TEST(FileClassifierTest, DefaultRulesContainAllCategories) {
    FileClassifier fc;
    const auto& rules = fc.getRules();
    EXPECT_NE(rules.find(CAT_VIDEO),   rules.end());
    EXPECT_NE(rules.find(CAT_AUDIO),   rules.end());
    EXPECT_NE(rules.find(CAT_DOC),     rules.end());
    EXPECT_NE(rules.find(CAT_ARCHIVE), rules.end());
    EXPECT_NE(rules.find(CAT_PROGRAM), rules.end());
    EXPECT_NE(rules.find(CAT_IMAGE),   rules.end());
}

// ── classify: known extensions ─────────────────────────────────

TEST(FileClassifierTest, ClassifyVideoExtensions) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("movie.mp4"), CAT_VIDEO);
    EXPECT_EQ(fc.classify("clip.avi"),  CAT_VIDEO);
    EXPECT_EQ(fc.classify("film.mkv"),  CAT_VIDEO);
    EXPECT_EQ(fc.classify("rec.mov"),   CAT_VIDEO);
}

TEST(FileClassifierTest, ClassifyAudioExtensions) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("song.mp3"),  CAT_AUDIO);
    EXPECT_EQ(fc.classify("track.flac"),CAT_AUDIO);
    EXPECT_EQ(fc.classify("sound.wav"), CAT_AUDIO);
    EXPECT_EQ(fc.classify("music.aac"), CAT_AUDIO);
}

TEST(FileClassifierTest, ClassifyDocumentExtensions) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("report.pdf"),  CAT_DOC);
    EXPECT_EQ(fc.classify("letter.doc"),  CAT_DOC);
    EXPECT_EQ(fc.classify("essay.docx"),  CAT_DOC);
    EXPECT_EQ(fc.classify("data.xls"),    CAT_DOC);
    EXPECT_EQ(fc.classify("sheet.xlsx"),  CAT_DOC);
}

TEST(FileClassifierTest, ClassifyArchiveExtensions) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("archive.zip"),    CAT_ARCHIVE);
    EXPECT_EQ(fc.classify("backup.rar"),     CAT_ARCHIVE);
    EXPECT_EQ(fc.classify("compressed.7z"),  CAT_ARCHIVE);
    EXPECT_EQ(fc.classify("package.tar.gz"), CAT_ARCHIVE);
}

TEST(FileClassifierTest, ClassifyProgramExtensions) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("setup.exe"), CAT_PROGRAM);
    EXPECT_EQ(fc.classify("installer.msi"), CAT_PROGRAM);
}

TEST(FileClassifierTest, ClassifyImageExtensions) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("photo.jpg"),  CAT_IMAGE);
    EXPECT_EQ(fc.classify("icon.png"),   CAT_IMAGE);
    EXPECT_EQ(fc.classify("anim.gif"),   CAT_IMAGE);
    EXPECT_EQ(fc.classify("scan.bmp"),   CAT_IMAGE);
    EXPECT_EQ(fc.classify("pic.webp"),   CAT_IMAGE);
}

// ── classify: unknown extension → "其他" ───────────────────────

TEST(FileClassifierTest, ClassifyUnknownExtension) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("readme.txt"),    CAT_OTHER);
    EXPECT_EQ(fc.classify("data.csv"),      CAT_OTHER);
    EXPECT_EQ(fc.classify("script.py"),     CAT_OTHER);
}

// ── classify: no extension → "其他" ────────────────────────────

TEST(FileClassifierTest, ClassifyNoExtension) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("Makefile"),  CAT_OTHER);
    EXPECT_EQ(fc.classify("README"),    CAT_OTHER);
}

// ── classify: case-insensitive ─────────────────────────────────

TEST(FileClassifierTest, ClassifyCaseInsensitive) {
    FileClassifier fc;
    EXPECT_EQ(fc.classify("VIDEO.MP4"), CAT_VIDEO);
    EXPECT_EQ(fc.classify("Photo.JPG"), CAT_IMAGE);
    EXPECT_EQ(fc.classify("SONG.FLAC"),CAT_AUDIO);
    EXPECT_EQ(fc.classify("Doc.PDF"),  CAT_DOC);
}

// ── updateRules ────────────────────────────────────────────────

TEST(FileClassifierTest, UpdateRulesReplacesExisting) {
    FileClassifier fc;
    // Before update, .txt is "其他"
    EXPECT_EQ(fc.classify("notes.txt"), CAT_OTHER);

    // Add a custom rule
    std::map<std::string, std::vector<std::string>> custom = {
        {"text", {".txt", ".md"}}
    };
    fc.updateRules(custom);

    EXPECT_EQ(fc.classify("notes.txt"), "text");
    EXPECT_EQ(fc.classify("readme.md"), "text");
    // Old rules are gone
    EXPECT_EQ(fc.classify("movie.mp4"), CAT_OTHER);
}

// ── custom constructor ─────────────────────────────────────────

TEST(FileClassifierTest, CustomConstructor) {
    std::map<std::string, std::vector<std::string>> custom = {
        {"code", {".cpp", ".h", ".py"}}
    };
    FileClassifier fc(custom);

    EXPECT_EQ(fc.classify("main.cpp"), "code");
    EXPECT_EQ(fc.classify("header.h"), "code");
    EXPECT_EQ(fc.classify("movie.mp4"), CAT_OTHER);
}

// ── moveToCategory ─────────────────────────────────────────────

class FileClassifierMoveTest : public ::testing::Test {
protected:
    std::string test_dir_;

    void SetUp() override {
        test_dir_ = "fc_test_temp";
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        fs::remove_all(test_dir_);
    }

    // Create a dummy file and return its path
    std::string createFile(const std::string& name) {
        auto path = (fs::path(test_dir_) / name).string();
        std::ofstream ofs(path);
        ofs << "test content";
        return path;
    }
};

TEST_F(FileClassifierMoveTest, MovesFileToCorrectCategory) {
    FileClassifier fc;
    std::string src = createFile("movie.mp4");

    ASSERT_TRUE(fc.moveToCategory(src, test_dir_));

    // File should now be in <test_dir>/视频/movie.mp4
    fs::path expected = fs::path(test_dir_) / CAT_VIDEO / "movie.mp4";
    EXPECT_TRUE(fs::exists(expected));
    EXPECT_FALSE(fs::exists(src));
}

TEST_F(FileClassifierMoveTest, MovesUnknownToOther) {
    FileClassifier fc;
    std::string src = createFile("readme.txt");

    ASSERT_TRUE(fc.moveToCategory(src, test_dir_));

    fs::path expected = fs::path(test_dir_) / CAT_OTHER / "readme.txt";
    EXPECT_TRUE(fs::exists(expected));
}

TEST_F(FileClassifierMoveTest, CreatesCategoryDirIfNeeded) {
    FileClassifier fc;
    std::string src = createFile("photo.png");

    fs::path cat_dir = fs::path(test_dir_) / CAT_IMAGE;
    ASSERT_FALSE(fs::exists(cat_dir));

    ASSERT_TRUE(fc.moveToCategory(src, test_dir_));
    EXPECT_TRUE(fs::exists(cat_dir));
}

TEST_F(FileClassifierMoveTest, ReturnsFalseForNonExistentFile) {
    FileClassifier fc;
    EXPECT_FALSE(fc.moveToCategory("nonexistent_file.mp4", test_dir_));
}

} // namespace
