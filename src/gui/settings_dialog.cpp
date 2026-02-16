#include "settings_dialog.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QProcess>
#include <QTimer>
#include <QRegularExpression>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <comdef.h>
#endif

// ---------------------------------------------------------------------------
// Default file types
// ---------------------------------------------------------------------------

const QString SettingsDialog::kDefaultFileTypes =
    "3GP 7Z AAC ACE AIF APK ARJ ASF AVI BIN BZ2 EXE GZ GZIP IMG ISO LZH "
    "M4A M4V MKV MOV MP3 MP4 MPA MPE MPEG MPG MSI MSU OGG OGV "
    "PDF PLJ PPS PPT QT RAR RM RMVB SEA SIT SITX TAR TIF "
    "TIFF WAV WMA WMV Z ZIP "
    "DOC DOCX XLS XLSX PPTX FLAC WEBM FLV DEB RPM APPIMAGE DMG "
    "ROM TORRENT";

QStringList SettingsDialog::fileTypes()
{
    QSettings s;
    QString raw = s.value("settings/file_types", kDefaultFileTypes).toString();
    QStringList list;
    for (const auto& t : raw.split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts))
        list << t.toLower().trimmed();
    return list;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SettingsDialog::SettingsDialog(const ManagerConfig& current, QWidget* parent)
    : QDialog(parent), config_(current)
{
    setWindowTitle(QString::fromUtf8("设置"));
    setMinimumSize(560, 480);

    detectBrowsers();

    tabs_ = new QTabWidget(this);
    tabs_->addTab(createGeneralTab(), QString::fromUtf8("常规"));
    tabs_->addTab(createFileTypesTab(), QString::fromUtf8("文件类型"));
    tabs_->addTab(createBrowserTab(), QString::fromUtf8("浏览器集成"));

    button_box_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    button_box_->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("保存"));
    button_box_->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));
    connect(button_box_, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* top = new QVBoxLayout(this);
    top->setContentsMargins(16, 16, 16, 16);
    top->setSpacing(12);
    top->addWidget(tabs_);
    top->addWidget(button_box_);

    loadFromSettings();
}

// ---------------------------------------------------------------------------
// General tab
// ---------------------------------------------------------------------------

QWidget* SettingsDialog::createGeneralTab()
{
    auto* page = new QWidget;

    save_dir_edit_ = new QLineEdit(page);
    auto* browse = new QPushButton(QString::fromUtf8("浏览"), page);
    connect(browse, &QPushButton::clicked, this, &SettingsDialog::onBrowseSaveDir);

    auto* dirRow = new QHBoxLayout;
    dirRow->setSpacing(8);
    dirRow->addWidget(save_dir_edit_, 1);
    dirRow->addWidget(browse);

    max_blocks_spin_ = new QSpinBox(page);
    max_blocks_spin_->setRange(1, 32);

    max_concurrent_spin_ = new QSpinBox(page);
    max_concurrent_spin_->setRange(1, 10);

    speed_limit_spin_ = new QSpinBox(page);
    speed_limit_spin_->setRange(0, 999999);
    speed_limit_spin_->setSuffix(QString::fromUtf8(" KB/s"));
    speed_limit_spin_->setSpecialValueText(QString::fromUtf8("不限速"));

    clipboard_check_ = new QCheckBox(QString::fromUtf8("启用剪贴板监听"), page);
    clipboard_check_->setChecked(true);

    autostart_check_ = new QCheckBox(QString::fromUtf8("开机自动启动"), page);
    autostart_check_->setChecked(false);

    auto_open_folder_check_ = new QCheckBox(QString::fromUtf8("下载完成后自动打开文件夹"), page);
    auto_open_folder_check_->setChecked(false);
#ifdef _WIN32
    // Check current autostart status
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    autostart_check_->setChecked(reg.contains("SuperDownload"));
#endif

    auto* form = new QFormLayout;
    form->setSpacing(12);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addRow(QString::fromUtf8("默认下载路径"), dirRow);
    form->addRow(QString::fromUtf8("最大分块数"), max_blocks_spin_);
    form->addRow(QString::fromUtf8("最大并发任务"), max_concurrent_spin_);
    form->addRow(QString::fromUtf8("速度限制"), speed_limit_spin_);
    form->addRow("", clipboard_check_);
    form->addRow("", autostart_check_);
    form->addRow("", auto_open_folder_check_);

    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->addLayout(form);
    lay->addStretch();
    return page;
}

