#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QMap>
#include <QStringList>

#include "../core/download_manager.h"

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const ManagerConfig& current, QWidget* parent = nullptr);
    ManagerConfig result() const;

    /// Returns the current file type list (used by other components)
    static QStringList fileTypes();
    static const QString kDefaultFileTypes;

private slots:
    void onBrowseSaveDir();
    void onAccepted();
    void onInstallExtension();
    void onResetFileTypes();

private:
    QWidget* createGeneralTab();
    QWidget* createFileTypesTab();
    QWidget* createBrowserTab();
    void loadFromSettings();
    void saveToSettings();
    void updateExtensionFileTypes();

    struct BrowserInfo {
        QString name;
        QString exePath;
        QString extensionUrl;
        QString registryKey;
        bool detected = false;
        bool isChromium = true;
    };

    void detectBrowsers();
    QString getExtensionDir() const;
    bool installExtensionFiles(const QString& targetDir);
    bool writeRegistryPolicy(const BrowserInfo& info, const QString& extDir);
    bool removeRegistryPolicy(const BrowserInfo& info);
    void refreshInstallStatus();

    QTabWidget* tabs_;
    QLineEdit* save_dir_edit_;
    QSpinBox* max_blocks_spin_;
    QSpinBox* max_concurrent_spin_;
    QSpinBox* speed_limit_spin_;
    QCheckBox* clipboard_check_;
    QCheckBox* autostart_check_;
    QCheckBox* auto_open_folder_check_;
    QPlainTextEdit* file_types_edit_;
    QDialogButtonBox* button_box_;

    QMap<QString, BrowserInfo> browsers_;
    QMap<QString, QCheckBox*> browser_checks_;
    QMap<QString, QLabel*> browser_status_labels_;

    ManagerConfig config_;
};
