#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_NugetManager.h"
#include <QProcess>

class NugetManager : public QMainWindow
{
    Q_OBJECT
    enum State
    {
        Default,
        BuildDebug,
        BuildRelease,
        BuildDir,
        CopyFiles,
        NugetPack,
        NugetPush
    };
    enum Action
    {
        Nuget
    };
public:
    NugetManager(QWidget* parent = Q_NULLPTR);
    ~NugetManager();


private:
    void showError(QString s);
    void restoreFile();
    QString getDependencies();
    bool createNuspecFile();
    bool createPropsFile();
    bool createDirTree();
    bool copyFiles();
    void printError(QString s);
    void updateSource();
    void setDefaultSource();
    void getPackageVersionMap();
    void updateNugetInfo();//更新源和包版本信息

    Ui::NugetManagerClass ui;
    QStringList projList;
    State state;
    QProcess* process;
    QString ID;
    QString version;
    QString desc;
    QString authors;
    QString slnPath;
    QString projName;
    QString projPath;
    QString vcxprojContent;
    bool bCopyReleasePdb;
    bool bStaticLib;
    QString configJson;
    QMap<QString, QString> sourceMap;
    QMap<QString/*source name*/, QMap<QString/*package name*/, QString/*version*/>> packageInfoMap; //某一源上的nuget包版本缓存
private slots:
    void onProjChanged();
    void on_pushButton_select_clicked();
    void on_pushButton_build_clicked();
    void on_pushButton_upload_clicked();
    void onProcessFinished(int, QProcess::ExitStatus);
    void onReadyRead();
    void onTriggered(QAction* action);
    void onGetPackageVersionFinished(int exitCode, QProcess::ExitStatus exitStatus);
};
