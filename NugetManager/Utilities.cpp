#include "Utilities.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>

QMap<QString, QString> Utilities::getSourceMap()
{
    QString json = getSourceJson();
    QMap<QString, QString> sourceMap;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);

    if (!json.isEmpty() && doc.isNull())
    {
        showError(QString(QObject::tr("解析错误：%1")).arg(error.errorString()));
        return sourceMap;
    }

    QJsonArray sourceArray = doc.object()["source"].toArray();

    for (auto sourceRef : sourceArray)
    {
        QJsonObject obj = sourceRef.toObject();
        QString name = obj["name"].toString();
        QString address = obj["address"].toString();
        sourceMap[name] = address;
    }

    return sourceMap;
}

QString Utilities::getSourceJson()
{
    QFile configFile("config.json");
    QString json;

    if (configFile.exists())
    {
        if (!configFile.open(QIODevice::Text | QIODevice::ReadOnly))
        {
            showError(QObject::tr("无法打开配置文件config.json"));
            return json;
        }

        json = QString::fromLocal8Bit(configFile.readAll());
        configFile.close();
    }
    else
    {
        if (!configFile.open(QIODevice::Text | QIODevice::WriteOnly))
        {
            showError(QObject::tr("无法创建配置文件config.json"));
            return json;
        }

        configFile.close();
    }

    return json;
}

void Utilities::showError(QString s, QWidget* parent)
{
    QMessageBox* msgBox = new QMessageBox(parent);
    msgBox->setText(s);
    msgBox->show();
}

QString Utilities::getAddedVersion(QString oldVersion)
{
    QStringList versionNumList = oldVersion.split(".", QString::SkipEmptyParts);

    if (versionNumList.isEmpty())
    {
        return QString();
    }

    QString oldNumStr = versionNumList.last();
    QString newNumStr = QString::number(oldNumStr.toUInt() + 1);
    versionNumList.replace(versionNumList.count() - 1, newNumStr);
    QString newVersion = versionNumList.join(".");
    return newVersion;
}