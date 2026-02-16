#include "new_download_dialog.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QApplication>
#include <QClipboard>
#include <QSettings>
#include <QStandardPaths>

NewDownloadDialog::NewDownloadDialog(const QString& default_save_dir,
                                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("新建下载"));
    setMinimumWidth(520);

    // URL row
    auto* url_label = new QLabel(QString::fromUtf8("下载地址"), this);
    url_edit_ = new QLineEdit(this);
    url_edit_->setPlaceholderText(QString::fromUtf8("粘贴下载链接..."));

    // File type indicator
    auto* type_label = new QLabel(this);
    type_label->setStyleSheet("color: #9ca3af; font-size: 12px;");
    type_label->hide();

    connect(url_edit_, &QLineEdit::textChanged, this, [type_label](const QString& text) {
        QString url = text.trimmed();
        if (url.isEmpty()) { type_label->hide(); return; }
        // Extract extension from URL
        int qmark = url.indexOf('?');
        QString path = (qmark > 0) ? url.left(qmark) : url;
        int lastDot = path.lastIndexOf('.');
        int lastSlash = path.lastIndexOf('/');
        if (lastDot > lastSlash && lastDot > 0) {
            QString ext = path.mid(lastDot + 1).toLower();
            if (ext.length() <= 6) {
                // Guess type
                QString typeStr;
                if (QStringList{"mp4","mkv","avi","mov","wmv","flv","webm"}.contains(ext))
                    typeStr = QString::fromUtf8("🎬 视频文件 (.%1)").arg(ext);
                else if (QStringList{"mp3","flac","wav","aac","ogg","wma","m4a"}.contains(ext))
                    typeStr = QString::fromUtf8("🎵 音乐文件 (.%1)").arg(ext);
                else if (QStringList{"zip","rar","7z","tar","gz","bz2","xz","iso"}.contains(ext))
                    typeStr = QString::fromUtf8("📦 压缩文件 (.%1)").arg(ext);
                else if (QStringList{"pdf","doc","docx","xls","xlsx","ppt","pptx","txt"}.contains(ext))
                    typeStr = QString::fromUtf8("📄 文档 (.%1)").arg(ext);
                else if (QStringList{"exe","msi","deb","rpm","apk"}.contains(ext))
                    typeStr = QString::fromUtf8("⚙ 程序 (.%1)").arg(ext);
                else
                    typeStr = QString::fromUtf8("📎 .%1 文件").arg(ext);
                type_label->setText(typeStr);
                type_label->show();
                return;
            }
        }
        type_label->hide();
    });

    // Auto-paste clipboard URL
    QString clip = QApplication::clipboard()->text().trimmed();
    if (clip.startsWith("http://", Qt::CaseInsensitive) ||
        clip.startsWith("https://", Qt::CaseInsensitive)) {
        url_edit_->setText(clip);
        // Trigger type detection
        emit url_edit_->textChanged(clip);
    }

    // Save-path row
    auto* path_label = new QLabel(QString::fromUtf8("保存路径"), this);
    path_edit_ = new QLineEdit(this);
    if (!default_save_dir.isEmpty()) {
        path_edit_->setText(default_save_dir);
    } else {
        // Load from settings
        QSettings s;
        path_edit_->setText(s.value("settings/default_save_dir",
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString());
    }

    auto* browse_btn = new QPushButton(QString::fromUtf8("浏览"), this);
    connect(browse_btn, &QPushButton::clicked, this, &NewDownloadDialog::onBrowse);

    auto* path_row = new QHBoxLayout;
    path_row->setSpacing(8);
    path_row->addWidget(path_edit_, 1);
    path_row->addWidget(browse_btn);

    // OK / Cancel
    button_box_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    button_box_->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("开始下载"));
    button_box_->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));
    connect(button_box_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Layout
    auto* form = new QVBoxLayout(this);
    form->setContentsMargins(20, 20, 20, 20);
    form->setSpacing(8);
    form->addWidget(url_label);
    form->addWidget(url_edit_);
    form->addWidget(type_label);
    form->addSpacing(8);
    form->addWidget(path_label);
    form->addLayout(path_row);
    form->addSpacing(16);
    form->addWidget(button_box_);
}

QString NewDownloadDialog::getUrl() const { return url_edit_->text().trimmed(); }
QString NewDownloadDialog::getSavePath() const { return path_edit_->text().trimmed(); }
void NewDownloadDialog::setUrl(const QString& url) { url_edit_->setText(url); }

void NewDownloadDialog::onBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8("选择保存目录"), path_edit_->text());
    if (!dir.isEmpty())
        path_edit_->setText(dir);
}
