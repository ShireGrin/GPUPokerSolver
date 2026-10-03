#ifndef AUTOEVDIALOG_H
#define AUTOEVDIALOG_H

#include <QDialog>
#include <QProcess>

namespace Ui {
class AutoEvDialog;
}

class AutoEvDialog : public QDialog
{
    QObject *parent;
    Q_OBJECT

public:
    explicit AutoEvDialog(QWidget *parent = nullptr);
    ~AutoEvDialog();

private slots:
    void on_runButton_clicked();
    void handleProcessOutput();
    void handleProcessError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    Ui::AutoEvDialog *ui;
    QProcess *process;
};

#endif // AUTOEVDIALOG_H
