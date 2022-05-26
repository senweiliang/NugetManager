#pragma once
#include <QString>
#include <QMap>
#include <QWidget>

class Utilities
{
public:
    static QMap<QString, QString> getSourceMap();
    static QString getSourceJson();
    static void showError(QString s, QWidget* parent = nullptr);
    static QString getAddedVersion(QString currentVersion);
};