// ---------------------------------------------------------------------------
// File types tab
// ---------------------------------------------------------------------------

QWidget* SettingsDialog::createFileTypesTab()
{
    auto* page = new QWidget;

    auto* label = new QLabel(
        QString::fromUtf8("自动开始下载下列类型的文件:"), page);

    file_types_edit_ = new QPlainTextEdit(page);
    file_types_edit_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    file_types_edit_->setMinimumHeight(120);

    auto* resetBtn = new QPushButton(QString::fromUtf8("默认(D)"), page);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::onResetFileTypes);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(resetBtn);

    auto* noteLabel = new QLabel(
        QString::fromUtf8("用空格分隔扩展名，不需要加点号。\n"
                          "保存后会同步到浏览器扩展和剪贴板监听。"), page);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("color: #6b7280; font-size: 12px;");

    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(10);
    lay->addWidget(label);
    lay->addWidget(file_types_edit_, 1);
    lay->addLayout(btnRow);
    lay->addWidget(noteLabel);
    return page;
}

void SettingsDialog::onResetFileTypes()
{
    file_types_edit_->setPlainText(kDefaultFileTypes);
}

// ---------------------------------------------------------------------------
// Browser tab
// ---------------------------------------------------------------------------

QWidget* SettingsDialog::createBrowserTab()
{
    auto* page = new QWidget;

    auto* group = new QGroupBox(QString::fromUtf8("一键安装扩展到浏览器:"), page);
    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(8);

    QStringList browserOrder = {"Google Chrome", "Microsoft Edge",
                                "Mozilla Firefox", "Opera", "Brave"};

    for (const auto& name : browserOrder) {
        bool detected = browsers_.contains(name) && browsers_[name].detected;

        auto* row = new QHBoxLayout;
        row->setSpacing(8);

        auto* cb = new QCheckBox(name, group);
        cb->setEnabled(detected);
        cb->setChecked(false);
        if (!detected)
            cb->setText(name + QString::fromUtf8("  (未检测到)"));
        browser_checks_[name] = cb;
        row->addWidget(cb, 1);

        auto* statusLabel = new QLabel(group);
        statusLabel->setStyleSheet("font-size: 12px;");
        browser_status_labels_[name] = statusLabel;
        row->addWidget(statusLabel);

        groupLayout->addLayout(row);
    }
    groupLayout->addStretch();

    auto* installBtn = new QPushButton(QString::fromUtf8("⚡ 一键安装扩展"), page);
    installBtn->setMinimumHeight(36);
    installBtn->setStyleSheet(
        "QPushButton { background-color: #2563eb; color: white; border: none; "
        "border-radius: 6px; font-size: 14px; font-weight: bold; padding: 6px 20px; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QPushButton:pressed { background-color: #1e40af; }");
    connect(installBtn, &QPushButton::clicked, this, &SettingsDialog::onInstallExtension);

    auto* noteLabel = new QLabel(
        QString::fromUtf8(
            "说明: 安装后会在桌面创建带扩展的浏览器快捷方式。\n"
            "通过该快捷方式启动浏览器即可自动加载 Super Download 扩展。\n"
            "首次使用时浏览器会提示「开发者模式扩展」，请选择保留。"), page);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("color: #6b7280; font-size: 12px;");

    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(12);
    lay->addWidget(group);
    lay->addWidget(installBtn);
    lay->addWidget(noteLabel);
    lay->addStretch();

    refreshInstallStatus();
    return page;
}

