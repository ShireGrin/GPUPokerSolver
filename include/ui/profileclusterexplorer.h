#ifndef PROFILECLUSTEREXPLORER_H
#define PROFILECLUSTEREXPLORER_H

#include <QDialog>
#include <QSqlDatabase>
#include <QTableWidget>
#include <QSlider>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include "include/ui/htmltablerangeview.h"
#include "include/ui/rangeselectortablemodel.h"
#include "include/ui/rangeselectortabledelegate.h"

class ProfileClusterExplorer : public QDialog
{
    Q_OBJECT

public:
    explicit ProfileClusterExplorer(QWidget *parent = nullptr);
    ~ProfileClusterExplorer();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onSelectionChanged();
    void onSaveClicked();
    void onRangeChanged(int, int);

private:
    void setupDatabase();
    void loadDropdowns();
    void loadRange();

    QSqlDatabase db;
    
    QTableWidget *clusterTable;
    QTableWidget *scenarioTable;
    QTableWidget *positionTable;
    QTableWidget *stackTable;
    
    QSlider *weightSlider;
    QLabel *weightLabel;
    QLabel *totalHandsLabel;
    
    QPushButton *saveButton;
    QCheckBox *enablePaintBox;
    QCheckBox *cleanRangeBox;
    HtmlTableRangeView *rangeView;
    RangeSelectorTableModel *rangeModel;
    RangeSelectorTableDelegate *rangeDelegate;

    bool isUpdating;
};

#endif // PROFILECLUSTEREXPLORER_H
