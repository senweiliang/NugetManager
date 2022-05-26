#pragma once

#include <QDialog>
#include "ui_NugetSettingDialog.h"
#include <QMap>
#include <QJsonObject>

class NugetSettingDialog : public QDialog
{
    Q_OBJECT

public:
    NugetSettingDialog(QWidget* parent = Q_NULLPTR);
    ~NugetSettingDialog();

    void init();

private slots:
    void onClose();
    void on_pushButton_add_clicked();
    void on_pushButton_delete_clicked();

private:
    Ui::NugetSettingDialog ui;
    QMap<QString, QString> sourceMap;
    QJsonObject configObj;
};