// ---------------------------------------------------------------------------
// Browser detection
// ---------------------------------------------------------------------------

void SettingsDialog::detectBrowsers()
{
#ifdef _WIN32
    struct BrowserPath {
        QString name;
        QStringList paths;
        QString extUrl;
        QString regKey;
        bool chromium;
    };

    QStringList searchBases = {
        QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles")),
        QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles(x86)")),
        QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")),
    };

    QList<BrowserPath> known = {
        {"Google Chrome",
         {"Google/Chrome/Application/chrome.exe"},
         "chrome://extensions", "", true},
        {"Microsoft Edge",
         {"Microsoft/Edge/Application/msedge.exe"},
         "edge://extensions", "", true},
        {"Mozilla Firefox",
         {"Mozilla Firefox/firefox.exe"},
         "about:addons", "", false},
        {"Opera",
         {"Opera/launcher.exe", "Opera Software/Opera Stable/opera.exe"},
         "opera://extensions", "", true},
        {"Brave",
         {"BraveSoftware/Brave-Browser/Application/brave.exe"},
         "brave://extensions", "", true},
    };

    for (auto& bp : known) {
        BrowserInfo info;
        info.name = bp.name;
        info.extensionUrl = bp.extUrl;
        info.registryKey = bp.regKey;
        info.isChromium = bp.chromium;
        info.detected = false;

        for (const auto& base : searchBases) {
            if (base.isEmpty()) continue;
            for (const auto& rel : bp.paths) {
                QString full = base + "/" + rel;
                if (QFileInfo::exists(full)) {
                    info.exePath = full;
                    info.detected = true;
                    break;
                }
            }
            if (info.detected) break;
        }
        browsers_[bp.name] = info;
    }
#endif
}

// ---------------------------------------------------------------------------
// Extension directory & file copy
// ---------------------------------------------------------------------------

QString SettingsDialog::getExtensionDir() const
{
    // Use a path WITHOUT spaces to avoid argument parsing issues
    QString appData = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA"));
    return appData + "/SuperDownload/extension";
}

