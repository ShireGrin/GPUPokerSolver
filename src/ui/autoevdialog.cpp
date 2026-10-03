#include "include/ui/autoevdialog.h"
#include "ui_autoevdialog.h"
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>

AutoEvDialog::AutoEvDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AutoEvDialog),
    process(new QProcess(this))
{
    ui->setupUi(this);

    connect(ui->runButton, &QPushButton::clicked, this, &AutoEvDialog::on_runButton_clicked);
    connect(process, &QProcess::readyReadStandardOutput, this, &AutoEvDialog::handleProcessOutput);
    connect(process, &QProcess::readyReadStandardError, this, &AutoEvDialog::handleProcessError);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &AutoEvDialog::handleProcessFinished);
}

AutoEvDialog::~AutoEvDialog()
{
    if (process->state() == QProcess::Running) {
        process->kill();
        process->waitForFinished();
    }
    delete ui;
}

void AutoEvDialog::on_runButton_clicked()
{
    if (process->state() == QProcess::Running) {
        QMessageBox::warning(this, "Running", "Analysis is already running.");
        return;
    }

    QString playerName = ui->playerNameEdit->text().trimmed();
    int limit = ui->limitSpinBox->value();

    if (playerName.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please enter a Hero name.");
        return;
    }

    ui->logOutputEdit->clear();
    ui->logOutputEdit->append(QString("Starting Auto EV Analysis for %1 (Limit: %2)...\n").arg(playerName).arg(limit));
    ui->runButton->setEnabled(false);

    QString scriptDir = QCoreApplication::applicationDirPath() + "/../scripts";
    QString pythonExe = scriptDir + "/venv/bin/python";
    QString scriptPath = scriptDir + "/auto_ev.py";

    // Fallback to global python3 if venv python doesn't exist
    if (!QFile::exists(pythonExe)) {
        pythonExe = "python3";
    }

    QStringList arguments;
    arguments << scriptPath << playerName << "--limit" << QString::number(limit);

    process->setWorkingDirectory(scriptDir);
    process->start(pythonExe, arguments);

    if (!process->waitForStarted()) {
        ui->logOutputEdit->append("Failed to start process.");
        ui->runButton->setEnabled(true);
    }
}

void AutoEvDialog::handleProcessOutput()
{
    QByteArray output = process->readAllStandardOutput();
    ui->logOutputEdit->append(QString::fromUtf8(output).trimmed());
}

void AutoEvDialog::handleProcessError()
{
    QByteArray error = process->readAllStandardError();
    ui->logOutputEdit->append(QString::fromUtf8(error).trimmed());
}

void AutoEvDialog::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    ui->runButton->setEnabled(true);
    if (exitStatus == QProcess::CrashExit) {
        ui->logOutputEdit->append("\nProcess crashed.");
    } else {
        ui->logOutputEdit->append(QString("\nProcess finished with exit code %1.").arg(exitCode));
    }
}
