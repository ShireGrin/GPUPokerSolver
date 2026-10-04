#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QPlainTextEdit>
#include "include/runtime/qsolverjob.h"
#include "qstextedit.h"
#include "strategyexplorer.h"
#include "rangeselector.h"
#include <QMessageBox>
#include "boardselector.h"
#include "settingeditor.h"
#include "include/ui/rangeselectortablemodel.h"
#include "include/ui/rangeselectortabledelegate.h"
#include "include/globals.h"

namespace Ui {
class MainWindow;
}

class PreflopSolverWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    static QSTextEdit * s_textEdit;
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    Ui::MainWindow * getPriUi();
    QSTextEdit * getLogArea();
    QSTextEdit * get_logwindow();

private slots:
    void on_buttonSolve_clicked();
    void on_clearLogButtom_clicked();
    void on_buildTreeButtom_clicked();
    void on_actionjson_triggered();
    void on_actionimport_triggered();
    void import_from_file(QString from);
    void import_from_content(QString content);
    QString export_to_string();
    void on_actionexport_triggered();
    void on_actionSettings_triggered();
    void on_actionPreflopSolver_triggered();
    void on_actionclear_all_triggered();
    void on_ip_range(QString range_text);
    void on_copyButtom_clicked();
    void on_showResultButton_clicked();
    void on_stopSolvingButton_clicked();
    void on_ipRangeSelectButtom_clicked();
    void on_oopRangeSelectButtom_clicked();
    void on_estimateMemoryButtom_clicked();
    void on_selectBoardButton_clicked();
    void on_saveParameterButton_clicked();
    void on_loadParameterButton_clicked();
    void on_deleteParameterButton_clicked();
    void on_parametersTableView_doubleClicked(const QModelIndex& index);
    void solve_item_clicked(const QModelIndex&);
    void on_saveSolveButton_clicked();
    void on_loadSolveButton_clicked();
    void on_deleteSolveButton_clicked();
    void on_openSolvesFolderButton_clicked();

    void on_ipRangeText_textChanged();

    void on_oopRangeText_textChanged();
    void onSolverJobFinished();

private:
    void openStrategyExplorer(bool maximized);
    void clear_all_params();
    Ui::MainWindow *ui = NULL;
    QSolverJob* qSolverJob = NULL;
    class QSqlQueryModel * qParametersModel = NULL;
    class QSqlQueryModel * qSolvesModel = NULL;
    StrategyExplorer* strategyExplorer = NULL;
    RangeSelector* rangeSelector = NULL;
    boardselector* boardSelector = NULL;
    SettingEditor* settingEditor = NULL;
    RangeSelectorTableDelegate * ip_delegate;
    RangeSelectorTableDelegate * oop_delegate;
    RangeSelectorTableModel * ip_model;
    RangeSelectorTableModel * oop_model;
    PreflopSolverWindow* preflopSolverWindow = nullptr;
    int selectedSolver = 0;
    int m_currentParentSolveId = -1;
    int m_currentHandId = -1;
    bool m_hasNodeLocks = false;
};

#endif // MAINWINDOW_H
