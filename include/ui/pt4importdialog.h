#ifndef PT4IMPORTDIALOG_H
#define PT4IMPORTDIALOG_H

#include <QDialog>
#include <QSqlDatabase>
#include <QJsonObject>
#include <QMap>
#include <QPushButton>

namespace Ui {
class PT4ImportDialog;
}

struct PreflopPlayer {
    QString name;
    int position;
    float stack;
    QString range;
    bool folded;
    int calls;
    int raises;
};

struct ParsedProfileData {
    QString range;
    QString flop_bet_sizes;
    QString turn_bet_sizes;
    QString river_bet_sizes;
    QString flop_raise_sizes;
    QString turn_raise_sizes;
    QString river_raise_sizes;
    QString flop_donk_sizes;
    QString turn_donk_sizes;
    QString river_donk_sizes;
    bool flop_allin = false;
    bool turn_allin = false;
    bool river_allin = true;
};


struct ImportedHand {
    QString board;
    QString turn_card;
    QString river_card;
    float pot;
    float effective_stack;
    QString ip_player;
    QString oop_player;
    QString ip_range;
    QString oop_range;
    
    QString ip_flop_bet_sizes;
    QString ip_turn_bet_sizes;
    QString ip_river_bet_sizes;
    QString oop_flop_bet_sizes;
    QString oop_turn_bet_sizes;
    QString oop_river_bet_sizes;

    QString ip_flop_raise_sizes;
    QString ip_turn_raise_sizes;
    QString ip_river_raise_sizes;
    QString oop_flop_raise_sizes;
    QString oop_turn_raise_sizes;
    QString oop_river_raise_sizes;

    QString oop_turn_donk_sizes;
    QString oop_river_donk_sizes;

    bool ip_flop_allin;
    bool ip_turn_allin;
    bool ip_river_allin;
    bool oop_flop_allin;
    bool oop_turn_allin;
    bool oop_river_allin;
    
    int cnt_players;
    float ante;
    QString hand_no;
    float bb_size = 2.0f;
    int id_hand = -1;
    std::vector<PreflopPlayer> preflop_players;
};

class HandVisualizerWidget;

class PT4ImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PT4ImportDialog(QWidget *parent = nullptr);
    ~PT4ImportDialog();

    bool hasSelectedHand() const;
    ImportedHand getSelectedHand() const;
    bool isPreflopImport() const;

private slots:
    void on_searchButton_clicked();
    void on_importButton_clicked();
    void on_copyParamsButton_clicked();
    void on_importPreflopButton_clicked();
    void prevPage();
    void nextPage();
    void onHandSelectionChanged();
    void onHandHovered(int row, int column);
    void onHandClicked(int row, int column);

private:
    void setupDatabase();
    void loadProfiles();
    void getProfileData(const QString& playerName, int position, const QString& scenarioName, const QString& positionName, double stackBb, double blindLevel, ParsedProfileData& pd) const;
    void getPreflopRangeKeys(int id_hand, const QString& ipPlayer, const QString& oopPlayer, QString& ipScenario, QString& oopScenario, QString& ipPosName, QString& oopPosName) const;
    void runSearch();
    void loadHandForVisualizer(int row);

    Ui::PT4ImportDialog *ui;
    QSqlDatabase db;
    QJsonObject profilesData;
    QMap<QString, QString> playerMapping;
    HandVisualizerWidget* visualizer;
    QPushButton* copyRawButton = nullptr;
    
    bool m_hasSelectedHand;
    ImportedHand m_selectedHand;
    bool m_isPreflopImport;
    bool m_isSelectionLocked;
    int m_lockedRow;
    int m_currentPage;
    const int m_pageSize = 50;
    QString m_currentHistoryText;
};

#endif // PT4IMPORTDIALOG_H
