#pragma once
#include <QString>

/// Returns the global light-theme stylesheet for the application.
inline QString appStyleSheet() {
    return QStringLiteral(R"(
/* ── Global ─────────────────────────────────────────────── */
* {
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 13px;
}

QMainWindow, QDialog {
    background-color: #ffffff;
    color: #1a1a2e;
}

/* ── Top bar (toolbar) ──────────────────────────────────── */
QToolBar {
    background-color: #f7f7f8;
    border: none;
    border-bottom: 1px solid #e5e5e5;
    padding: 6px 10px;
    spacing: 6px;
}

QToolBar QToolButton {
    background-color: transparent;
    color: #333333;
    border: 1px solid #e0e0e0;
    border-radius: 6px;
    padding: 7px 16px;
    font-weight: 500;
}

QToolBar QToolButton:hover {
    background-color: #f0f0f0;
    border-color: #d0d0d0;
}

QToolBar QToolButton:pressed {
    background-color: #e4e4e4;
}

QToolBar QToolButton#btn_new {
    background-color: #2563eb;
    color: #ffffff;
    border: none;
    font-weight: 600;
}

QToolBar QToolButton#btn_new:hover {
    background-color: #1d4ed8;
}

QToolBar QToolButton#btn_delete {
    color: #dc2626;
    border-color: #fecaca;
}

QToolBar QToolButton#btn_delete:hover {
    background-color: #fef2f2;
    border-color: #f87171;
}

QToolBar::separator {
    width: 1px;
    background-color: #e5e5e5;
    margin: 4px 6px;
}

/* ── Table view ─────────────────────────────────────────── */
QTableView {
    background-color: #ffffff;
    alternate-background-color: #fafafa;
    color: #1a1a2e;
    border: none;
    gridline-color: transparent;
    selection-background-color: #e8f0fe;
    selection-color: #1a1a2e;
    outline: none;
}

QTableView::item {
    padding: 8px 12px;
    border-bottom: 1px solid #f0f0f0;
}

QTableView::item:selected {
    background-color: #dbeafe;
    color: #1e40af;
}

QHeaderView::section {
    background-color: #f7f7f8;
    color: #6b7280;
    border: none;
    border-bottom: 2px solid #e5e5e5;
    padding: 10px 12px;
    font-weight: 600;
    font-size: 12px;
    text-transform: uppercase;
}

QHeaderView::section:hover {
    color: #1a1a2e;
}

/* ── Scrollbar ──────────────────────────────────────────── */
QScrollBar:vertical {
    background-color: transparent;
    width: 8px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background-color: #d1d5db;
    border-radius: 4px;
    min-height: 30px;
}

QScrollBar::handle:vertical:hover {
    background-color: #9ca3af;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: none;
    height: 0;
}

QScrollBar:horizontal {
    background-color: transparent;
    height: 8px;
}

QScrollBar::handle:horizontal {
    background-color: #d1d5db;
    border-radius: 4px;
    min-width: 30px;
}

/* ── Status bar ─────────────────────────────────────────── */
QStatusBar {
    background-color: #f7f7f8;
    color: #6b7280;
    border-top: 1px solid #e5e5e5;
    padding: 4px 12px;
    font-size: 12px;
}

QStatusBar QLabel {
    color: #6b7280;
    padding: 0 8px;
}

/* ── Dialogs ────────────────────────────────────────────── */
QDialog {
    background-color: #ffffff;
}

QLabel {
    color: #1a1a2e;
}

QLineEdit {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 8px 12px;
    selection-background-color: #2563eb;
    selection-color: #ffffff;
}

QLineEdit:focus {
    border-color: #2563eb;
}

QLineEdit::placeholder {
    color: #9ca3af;
}

QSpinBox {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 6px 10px;
}

QSpinBox:focus {
    border-color: #2563eb;
}

QSpinBox::up-button, QSpinBox::down-button {
    background-color: #f0f0f0;
    border: none;
    width: 20px;
    border-radius: 3px;
}

QSpinBox::up-button:hover, QSpinBox::down-button:hover {
    background-color: #e0e0e0;
}

QPushButton {
    background-color: #ffffff;
    color: #333333;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 8px 20px;
    font-weight: 500;
}

QPushButton:hover {
    background-color: #f5f5f5;
    border-color: #bbb;
}

QPushButton:pressed {
    background-color: #ebebeb;
}

QPushButton:default, QPushButton#btn_ok {
    background-color: #2563eb;
    color: #ffffff;
    border: none;
    font-weight: 600;
}

QPushButton:default:hover, QPushButton#btn_ok:hover {
    background-color: #1d4ed8;
}

QDialogButtonBox QPushButton {
    min-width: 80px;
}

/* ── TextEdit (log viewer) ──────────────────────────────── */
QTextEdit {
    background-color: #f9fafb;
    color: #166534;
    border: 1px solid #e5e5e5;
    border-radius: 6px;
    padding: 8px;
    font-family: "Cascadia Code", "Consolas", monospace;
    font-size: 12px;
}

