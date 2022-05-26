#include "NugetManager.h"
#include <QFileDialog>
#include <QErrorMessage>
#include <QRegExp>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include "NugetSettingDialog.h"
#include "Utilities.h"
#include <QJsonDocument>

NugetManager::NugetManager(QWidget* parent)
    : QMainWindow(parent), state(Default), process(new QProcess(this)), bCopyReleasePdb(true), bStaticLib(false)
{
    ui.setupUi(this);
    qsrand((quint32)time(0));//避免程序首次运行时start内CreateNamePipe失败
    //源
    updateNugetInfo();
    setDefaultSource();
    //菜单
    QMenu* menu = new QMenu(ui.menuBar);
    menu->setTitle("设置");
    ui.menuBar->addAction(menu->menuAction());
    QAction* action = new QAction(tr("nuget源设置"), this);
    action->setData((int)Nuget);
    menu->addAction(action);
    connect(ui.menuBar, &QMenuBar::triggered, this, &NugetManager::onTriggered);
    connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &NugetManager::onProcessFinished);
    connect(process, &QProcess::readyReadStandardOutput, this, &NugetManager::onReadyRead);
    connect(process, &QProcess::readyReadStandardError, this, &NugetManager::onReadyRead);
    connect(ui.comboBox_proj, &QComboBox::currentTextChanged, this, &NugetManager::onProjChanged);
    connect(ui.comboBox_source, &QComboBox::currentTextChanged, this, &NugetManager::onProjChanged);
}

NugetManager::~NugetManager()
{
    restoreFile();
    QJsonDocument doc = QJsonDocument::fromJson(configJson.toUtf8());

    if (doc.isNull())
    {
        return;
    }

    QJsonObject obj = doc.object();
    obj.insert("defaultSource", ui.comboBox_source->currentText());
    doc.setObject(obj);
    QFile file("config.json");

    if (!file.open(QIODevice::Text | QIODevice::WriteOnly))
    {
        return;
    }

    file.write(doc.toJson());
    file.close();
}

void NugetManager::onProjChanged()
{
    QString curProjName = ui.comboBox_proj->currentText();
    QString sourceName = ui.comboBox_source->currentText();

    if (curProjName.isEmpty())
    {
        return;
    }

    ui.lineEdit_id->setText(curProjName);
    QMap<QString, QString> packageVersionMap = packageInfoMap.value(sourceName);
    QString currentVersion = packageVersionMap.value(curProjName);

    if (currentVersion.isEmpty())
    {
        ui.lineEdit_version->setText("0.0.1");
        return;
    }

    QString newVersion = Utilities::getAddedVersion(currentVersion);
    ui.lineEdit_version->setText(newVersion);
}

void NugetManager::on_pushButton_select_clicked()
{
    //TODO !!!!变更日志
    //TODO 加载上次路径
    QString path = QFileDialog::getOpenFileName(this, tr("选择解决方案文件"), "", "Files (*.sln)");

    if (path.isEmpty())
    {
        return;
    }

    ui.lineEdit_path->setText(path);
    //TODO 自动刷新？
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        //TODO
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();
    QRegExp regExp(R"(= \"(.*)\", \"(.*\.vcxproj)\")");
    regExp.setMinimal(true);//Qt中设置正则非贪婪模式
    int pos = 0;
    ui.comboBox_proj->clear();

    while ((pos = regExp.indexIn(content, pos)) != -1)
    {
        int n = regExp.matchedLength();
        QString projName = regExp.cap(1);
        QString projPath = regExp.cap(2);
        projList << projPath;
        ui.comboBox_proj->addItem(regExp.cap(1), regExp.cap(2));
        pos += n;
    }
}

void NugetManager::showError(QString s)
{
    //QErrorMessage* dialog = new QErrorMessage(this);
    //dialog->setWindowTitle("错误");
    //dialog->showMessage(s);
    //state = Default;
    Utilities::showError(s, this);

    if (state >= BuildDebug)
        restoreFile();

    if (state <= NugetPack)
        state = Default;
}