bool SettingsDialog::installExtensionFiles(const QString& targetDir)
{
    QString appDir = QApplication::applicationDirPath();
    QString srcDir = appDir + "/browser_extension";
    if (!QDir(srcDir).exists())
        srcDir = QFileInfo(appDir + "/../../browser_extension").absoluteFilePath();
    if (!QDir(srcDir).exists())
        srcDir = QFileInfo(appDir + "/../../../browser_extension").absoluteFilePath();
    if (!QDir(srcDir).exists())
        return false;

    QDir().mkpath(targetDir);

    QDir src(srcDir);
    for (const auto& entry : src.entryInfoList(QDir::Files)) {
        QString destFile = targetDir + "/" + entry.fileName();
        if (QFile::exists(destFile))
            QFile::remove(destFile);
        if (!QFile::copy(entry.absoluteFilePath(), destFile))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Create desktop shortcut with --load-extension (Windows COM IShellLink)
// This is the ONLY reliable way to auto-load unpacked extensions.
// ---------------------------------------------------------------------------

#ifdef _WIN32

// Patch existing Start Menu shortcuts to add --load-extension
static bool patchStartMenuShortcut(const QString& browserName,
                                   const QString& extDir)
{
    // Find the shortcut in common Start Menu locations
    wchar_t startMenuPath[MAX_PATH];
    // User start menu
    SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startMenuPath);
    QString userStartMenu = QString::fromWCharArray(startMenuPath);

    // Common start menu (all users)
    SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, startMenuPath);
    QString commonStartMenu = QString::fromWCharArray(startMenuPath);

    // Known shortcut locations
    QStringList lnkPaths;
    if (browserName == "Google Chrome") {
        lnkPaths << userStartMenu + "/Google Chrome.lnk"
                  << commonStartMenu + "/Google Chrome.lnk";
    } else if (browserName == "Microsoft Edge") {
        lnkPaths << userStartMenu + "/Microsoft Edge.lnk"
                  << commonStartMenu + "/Microsoft Edge.lnk";
    } else if (browserName == "Brave") {
        lnkPaths << userStartMenu + "/Brave.lnk"
                  << userStartMenu + "/Brave Browser.lnk"
                  << commonStartMenu + "/Brave.lnk"
                  << commonStartMenu + "/Brave Browser.lnk";
    }

    QString loadExtArg = QString("--load-extension=\"%1\"")
        .arg(QDir::toNativeSeparators(extDir));

    bool anyPatched = false;

    for (const auto& lnkPath : lnkPaths) {
        if (!QFileInfo::exists(lnkPath)) continue;

        CoInitialize(nullptr);
        IShellLinkW* psl = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW, reinterpret_cast<void**>(&psl));
        if (FAILED(hr)) { CoUninitialize(); continue; }

        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (FAILED(hr)) { psl->Release(); CoUninitialize(); continue; }

        // Load existing shortcut
        hr = ppf->Load(lnkPath.toStdWString().c_str(), STGM_READWRITE);
        if (FAILED(hr)) { ppf->Release(); psl->Release(); CoUninitialize(); continue; }

        // Read current arguments
        wchar_t existingArgs[4096] = {};
        psl->GetArguments(existingArgs, 4096);
        QString currentArgs = QString::fromWCharArray(existingArgs);

        // Check if already has --load-extension
        if (currentArgs.contains("--load-extension")) {
            ppf->Release(); psl->Release(); CoUninitialize();
            anyPatched = true; // already patched
            continue;
        }

        // Append our argument
        QString newArgs = currentArgs.isEmpty()
            ? loadExtArg
            : currentArgs + " " + loadExtArg;
        psl->SetArguments(newArgs.toStdWString().c_str());

        // Save back
        hr = ppf->Save(lnkPath.toStdWString().c_str(), TRUE);
        if (SUCCEEDED(hr)) anyPatched = true;

        ppf->Release();
        psl->Release();
        CoUninitialize();
    }

    return anyPatched;
}

bool SettingsDialog::writeRegistryPolicy(const BrowserInfo& info, const QString& extDir)
{
    if (!info.isChromium || !info.detected) return false;

    // Patch existing Start Menu shortcut to include --load-extension
    patchStartMenuShortcut(info.name, extDir);

    return true;
}

bool SettingsDialog::removeRegistryPolicy(const BrowserInfo& info)
{
    (void)info;
    return true;
}

#else
bool SettingsDialog::writeRegistryPolicy(const BrowserInfo&, const QString&) { return false; }
bool SettingsDialog::removeRegistryPolicy(const BrowserInfo&) { return false; }
#endif

// ---------------------------------------------------------------------------
// Refresh install status
// ---------------------------------------------------------------------------