/* ── MessageBox ─────────────────────────────────────────── */
QMessageBox {
    background-color: #ffffff;
}

QMessageBox QLabel {
    color: #1a1a2e;
    font-size: 13px;
}

/* ── Tooltip ────────────────────────────────────────────── */
QToolTip {
    background-color: #1a1a2e;
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 4px 8px;
}

/* ── Menu (context / tray) ──────────────────────────────── */
QMenu {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #e5e5e5;
    border-radius: 8px;
    padding: 4px;
}

QMenu::item {
    padding: 8px 24px;
    border-radius: 4px;
}

QMenu::item:selected {
    background-color: #e8f0fe;
}

QMenu::item:disabled {
    color: #9ca3af;
}

QMenu::separator {
    height: 1px;
    background-color: #f0f0f0;
    margin: 4px 8px;
}

/* ── FormLayout labels ──────────────────────────────────── */
QFormLayout QLabel {
    font-weight: 500;
    color: #6b7280;
    padding-right: 8px;
}

/* ── Sidebar (QTreeWidget) ──────────────────────────────── */
QTreeWidget {
    background-color: #f9fafb;
    border: none;
    border-right: 1px solid #e5e5e5;
    outline: none;
    font-size: 13px;
}

QTreeWidget::item {
    padding: 6px 8px;
    border-radius: 4px;
    margin: 1px 4px;
}

QTreeWidget::item:selected {
    background-color: #e8f0fe;
    color: #1a1a2e;
}

QTreeWidget::item:hover:!selected {
    background-color: #f0f0f0;
}

QTreeWidget::branch {
    background-color: transparent;
}

QTreeWidget QHeaderView::section {
    background-color: #f9fafb;
    color: #6b7280;
    border: none;
    border-bottom: 1px solid #e5e5e5;
    padding: 8px;
    font-weight: 600;
    font-size: 12px;
}

/* ── TabWidget ──────────────────────────────────────────── */
QTabWidget::pane {
    border: 1px solid #e5e5e5;
    border-radius: 6px;
    background-color: #ffffff;
}

QTabBar::tab {
    background-color: #f7f7f8;
    color: #6b7280;
    border: 1px solid #e5e5e5;
    border-bottom: none;
    padding: 8px 20px;
    margin-right: 2px;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}

QTabBar::tab:selected {
    background-color: #ffffff;
    color: #1a1a2e;
    font-weight: 600;
    border-bottom: 2px solid #2563eb;
}

QTabBar::tab:hover:!selected {
    background-color: #f0f0f0;
    color: #333333;
}

/* ── GroupBox ───────────────────────────────────────────── */
QGroupBox {
    border: 1px solid #e5e5e5;
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 16px;
    font-weight: 600;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 8px;
    color: #333333;
}

/* ── CheckBox ──────────────────────────────────────────── */
QCheckBox {
    spacing: 8px;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 2px solid #d1d5db;
    border-radius: 3px;
    background-color: #ffffff;
}

QCheckBox::indicator:checked {
    background-color: #2563eb;
    border-color: #2563eb;
}

QCheckBox::indicator:hover {
    border-color: #2563eb;
}

/* ── PlainTextEdit ─────────────────────────────────────── */
QPlainTextEdit {
    background-color: #f9fafb;
    color: #1a1a2e;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 8px;
    font-family: "Cascadia Code", "Consolas", monospace;
    font-size: 12px;
}

QPlainTextEdit:focus {
    border-color: #2563eb;
}

/* ── DateTimeEdit ──────────────────────────────────────── */
QDateTimeEdit {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 6px 10px;
}

QDateTimeEdit:focus {
    border-color: #2563eb;
}

QDateTimeEdit::drop-down {
    background-color: #f0f0f0;
    border: none;
    width: 24px;
    border-top-right-radius: 6px;
    border-bottom-right-radius: 6px;
}

QDateTimeEdit::drop-down:hover {
    background-color: #e0e0e0;
}

/* ── ComboBox ──────────────────────────────────────────── */
QComboBox {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 6px 10px;
}

QComboBox:focus {
    border-color: #2563eb;
}

QComboBox::drop-down {
    background-color: #f0f0f0;
    border: none;
    width: 24px;
    border-top-right-radius: 6px;
    border-bottom-right-radius: 6px;
}

QComboBox QAbstractItemView {
    background-color: #ffffff;
    border: 1px solid #e5e5e5;
    border-radius: 6px;
    selection-background-color: #e8f0fe;
    selection-color: #1a1a2e;
    outline: none;
}

/* ── ProgressBar (fallback) ────────────────────────────── */
QProgressBar {
    background-color: #e5e7eb;
    border: none;
    border-radius: 8px;
    text-align: center;
    font-size: 11px;
    color: #333333;
}

QProgressBar::chunk {
    background-color: #2563eb;
    border-radius: 8px;
}
)");
}