void NugetManager::on_pushButton_build_clicked()
{
    ID = ui.lineEdit_id->text();
    version = ui.lineEdit_version->text();
    desc = ui.plainTextEdit_desc->toPlainText();
    authors = ui.lineEdit_author->text();
    slnPath = ui.lineEdit_path->text();
    projName = ui.comboBox_proj->currentText();
    projPath = QFileInfo(slnPath).absolutePath() + "\\" + ui.comboBox_proj->currentData().toString();
    projPath = QDir::fromNativeSeparators(projPath);

    //TODO 版本号格式校验？
    if (ID.isEmpty() || version.isEmpty() || desc.isEmpty() || authors.isEmpty())
    {
        showError("Nuget包信息字段均不能为空");
        return;
    }

    //设置构建目标
    QFile file(projPath);
    QString backFile = QString("%1/%2.bak").arg(QCoreApplication::applicationDirPath()).arg(QFileInfo(projPath).fileName());
    QFile::remove(backFile);

    if (!QFile::copy(projPath, backFile))
    {
        showError("无法备份项目vcproj文件");
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        showError(QString("无法打开%1文件").arg(projPath));
        return;
    }

    vcxprojContent = QString::fromUtf8(file.readAll());
    file.close();

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        showError(QString("无法打开%1文件").arg(projPath));
        return;
    }

    if (vcxprojContent.contains("<ConfigurationType>StaticLibrary</ConfigurationType>"))
    {
        bStaticLib = true;
    }

    QString tempContent = vcxprojContent;
    int pos = tempContent.lastIndexOf("</Project>");
    QString targetStr = QString(R"(  <PropertyGroup>
    <TargetName>$(ProjectName).%1</TargetName>
    <TargetFileName>$(ProjectName).%1.dll</TargetFileName>
    <TargetPath>$(TargetDir)\$(ProjectName).%1.dll</TargetPath>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <Link>
      <OutputFile>$(OutDir)\$(ProjectName).%1.dll</OutputFile>
    </Link>
    <ClCompile>
      <ProgramDataBaseFileName>$(OutDir)\$(ProjectName).%1.pdb</ProgramDataBaseFileName>
    </ClCompile>
  </ItemDefinitionGroup>)").arg(version);
    tempContent.insert(pos, targetStr);
    file.write(tempContent.toUtf8());
    file.close();
    //开始工作
    ui.textBrowser->insertPlainText("====== 开始构建Debug ======");
    state = BuildDebug;
    QStringList args = { slnPath, "/Build", "Debug|x64", "/Project", projName};
    process->start("devenv.com", args);//必须是com否则无法读取输出
}

void NugetManager::on_pushButton_upload_clicked()
{
    if (state >= NugetPack)
    {
        state = NugetPush;
        QString nupkgFilePath = QString("./nuget/%1.%2.nupkg").arg(projName).arg(version);
        QString address = ui.comboBox_source->currentData().toString();
        QStringList args = { "push", nupkgFilePath, "-Source", address, "-Apikey", "0226651E-19EE-4F93-A172-manteiadevelop" };
        process->start("nuget", args);
    }
    else
    {
        showError(tr("请先生成nuget包"));
    }
}

void NugetManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess* process = (QProcess*)(sender());
    qDebug() << exitCode << exitStatus;

    if (exitCode != 0 || exitStatus != QProcess::NormalExit)
    {
        if (state == BuildDebug || state == BuildRelease)
        {
            showError("构建项目出错");
        }

        //其他状态由textBrowser显示
        return;
    }

    if (state == BuildDebug)
    {
        ui.textBrowser->insertPlainText("====== 开始构建Release ======");
        state = BuildRelease;
        QStringList args = { slnPath, "/Build", "Release|x64", "/Project", projName };
        process->start("devenv.com", args);
    }
    else if (state == BuildRelease)
    {
        restoreFile();
        state = BuildDir;

        if (!createDirTree())
        {
            showError(tr("创建目录树失败"));
            return;
        }

        state = CopyFiles;

        if (!copyFiles())
        {
            showError(tr("拷贝项目生成文件失败"));
            return;
        }

        if (!createNuspecFile())
        {
            showError(tr("创建nuspec文件失败"));
            return;
        }

        if (!createPropsFile())
        {
            showError(tr("创建props文件失败"));
            return;
        }

        state = NugetPack;
        QStringList args = {"pack", QString("./nuget/%2.nuspec").arg(projName), "-OutputDirectory", "./nuget"};
        process->start("nuget", args);
    }
    else if (state == NugetPack)
    {
        QString nupkgFilePath = QString("./nuget/%1.%2.nupkg").arg(projName).arg(version);
        QFile nupkgFile(nupkgFilePath);

        if (!nupkgFile.exists())
        {
            showError(tr("打包失败"));
            return;
        }

        //TODO 判断并提示成功
        QMessageBox* msgBox = new QMessageBox(this);
        msgBox->setText("成功生成nuget包");
        msgBox->show();
    }
    else if (state == NugetPush)
    {
        //TODO 判断并提示成功
        QMessageBox* msgBox = new QMessageBox(this);
        msgBox->setText("上传成功");
        msgBox->show();
        QStringList args = process->arguments();
        QString address = args[3];//push的第4个参数是地址

        for (const auto& name : sourceMap.keys())
        {
            if (sourceMap[name] == address)
            {
                QString newVersion = Utilities::getAddedVersion(ui.lineEdit_version->text());
                ui.lineEdit_version->setText(newVersion);
                packageInfoMap[name][projName] = newVersion;
            }
        }
    }
}

