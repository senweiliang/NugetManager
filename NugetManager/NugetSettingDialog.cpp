#include "NugetSettingDialog.h"
#include <QFile>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "Utilities.h"

NugetSettingDialog::NugetSettingDialog(QWidget* parent)
    : QDialog(parent)
{
    ui.setupUi(this);
    init();
    connect(this, &QDialog::rejected, this, &NugetSettingDialog::onClose);
}

NugetSettingDialog::~NugetSettingDialog()
{
}

void NugetSettingDialog::init()
{
    ui.tableWidget->setColumnCount(2);
    ui.tableWidget->setColumnWidth(0, 100);
    ui.tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui.tableWidget->setHorizontalHeaderLabels(QStringList() << tr("名称") << tr("源地址"));
    QString json = Utilities::getSourceJson();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);

    if (!json.isEmpty() && doc.isNull())
    {
        Utilities::showError(QString(tr("解析错误：%1")).arg(error.errorString()), this);
        return;
    }

    QJsonArray sourceArray = doc.object()["source"].toArray();
    int row = ui.tableWidget->rowCount();

    for (auto sourceRef : sourceArray)
    {
        QJsonObject obj = sourceRef.toObject();
        QString name = obj["name"].toString();
        QString address = obj["address"].toString();
        ui.tableWidget->insertRow(row);
        ui.tableWidget->setItem(row, 0, new QTableWidgetItem(name));
        ui.tableWidget->setItem(row, 1, new QTableWidgetItem(address));
        row++;
    }
}

void NugetSettingDialog::onClose()
{
    int row = ui.tableWidget->rowCount();
    QJsonArray sourceArray;

    for (int i = 0; i < row; i++)
    {
        QTableWidgetItem* itemName = ui.tableWidget->item(i, 0);

        if (itemName == nullptr)
        {
            continue;
        }

        QTableWidgetItem* itemAddress = ui.tableWidget->item(i, 1);

        if (itemAddress == nullptr)
        {
            continue;
        }

        QString name = itemName->text();
        QString address = itemAddress->text();

        if (name.isEmpty() || address.isEmpty())
        {
            continue;
        }

        sourceMap[name] = address;
        QJsonObject obj;
        obj.insert("name", name);
        obj.insert("address", address);
        sourceArray.append(obj);
    }

    configObj.insert("source", sourceArray);
    QJsonDocument doc(configObj);
    QFile file("config.json");

    if (!file.open(QIODevice::Text | QIODevice::WriteOnly))
    {
        Utilities::showError(tr("保存config.json失败"), this);
        return;
    }

    file.write(doc.toJson());
    file.close();
}

void NugetSettingDialog::on_pushButton_add_clicked()
{
    int row = ui.tableWidget->rowCount();
    ui.tableWidget->insertRow(row);
}

void NugetSettingDialog::on_pushButton_delete_clicked()
{
    int row = ui.tableWidget->currentRow();
    ui.tableWidget->removeRow(row);
}