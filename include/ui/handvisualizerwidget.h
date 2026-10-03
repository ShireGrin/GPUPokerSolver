#ifndef HANDVISUALIZERWIDGET_H
#define HANDVISUALIZERWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QPaintEvent>
#include <QPainter>
#include <QComboBox>
#include <vector>
#include <memory>
#include "include/ui/handhistoryparser.h"

enum class StackDisplayMode {
    Chips,
    BigBlinds
};

// Custom widget to draw the virtual table felt, seats, cards, and bets
class TableDrawingWidget : public QWidget {
    Q_OBJECT
private:
    HandGameState m_state;
    bool m_hasState = false;
    StackDisplayMode m_displayMode = StackDisplayMode::BigBlinds;

protected:
    void paintEvent(QPaintEvent* event) override;
    void drawCard(QPainter& painter, const QRectF& rect, const QString& cardStr);
    void drawSeat(QPainter& painter, const QPointF& pos, const PlayerState& p, bool isActive);

public:
    explicit TableDrawingWidget(QWidget* parent = nullptr);
    void updateState(const HandGameState& state);
    void setDisplayMode(StackDisplayMode mode);
    void clearState();
};

// Main visualizer widget combining the table drawing panel, playback controls, and action log list
class HandVisualizerWidget : public QWidget {
    Q_OBJECT
private:
    TableDrawingWidget* tableWidget;
    QListWidget* actionListWidget;
    QComboBox* displayModeComboBox;
    
    QLabel* streetLabel;
    QLabel* potLabel;
    QLabel* actionDescriptionLabel;
    
    QPushButton* prevButton;
    QPushButton* nextButton;
    QPushButton* playButton;
    QPushButton* resetButton;
    
    QTimer* playTimer;
    std::vector<HandGameState> m_states;
    int m_currentStep = 0;
    bool m_isPlaying = false;
    StackDisplayMode m_displayMode = StackDisplayMode::BigBlinds;

private slots:
    void stepForward();
    void stepBackward();
    void togglePlay();
    void resetHand();
    void onPlayTimerTimeout();
    void onActionLogItemClicked(QListWidgetItem* item);

private:
    void updateUIForStep();
    void styleButton(QPushButton* btn, const QString& normalColor, const QString& hoverColor);

public:
    explicit HandVisualizerWidget(QWidget* parent = nullptr);
    ~HandVisualizerWidget();

    // Sets up the hand from parsed database players and history text
    void setHand(const ImportedHand& hand, const QString& historyText = "");
    void clear();
};

#endif // HANDVISUALIZERWIDGET_H