void NugetManager::onReadyRead()
{
    QString line = QString::fromLocal8Bit(process->readAllStandardOutput());
    ui.textBrowser->insertPlainText(line);//此接口不自动换行
    printError(QString::fromLocal8Bit(process->readAllStandardError()));
    ui.textBrowser->moveCursor(QTextCursor::End);
}

void NugetManager::printError(QString s)
{
    if (s.isEmpty())
    {
        return;
    }

    ui.textBrowser->insertHtml(QString(R"(<font color="red">%1</font><br>)").arg(s));
}

void NugetManager::restoreFile()
{
    QFile file(projPath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        printError(QString("无法打开%1文件").arg(projPath));
        return;
    }

    file.write(vcxprojContent.toUtf8());
    file.close();
}

QString NugetManager::getDependencies()
{
    QString s;
    QRegExp regExp(R"(id=\"(.*)\" version=\"(.*)\")");
    regExp.setMinimal(true);//Qt中设置正则非贪婪模式
    int pos = 0;
    QString path = QString("%1/packages.config").arg(QFileInfo(projPath).absolutePath());
    QFile packageConfig(path);

    if (!packageConfig.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        //TODO
        return "";
    }

    QString content = QString::fromUtf8(packageConfig.readAll());
    packageConfig.close();

    while ((pos = regExp.indexIn(content, pos)) != -1)
    {
        int n = regExp.matchedLength();
        QString id = regExp.cap(1);
        QString version = regExp.cap(2);
        s.append(QString("<dependency id=\"%1\" version=\"%2\"/>\n").arg(id).arg(version));
        pos += n;
    }

    s = s.trimmed();
    return s;
}

bool NugetManager::createNuspecFile()
{
    QFile file(QString("./nuget/%1.nuspec").arg(projName));
    QString dependencies = getDependencies();
    QString content = QString(R"(<?xml version="1.0"?>
<package>
    <metadata>
        <id>%1</id>
        <version>%2</version>
        <description>%3</description>
        <authors>%4</authors>
		<dependencies>
			%5
        </dependencies>
    </metadata>
    <files>
        <file src="build\native\lib\**\*.*"       target="build\native\lib"     />
        <file src="build\native\include\**\*.*"   target="build\native\include" />
        <file src="build\native\bin\**\*.*"            target="build\native\bin"     />
        <file src="build\native\%6.props"		  target="build\native" />
    </files>
</package>)").arg(ID).arg(version).arg(desc).arg(authors).arg(dependencies).arg(projName);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        showError(QString("创建%1失败").arg(file.fileName()));
        return false;
    }

    file.write(content.toUtf8());
    file.close();
    return true;
}