void SettingsDialog::refreshInstallStatus()
{
#ifdef _WIN32
    // Check if extension files are installed
    QString extDir = getExtensionDir();
    bool extInstalled = QFileInfo::exists(extDir + "/manifest.json");

    for (auto it = browser_status_labels_.begin(); it != browser_status_labels_.end(); ++it) {
        const QString& name = it.key();
        QLabel* label = it.value();

        if (!browsers_.contains(name) || !browsers_[name].detected) {
            label->setText("");
            continue;
        }

        const auto& info = browsers_[name];
        if (!info.isChromium) {
            label->setText(QString::fromUtf8("手动安装"));
            label->setStyleSheet("color: #9ca3af; font-size: 12px;");
            continue;
        }

        if (extInstalled) {
            label->setText(QString::fromUtf8("✅ 已安装"));
            label->setStyleSheet("color: #16a34a; font-size: 12px; font-weight: bold;");
            if (browser_checks_.contains(name))
                browser_checks_[name]->setChecked(true);
        } else {
            label->setText(QString::fromUtf8("未安装"));
            label->setStyleSheet("color: #9ca3af; font-size: 12px;");
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// One-click install handler
// ---------------------------------------------------------------------------

void SettingsDialog::onInstallExtension()
{
    QStringList selected;
    for (auto it = browser_checks_.begin(); it != browser_checks_.end(); ++it) {
        if (it.value()->isChecked() && it.value()->isEnabled())
            selected.append(it.key());
    }

    if (selected.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Super Download"),
            QString::fromUtf8("请先勾选要安装扩展的浏览器。"));
        return;
    }

    // Step 1: Copy extension files to fixed directory
    QString extDir = getExtensionDir();
    if (!installExtensionFiles(extDir)) {
        QMessageBox::critical(this, QStringLiteral("Super Download"),
            QString::fromUtf8("复制扩展文件失败。\n\n"
                "请确保 browser_extension 文件夹存在于程序目录中。"));
        return;
    }

    // Step 2: Create desktop shortcuts + bat launchers
    QStringList succeeded, failed, manual;

    for (const auto& name : selected) {
        if (!browsers_.contains(name)) continue;
        const auto& info = browsers_[name];

        if (!info.isChromium) {
            manual.append(name);
            continue;
        }

        if (writeRegistryPolicy(info, extDir))
            succeeded.append(name);
        else
            failed.append(name);
    }

    // Step 3: Auto-launch — kill browser processes, then restart with extension
    QStringList launched;
    for (const auto& name : succeeded) {
        if (!browsers_.contains(name)) continue;
        const auto& info = browsers_[name];
        QString exeName = QFileInfo(info.exePath).fileName();

        // Kill existing browser processes
        QProcess::execute("taskkill", {"/f", "/im", exeName});
    }

    if (!succeeded.isEmpty()) {
        // Small delay to let processes fully exit, then launch
        // Use a single-shot timer so the UI stays responsive
        QStringList browsersToLaunch = succeeded;
        QString extDirCopy = extDir;
        QTimer::singleShot(1500, this, [this, browsersToLaunch, extDirCopy]() {
            QString nativeExtDir = QDir::toNativeSeparators(extDirCopy);
            for (const auto& name : browsersToLaunch) {
                if (!browsers_.contains(name)) continue;
                const auto& info = browsers_[name];
                QStringList args;
                args << QString("--load-extension=%1").arg(nativeExtDir);
                QProcess::startDetached(info.exePath, args);
            }
        });
    }

    // Build result message
    QString msg;
    if (!succeeded.isEmpty()) {
        msg += QString::fromUtf8("✅ 安装成功！正在重启浏览器...\n\n");
        for (const auto& s : succeeded)
            msg += QString::fromUtf8("    • %1\n").arg(s);
        msg += QString::fromUtf8("\n浏览器将自动重启并加载扩展。\n\n");
        msg += QString::fromUtf8("⚠️ 首次启动时浏览器会提示「开发者模式扩展」，请选择保留。\n");
    }
    if (!failed.isEmpty()) {
        msg += QString::fromUtf8("\n❌ 安装失败:\n");
        for (const auto& f : failed)
            msg += "    • " + f + "\n";
    }
    if (!manual.isEmpty()) {
        msg += QString::fromUtf8("\n📋 需要手动安装:\n");
        for (const auto& m : manual)
            msg += "    • " + m + "\n";
        msg += QString::fromUtf8("\n手动步骤:\n"
            "1. 打开浏览器扩展管理页面\n"
            "2. 开启「开发者模式」\n"
            "3. 点击「加载已解压的扩展程序」\n"
            "4. 选择: %1\n").arg(QDir::toNativeSeparators(extDir));
    }

    QMessageBox::information(this, QStringLiteral("Super Download"), msg);
    refreshInstallStatus();
}

// ---------------------------------------------------------------------------
// Existing slots
// ---------------------------------------------------------------------------

ManagerConfig SettingsDialog::result() const { return config_; }

void SettingsDialog::onBrowseSaveDir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8("选择默认下载目录"), save_dir_edit_->text());
    if (!dir.isEmpty())
        save_dir_edit_->setText(dir);
}

