#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QDialogButtonBox>

/// Dialog for creating a new download task.
/// Provides URL input and save-path selection.
class NewDownloadDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewDownloadDialog(const QString& default_save_dir = QString(),
                               QWidget* parent = nullptr);

    QString getUrl() const;
    QString getSavePath() const;
    void setUrl(const QString& url);

private slots:
    void onBrowse();

private:
    QLineEdit* url_edit_;
    QLineEdit* path_edit_;
    QDialogButtonBox* button_box_;
};