bool NugetManager::createPropsFile()
{
    QFile file(QString("./nuget/build/native/%1.props").arg(projName));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        showError(QString("创建%1失败").arg(file.fileName()));
        return false;
    }

    QString targetStr = QString("%1.%2").arg(projName).arg(version);
    targetStr = targetStr.replace(".", "_");
    QString copyReleasePdbStr, debugCopyDllStr, releaseCopyDllStr;

    if (bCopyReleasePdb)
    {
        copyReleasePdbStr = QString(R"(<Copy SourceFiles="$(MSBuildThisFileDirectory)bin\x64\release\%1.%2.pdb" DestinationFiles="$(TargetDir)\%1.%2.pdb" SkipUnchangedFiles="true" />)").arg(projName).arg(version);
    }

    if (!bStaticLib)
    {
        debugCopyDllStr = QString(R"(<Copy SourceFiles="$(MSBuildThisFileDirectory)bin\x64\debug\%1.%2.dll" DestinationFiles="$(TargetDir)\%1.%2.dll" SkipUnchangedFiles="true" />)").arg(projName).arg(version);
        releaseCopyDllStr = QString(R"(<Copy SourceFiles="$(MSBuildThisFileDirectory)bin\x64\Release\%1.%2.dll" DestinationFiles="$(TargetDir)\%1.%2.dll" SkipUnchangedFiles="true" />)").arg(projName).arg(version);
    }

    QString content = QString(R"(<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <!-- x64 debug -->
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)' == 'Debug|x64'">
    <ClCompile>
      <PreprocessorDefinitions>%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>$(MSBuildThisFileDirectory)include\;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <AdditionalDependencies>$(MSBuildThisFileDirectory)lib\x64\debug\%1.%2.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
  <!-- x64 release -->
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)' == 'Release|x64'">
    <ClCompile>
      <PreprocessorDefinitions>%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>$(MSBuildThisFileDirectory)include\;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <AdditionalDependencies>$(MSBuildThisFileDirectory)lib\x64\release\%1.%2.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
  <Target Name="%3" AfterTargets="AfterBuild" />
  <Target Name="%3_cmd1" AfterTargets="%3" Condition="'$(Configuration)|$(Platform)' == 'Debug|x64'">
    %5
    <Copy SourceFiles="$(MSBuildThisFileDirectory)bin\x64\debug\%1.%2.pdb" DestinationFiles="$(TargetDir)\%1.%2.pdb" SkipUnchangedFiles="true" />
  </Target>
  <Target Name="%3_cmd2" AfterTargets="%3" Condition="'$(Configuration)|$(Platform)' == 'Release|x64'">
	%6
    %4
  </Target>
</Project>)").arg(projName).arg(version).arg(targetStr).arg(copyReleasePdbStr).arg(debugCopyDllStr).arg(releaseCopyDllStr);
    file.write(content.toUtf8());
    file.close();
    return true;
}

//删除文件夹
bool delDir(const QString& path)
{
    if (path.isEmpty())
    {
        return false;
    }

    QDir dir(path);

    if (!dir.exists())
    {
        return true;
    }

    dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot); //设置过滤
    QFileInfoList fileList = dir.entryInfoList(); // 获取所有的文件信息

    for (auto file : fileList) //遍历文件信息
    {
        if (file.isFile())   // 是文件，删除
        {
            file.dir().remove(file.fileName());
        }
        else   // 递归删除
        {
            delDir(file.absoluteFilePath());
        }
    }

    return dir.rmdir(dir.absolutePath()); // 删除文件夹
}

bool NugetManager::createDirTree()
{
    delDir("./nuget");
    QEventLoop loop;
    QTimer::singleShot(1000, &loop, SLOT(quit()));//太快可能无法成功创建文件夹
    loop.exec();
    QDir dir;
    //TODO 失败判断
    dir.mkpath("./nuget/build/native/bin/x64/Release");
    dir.mkpath("./nuget/build/native/bin/x64/Debug");
    dir.mkpath("./nuget/build/native/lib/x64/Release");
    dir.mkpath("./nuget/build/native/lib/x64/Debug");
    dir.mkpath("./nuget/build/native/include");
    return true;
}

//source源文件目录路径，destination目的文件目录，override文件存在是否覆盖
bool copyDir(const QString& source, const QString& destination)
{
    QDir directory(source);

    if (!directory.exists())
    {
        return false;
    }

    QString srcPath = QDir::toNativeSeparators(source);

    if (!srcPath.endsWith(QDir::separator()))
        srcPath += QDir::separator();

    QString dstPath = QDir::toNativeSeparators(destination);

    if (!dstPath.endsWith(QDir::separator()))
        dstPath += QDir::separator();

    bool error = false;
    QStringList fileNames = directory.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    for (int i = 0; i != fileNames.size(); ++i)
    {
        QString fileName = fileNames.at(i);
        QString srcFilePath = srcPath + fileName;
        QString dstFilePath = dstPath + fileName;
        QFileInfo fileInfo(srcFilePath);

        if (fileInfo.isFile())
        {
            QFile::copy(srcFilePath, dstFilePath);
        }
        else if (fileInfo.isDir())
        {
            QDir dstDir(dstFilePath);
            dstDir.mkpath(dstFilePath);

            if (!copyDir(srcFilePath, dstFilePath))
            {
                error = true;
            }
        }
    }

    return !error;
}