void SettingsDialog::onAccepted()
{
    config_.default_save_dir = save_dir_edit_->text().toStdString();
    config_.max_blocks_per_task = max_blocks_spin_->value();
    config_.max_concurrent_tasks = max_concurrent_spin_->value();
    config_.speed_limit = static_cast<int64_t>(speed_limit_spin_->value()) * 1024;
    saveToSettings();
    accept();
}

void SettingsDialog::loadFromSettings()
{
    QSettings s;
    save_dir_edit_->setText(
        s.value("settings/default_save_dir",
                QString::fromStdString(config_.default_save_dir)).toString());
    max_blocks_spin_->setValue(
        s.value("settings/max_blocks_per_task", config_.max_blocks_per_task).toInt());
    max_concurrent_spin_->setValue(
        s.value("settings/max_concurrent_tasks", config_.max_concurrent_tasks).toInt());
    int speed_kb = static_cast<int>(
        s.value("settings/speed_limit_kbps", config_.speed_limit / 1024).toLongLong());
    speed_limit_spin_->setValue(speed_kb);
    clipboard_check_->setChecked(
        s.value("settings/clipboard_monitor", true).toBool());
    auto_open_folder_check_->setChecked(
        s.value("settings/auto_open_folder", false).toBool());
    file_types_edit_->setPlainText(
        s.value("settings/file_types", kDefaultFileTypes).toString());
}

void SettingsDialog::saveToSettings()
{
    QSettings s;
    s.setValue("settings/default_save_dir",
              QString::fromStdString(config_.default_save_dir));
    s.setValue("settings/max_blocks_per_task", config_.max_blocks_per_task);
    s.setValue("settings/max_concurrent_tasks", config_.max_concurrent_tasks);
    s.setValue("settings/speed_limit_kbps",
              static_cast<qlonglong>(config_.speed_limit / 1024));
    s.setValue("settings/clipboard_monitor", clipboard_check_->isChecked());
    s.setValue("settings/auto_open_folder", auto_open_folder_check_->isChecked());
    s.setValue("settings/file_types", file_types_edit_->toPlainText().trimmed());

    // Handle autostart registry
#ifdef _WIN32
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    if (autostart_check_->isChecked()) {
        QString exePath = QDir::toNativeSeparators(QApplication::applicationFilePath());
        reg.setValue("SuperDownload", "\"" + exePath + "\" --minimized");
    } else {
        reg.remove("SuperDownload");
    }
#endif

    // Sync file types to browser extension
    updateExtensionFileTypes();
}

void SettingsDialog::updateExtensionFileTypes()
{
    // Write updated background.js with current file types to the installed extension dir
    QString extDir = getExtensionDir();
    QString bgPath = extDir + "/background.js";
    if (!QFileInfo::exists(bgPath)) return;

    QStringList types = fileTypes();

    // Build the JS Set literal
    QString jsSet;
    for (int i = 0; i < types.size(); ++i) {
        if (i > 0) jsSet += ", ";
        jsSet += "\"" + types[i] + "\"";
    }

    // Read current background.js and replace the INTERCEPT_EXTENSIONS set
    QFile file(bgPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    // Replace the Set contents between "new Set([" and "]);"
    QRegularExpression re("new Set\\(\\[([^\\]]*)\\]\\)");
    content.replace(re, "new Set([" + jsSet + "])");

    QFile out(bgPath);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        out.write(content.toUtf8());
        out.close();
    }
}