bool NugetManager::copyFiles()
{
    bCopyReleasePdb = true;
    //TODO 代码优化
    QList <QPair<QString, QString>> copyList;
    QString srcBase = QFileInfo(slnPath).absolutePath();
    QString dstBase = "./nuget/build/native";
    QString name = QString("%1.%2").arg(projName).arg(version);
    copyList << QPair<QString, QString>(QString("%1/x64/Release/%2.dll").arg(srcBase).arg(name),
                                        QString("%1/bin/x64/Release/%2.dll").arg(dstBase).arg(name));
    copyList << QPair<QString, QString>(QString("%1/x64/Release/%2.lib").arg(srcBase).arg(name),
                                        QString("%1/lib/x64/Release/%2.lib").arg(dstBase).arg(name));
    copyList << QPair<QString, QString>(QString("%1/x64/Release/%2.pdb").arg(srcBase).arg(name),
                                        QString("%1/bin/x64/Release/%2.pdb").arg(dstBase).arg(name));
    copyList << QPair<QString, QString>(QString("%1/x64/Debug/%2.dll").arg(srcBase).arg(name),
                                        QString("%1/bin/x64/Debug/%2.dll").arg(dstBase).arg(name));
    copyList << QPair<QString, QString>(QString("%1/x64/Debug/%2.lib").arg(srcBase).arg(name),
                                        QString("%1/lib/x64/Debug/%2.lib").arg(dstBase).arg(name));
    copyList << QPair<QString, QString>(QString("%1/x64/Debug/%2.pdb").arg(srcBase).arg(name),
                                        QString("%1/bin/x64/Debug/%2.pdb").arg(dstBase).arg(name));

    for (const auto& copyInfo : copyList)
    {
        if (!QFile::copy(copyInfo.first, copyInfo.second))
        {
            if (copyInfo.first.contains("Release") && copyInfo.first.contains(".pdb"))
            {
                bCopyReleasePdb = false;
            }

            ui.textBrowser->insertPlainText(QString(tr("拷贝%1到%2时出错")).arg(copyInfo.first).arg(copyInfo.second));
        }
    }

    QString srcDir = QString("%1/%2/include").arg(srcBase).arg(projName);
    QString dstDir = QString("%1/include").arg(dstBase).arg(projName);
    copyDir(srcDir, dstDir);
    return true;
}

void NugetManager::onTriggered(QAction* action)
{
    Action action_type = (Action)action->data().value<int>();

    if (action_type == Nuget)
    {
        NugetSettingDialog dialog;
        dialog.exec();
        updateNugetInfo();
    }
}

void NugetManager::updateNugetInfo()
{
    updateSource();
    getPackageVersionMap();
}

void NugetManager::updateSource()
{
    sourceMap = Utilities::getSourceMap();
    ui.comboBox_source->clear();

    for (const auto& name : sourceMap.keys())
    {
        ui.comboBox_source->addItem(name, sourceMap[name]);
    }
}

void NugetManager::setDefaultSource()
{
    configJson = Utilities::getSourceJson();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(configJson.toUtf8(), &error);

    if (!configJson.isEmpty() && doc.isNull() && error.error != QJsonParseError::NoError)
    {
        showError(tr("解析config.json错误"));
        return;
    }

    QString defaultSource = doc.object()["defaultSource"].toString();
    ui.comboBox_source->setCurrentText(defaultSource);
}

void NugetManager::getPackageVersionMap()
{
    for (const auto& name : sourceMap.keys())
    {
        QString address = sourceMap[name];
        QProcess* process = new QProcess;
        connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &NugetManager::onGetPackageVersionFinished);
        QStringList args = { "list", "-Source", address};
        process->start("nuget", args);
    }
}

void NugetManager::onGetPackageVersionFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus != QProcess::NormalExit)
    {
        printError("获取源nuget包列表失败");
        return;
    }

    QProcess* process = static_cast<QProcess*>(sender());
    process->deleteLater();
    QString s = QString::fromLocal8Bit(process->readAll());
    QStringList args = process->arguments();
    QMap<QString, QString> packageVersionMap;
    QRegExp regExp(R"(([\w\.]+) ([\d\.]+)\r\n)");
    regExp.setMinimal(true);//Qt中设置正则非贪婪模式
    int pos = 0;

    while ((pos = regExp.indexIn(s, pos)) != -1)
    {
        int n = regExp.matchedLength();
        QString name = regExp.cap(1);
        QString version = regExp.cap(2);
        packageVersionMap[name] = version;
        pos += n;
    }

    for (const auto& name : sourceMap.keys())
    {
        if (args.contains(sourceMap[name]))
        {
            packageInfoMap[name] = packageVersionMap;
            break;
        }
    }
}