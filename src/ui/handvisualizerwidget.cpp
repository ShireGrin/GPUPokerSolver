#include "include/ui/handvisualizerwidget.h"
#include <QPainterPath>
#include <QTime>
#include <cmath>
#include "include/tools/dbmanager.h"

// --- TableDrawingWidget Implementation ---

TableDrawingWidget::TableDrawingWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(450, 320);
}

void TableDrawingWidget::updateState(const HandGameState& state) {
    m_state = state;
    m_hasState = true;
    update();
}

void TableDrawingWidget::clearState() {
    m_hasState = false;
    update();
}

void TableDrawingWidget::setDisplayMode(StackDisplayMode mode) {
    m_displayMode = mode;
    update();
}

void TableDrawingWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Let the widget background seamlessly blend into the parent's QFrame theme
    if (!m_hasState) {
        painter.setPen(QColor(100, 100, 100));
        painter.setFont(QFont("Outfit", 12, QFont::Medium));
        painter.drawText(rect(), Qt::AlignCenter, "Select a hand to preview or play");
        return;
    }

    int width = this->width();
    int height = this->height();
    QPointF center(width / 2.0, height / 2.0 - 10.0);

    // Scale felt dimensions to fit widget bounding box and enforce horizontal oval
    double rx = width * 0.42;
    double ry = rx * 0.55; // Enforce a fixed aspect ratio so it never becomes circular or vertical
    // 1. Draw leather table outer rim
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(22, 22, 24)); // Table wood/leather color
    painter.drawEllipse(center, rx + 20.0, ry + 20.0);

    // 2. Draw outer rim highlight
    painter.setPen(QPen(QColor(60, 60, 64), 2));
    painter.setBrush(Qt::NoPen);
    painter.drawEllipse(center, rx + 20.0, ry + 20.0);

    // 3. Draw felt (radial gradient for standard premium poker room look)
    QRadialGradient feltGrad(center, rx + 10.0);
    feltGrad.setColorAt(0.0, QColor(24, 76, 56));  // Lighter center felt
    feltGrad.setColorAt(1.0, QColor(14, 46, 34));  // Darker outer felt
    painter.setPen(Qt::NoPen);
    painter.setBrush(feltGrad);
    painter.drawEllipse(center, rx + 10.0, ry + 10.0);

    // 4. Draw center felt outline
    painter.setPen(QPen(QColor(40, 100, 75), 1, Qt::DashLine));
    painter.drawEllipse(center, rx - 15.0, ry - 15.0);

    // 5. Draw Community Cards in the center
    int numBoard = m_state.board_cards.size();
    if (numBoard > 0) {
        double cardW = 32.0;
        double cardH = 46.0;
        double spacing = 4.0;
        double totalW = numBoard * cardW + (numBoard - 1) * spacing;
        double startX = center.x() - totalW / 2.0;
        double startY = center.y() - cardH / 2.0 - 5.0;

        for (int i = 0; i < numBoard; ++i) {
            drawCard(painter, QRectF(startX + i * (cardW + spacing), startY, cardW, cardH), m_state.board_cards[i]);
        }
    }

    // 6. Draw pot value in the center felt
    if (m_state.pot > 0.0f) {
        QString potText;
        if (m_displayMode == StackDisplayMode::BigBlinds && m_state.bb_size > 0.0f) {
            potText = QString("POT: %1 BB").arg(QString::number(m_state.pot / m_state.bb_size, 'f', 1));
        } else {
            potText = QString("POT: %1").arg(m_state.pot);
        }

        // Draw a premium pill-shaped container for the pot
        double potW = 100.0;
        double potH = 22.0;
        // Position it right below center if board cards are present, or right in the center if not
        double potY = (numBoard > 0) ? (center.y() + 29.0) : (center.y() - potH / 2.0);
        QRectF potRect(center.x() - potW / 2.0, potY, potW, potH);

        painter.setPen(QPen(QColor(255, 215, 0, 100), 1)); // Subtle gold border
        painter.setBrush(QColor(15, 20, 25, 220)); // Deep dark background
        painter.drawRoundedRect(potRect, 11.0, 11.0); // Perfect pill

        // Draw a tiny gold coin/circle icon next to the text
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 215, 0)); // Gold coin
        painter.drawEllipse(QPointF(potRect.left() + 12.0, potRect.center().y()), 4.5, 4.5);
        painter.setPen(QPen(QColor(200, 150, 0), 0.5));
        painter.setBrush(Qt::NoPen);
        painter.drawEllipse(QPointF(potRect.left() + 12.0, potRect.center().y()), 4.5, 4.5);

        // Draw the text
        painter.setPen(QColor(255, 215, 0)); // Gold text
        painter.setFont(QFont("Outfit", 8, QFont::Bold));
        painter.drawText(potRect.adjusted(18, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, potText);
    }

    // 6b. Draw blinds and antes in the center felt
    {
        double infoY;
        if (m_state.pot > 0.0f) {
            double potY = (numBoard > 0) ? (center.y() + 29.0) : (center.y() - 11.0);
            infoY = potY + 22.0 + 4.0; // 22.0 is potH, plus 4px gap
        } else {
            infoY = (numBoard > 0) ? (center.y() + 29.0) : (center.y() + 5.0);
        }

        QString infoText;
        float sb = m_state.sb_size;
        float bb = m_state.bb_size;
        float ante = m_state.ante_size;

        if (bb <= 0.0f) bb = 2.0f;
        if (sb <= 0.0f) sb = bb / 2.0f;

        if (m_displayMode == StackDisplayMode::BigBlinds) {
            float sb_bb = sb / bb;
            float bb_bb = 1.0f;
            float ante_bb = ante / bb;

            infoText = QString("Blinds: %1/%2 BB").arg(QString::number(sb_bb, 'g', 3)).arg(QString::number(bb_bb, 'g', 3));
            if (ante_bb > 0.0f) {
                infoText += QString(" | Ante: %1 BB").arg(QString::number(ante_bb, 'g', 3));
            }
        } else {
            infoText = QString("Blinds: %1/%2").arg(QString::number(sb, 'g', 4)).arg(QString::number(bb, 'g', 4));
            if (ante > 0.0f) {
                infoText += QString(" | Ante: %1").arg(QString::number(ante, 'g', 4));
            }
        }

        painter.setPen(QColor(180, 220, 200, 200)); // Sleek soft mint green to blend with premium felt
        painter.setFont(QFont("Outfit", 8, QFont::DemiBold));
        QRectF infoRect(center.x() - 150.0, infoY, 300.0, 16.0);
        painter.drawText(infoRect, Qt::AlignCenter, infoText);
    }

    // 7. Draw Player Seats, Dealer Button, Bets
    int numPlayers = m_state.players.size();
    if (numPlayers == 0) return;

    for (int i = 0; i < numPlayers; ++i) {
        const PlayerState& p = m_state.players[i];
        
        // Arrange seats explicitly Left (IP) and Right (OOP) for horizontal view in Heads Up,
        // otherwise distribute them evenly around the oval for multi-way tables.
        double angle;
        if (numPlayers == 2) {
            angle = i * M_PI + M_PI;
        } else {
            angle = (i * 2.0 * M_PI) / numPlayers + M_PI / 2.0;
        }
        double x = center.x() + rx * cos(angle);
        double y = center.y() + ry * sin(angle);

        // Draw Player Seat
        drawSeat(painter, QPointF(x, y), p, p.active);

        // Draw Dealer Button
        if (p.position_label == "BTN" || p.position_label == "SB/BTN") {
            double btnX = center.x() + (rx - 45.0) * cos(angle + 0.15);
            double btnY = center.y() + (ry - 35.0) * sin(angle + 0.15);
            
            painter.setPen(QPen(QColor(80, 80, 80), 1));
            painter.setBrush(QColor(255, 255, 255));
            painter.drawEllipse(QPointF(btnX, btnY), 9.0, 9.0);
            
            painter.setPen(QColor(20, 20, 20));
            painter.setFont(QFont("Outfit", 7, QFont::Bold));
            painter.drawText(QRectF(btnX - 9.0, btnY - 9.0, 18.0, 18.0), Qt::AlignCenter, "D");
        }

        // Draw Player Bets
        if (p.current_bet > 0.0f && !p.folded) {
            double betX = center.x() + (rx - 55.0) * cos(angle);
            double betY = center.y() + (ry - 45.0) * sin(angle);

            // Draw a tiny chip circle
            painter.setPen(QPen(QColor(255, 255, 255), 1));
            painter.setBrush(QColor(220, 50, 50)); // Red chip
            painter.drawEllipse(QPointF(betX - 12.0, betY), 5.0, 5.0);

            painter.setPen(QColor(220, 220, 220));
            painter.setFont(QFont("Outfit", 8, QFont::Bold));
            
            QString betStr;
            if (m_displayMode == StackDisplayMode::BigBlinds && m_state.bb_size > 0.0f) {
                betStr = QString("%1 BB").arg(QString::number(p.current_bet / m_state.bb_size, 'f', 1));
            } else {
                betStr = QString::number(p.current_bet);
            }
            
            painter.drawText(QRectF(betX - 5.0, betY - 10.0, 55.0, 20.0), Qt::AlignLeft | Qt::AlignVCenter, betStr);
        }
    }
}

void TableDrawingWidget::drawSeat(QPainter& painter, const QPointF& pos, const PlayerState& p, bool isActive) {
    double w = 82.0;
    double h = 38.0;
    QRectF rect(pos.x() - w / 2.0, pos.y() - h / 2.0, w, h);

    painter.save();
    
    // Set opacity based on folded state
    if (p.folded) {
        painter.setOpacity(0.35);
    }

    // Glowing active border
    if (isActive && !p.folded) {
        painter.setPen(QPen(QColor(255, 200, 0), 2.5)); // Gold highlight
    } else {
        painter.setPen(QPen(QColor(60, 64, 72), 1.2));
    }

    // Fill seat box with sleek charcoal glass
    painter.setBrush(QColor(36, 40, 48, 230));
    painter.drawRoundedRect(rect, 6.0, 6.0);

    // Draw position label (small badge at the top)
    QRectF posRect(rect.left() + 2.0, rect.top() - 6.0, 24.0, 11.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(p.folded ? QColor(80, 80, 80) : QColor(0, 200, 50)); // Green badge
    painter.drawRoundedRect(posRect, 2.0, 2.0);
    
    painter.setPen(Qt::white);
    painter.setFont(QFont("Outfit", 6, QFont::Bold));
    painter.drawText(posRect, Qt::AlignCenter, p.position_label);

    // Draw player name
    painter.setPen(p.folded ? QColor(140, 140, 140) : QColor(240, 240, 240));
    painter.setFont(QFont("Outfit", 8, QFont::Medium));
    QString displayName = p.name;
    
    if (!p.cluster_name.isEmpty() && p.cluster_name != "Unknown") {
        if (displayName.length() > 6) displayName = displayName.left(5) + "..";
        displayName += " (" + p.cluster_name + ")";
    } else {
        if (displayName.length() > 9) displayName = displayName.left(7) + "..";
    }
    
    painter.drawText(QRectF(rect.left() + 2.0, rect.top() + 4.0, rect.width() - 4.0, 16.0), Qt::AlignCenter, displayName);

    // Draw player stack size
    painter.setPen(p.folded ? QColor(120, 120, 120) : QColor(255, 215, 0));
    painter.setFont(QFont("Outfit", 7, QFont::Bold));
    
    QString stackText;
    if (m_displayMode == StackDisplayMode::BigBlinds && m_state.bb_size > 0.0f) {
        stackText = QString("%1 BB").arg(QString::number(p.stack / m_state.bb_size, 'f', 1));
    } else {
        stackText = QString::number(p.stack);
    }
    
    painter.drawText(QRectF(rect.left() + 2.0, rect.top() + 19.0, rect.width() - 4.0, 14.0), Qt::AlignCenter, stackText);

    // Draw hole cards if present next to the seat
    if (!p.folded && (!p.card1.isEmpty() || !p.card2.isEmpty())) {
        double cardW = 16.0;
        double cardH = 23.0;
        
        // Draw cards slightly overlapping above/behind the seat
        QRectF card1Rect(rect.left() + rect.width() / 2.0 - cardW + 1.0, rect.top() - cardH + 4.0, cardW, cardH);
        QRectF card2Rect(rect.left() + rect.width() / 2.0 - 1.0, rect.top() - cardH + 4.0, cardW, cardH);

        drawCard(painter, card1Rect, p.card1);
        drawCard(painter, card2Rect, p.card2);
    }

    painter.restore();
}

void TableDrawingWidget::drawCard(QPainter& painter, const QRectF& rect, const QString& cardStr) {
    painter.save();
    
    // Draw card background
    painter.setPen(QPen(QColor(180, 180, 180), 0.5));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(rect, 2.0, 2.0);

    if (cardStr.isEmpty() || cardStr == "??" || cardStr == "XX") {
        // Draw card back
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(180, 40, 40)); // Red card back
        painter.drawRoundedRect(rect.adjusted(1.5, 1.5, -1.5, -1.5), 1.0, 1.0);
        painter.restore();
        return;
    }

    QString rank = cardStr.left(1);
    QString suit = cardStr.mid(1, 1).toLower();

    QColor suitColor = Qt::black;
    if (suit == "h") suitColor = QColor(220, 50, 50);      // Red Hearts
    else if (suit == "d") suitColor = QColor(40, 100, 220);  // Blue Diamonds
    else if (suit == "c") suitColor = QColor(20, 150, 60);   // Green Clubs
    else if (suit == "s") suitColor = QColor(40, 40, 40);     // Dark Spades

    // Map suit character to Unicode symbol
    QString suitSymbol = "";
    if (suit == "h") suitSymbol = "♥";
    else if (suit == "d") suitSymbol = "♦";
    else if (suit == "c") suitSymbol = "♣";
    else if (suit == "s") suitSymbol = "♠";

    // Draw rank text
    painter.setPen(suitColor);
    painter.setFont(QFont("Outfit", rect.height() > 30 ? 11 : 7, QFont::Bold));
    painter.drawText(rect.adjusted(1, 0, -1, 0), Qt::AlignLeft | Qt::AlignTop, rank);

    // Draw suit symbol
    painter.drawText(rect.adjusted(1, 0, -1, -1), Qt::AlignRight | Qt::AlignBottom, suitSymbol);

    painter.restore();
}


// --- HandVisualizerWidget Implementation ---

HandVisualizerWidget::HandVisualizerWidget(QWidget* parent) : QWidget(parent) {
    // Layout setup
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    QSplitter* visualSplitter = new QSplitter(Qt::Horizontal, this);

    // Left container: Virtual table and playback buttons
    QWidget* tableContainer = new QWidget(this);
    QVBoxLayout* tableLayout = new QVBoxLayout(tableContainer);
    tableLayout->setContentsMargins(0, 0, 0, 0);

    // Header Panel for Options
    QWidget* headerPanel = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(headerPanel);
    headerLayout->setContentsMargins(10, 4, 10, 4);

    QLabel* titleLabel = new QLabel("LIVE PREVIEW", this);
    titleLabel->setStyleSheet("font-weight: bold; color: #6c727e; font-size: 9px; letter-spacing: 1px;");

    displayModeComboBox = new QComboBox(this);
    displayModeComboBox->addItem("Show Stacks: Chips", "chips");
    displayModeComboBox->addItem("Show Stacks: BB", "bb");
    displayModeComboBox->setStyleSheet(
        "QComboBox { background-color: #242830; color: #b4bccc; border: 1px solid #3a3f4a; border-radius: 4px; padding: 3px 10px; font-size: 9px; font-weight: bold; }"
        "QComboBox:hover { background-color: #2c313c; color: #fff; border-color: #555; }"
        "QComboBox QAbstractItemView { background-color: #1e222b; color: #ddd; selection-background-color: #00c832; border: 1px solid #333; }"
    );

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(displayModeComboBox);

    tableLayout->addWidget(headerPanel);

    // Create table panel
    tableWidget = new TableDrawingWidget(this);
    tableLayout->addWidget(tableWidget, 1);

    // Current Action Info Panel
    QWidget* infoPanel = new QWidget(this);
    QHBoxLayout* infoLayout = new QHBoxLayout(infoPanel);
    infoLayout->setContentsMargins(10, 5, 10, 5);

    streetLabel = new QLabel("STREET: --", this);
    streetLabel->setStyleSheet("font-weight: bold; color: #fff; font-size: 10px;");
    potLabel = new QLabel("POT: 0.0", this);
    potLabel->setStyleSheet("font-weight: bold; color: #ffd700; font-size: 10px;");

    infoLayout->addWidget(streetLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(potLabel);
    tableLayout->addWidget(infoPanel);

    // Bold action description label
    actionDescriptionLabel = new QLabel("Select a hand from the list...", this);
    actionDescriptionLabel->setAlignment(Qt::AlignCenter);
    actionDescriptionLabel->setStyleSheet("font-weight: bold; color: #00ff40; font-size: 11px; margin: 4px;");
    actionDescriptionLabel->setWordWrap(true);
    tableLayout->addWidget(actionDescriptionLabel);

    // Control Buttons
    QWidget* controlsWidget = new QWidget(this);
    QHBoxLayout* controlsLayout = new QHBoxLayout(controlsWidget);
    controlsLayout->setContentsMargins(10, 5, 10, 5);
    controlsLayout->setSpacing(10);

    resetButton = new QPushButton("🔄 Reset", this);
    prevButton = new QPushButton("◀ Prev", this);
    playButton = new QPushButton("▶ Play", this);
    nextButton = new QPushButton("Next ▶", this);

    styleButton(resetButton, "#444", "#555");
    styleButton(prevButton, "#444", "#555");
    styleButton(playButton, "#00c832", "#00e038");
    styleButton(nextButton, "#444", "#555");

    controlsLayout->addWidget(resetButton);
    controlsLayout->addWidget(prevButton);
    controlsLayout->addWidget(playButton);
    controlsLayout->addWidget(nextButton);

    tableLayout->addWidget(controlsWidget);
    visualSplitter->addWidget(tableContainer);

    // Right container: Scrollable Action Log List
    QWidget* logContainer = new QWidget(this);
    QVBoxLayout* logLayout = new QVBoxLayout(logContainer);
    logLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* logTitle = new QLabel("<b>Hand Action History Log</b>", this);
    logTitle->setStyleSheet("color: #aaa; padding: 4px; font-size: 10px;");
    logLayout->addWidget(logTitle);

    actionListWidget = new QListWidget(this);
    actionListWidget->setMinimumWidth(180);
    actionListWidget->setMaximumWidth(320);
    actionListWidget->setStyleSheet(
        "QListWidget { background-color: #1e222b; color: #ddd; border: 1px solid #333; border-radius: 4px; padding: 4px; }"
        "QListWidget::item { padding: 4px 6px; border-bottom: 1px solid #282c34; font-size: 10px; }"
        "QListWidget::item:hover { background-color: #2c313c; color: #fff; }"
        "QListWidget::item:selected { background-color: #00c832; color: #fff; font-weight: bold; }"
    );
    logLayout->addWidget(actionListWidget);

    visualSplitter->addWidget(logContainer);
    mainLayout->addWidget(visualSplitter);

    // Connect control button slots
    connect(resetButton, &QPushButton::clicked, this, &HandVisualizerWidget::resetHand);
    connect(prevButton, &QPushButton::clicked, this, &HandVisualizerWidget::stepBackward);
    connect(playButton, &QPushButton::clicked, this, &HandVisualizerWidget::togglePlay);
    connect(nextButton, &QPushButton::clicked, this, &HandVisualizerWidget::stepForward);
    connect(actionListWidget, &QListWidget::itemClicked, this, &HandVisualizerWidget::onActionLogItemClicked);
    
    connect(displayModeComboBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_displayMode = (index == 1) ? StackDisplayMode::BigBlinds : StackDisplayMode::Chips;
        tableWidget->setDisplayMode(m_displayMode);
        updateUIForStep();
    });

    displayModeComboBox->setCurrentIndex(1);

    // Setup play timer
    playTimer = new QTimer(this);
    connect(playTimer, &QTimer::timeout, this, &HandVisualizerWidget::onPlayTimerTimeout);

    clear();
}

HandVisualizerWidget::~HandVisualizerWidget() {
    delete playTimer;
}

void HandVisualizerWidget::styleButton(QPushButton* btn, const QString& normalColor, const QString& hoverColor) {
    btn->setStyleSheet(
        QString(
            "QPushButton { padding: 6px 12px; font-weight: bold; background-color: %1; color: white; border: none; border-radius: 4px; font-size: 10px; }"
            "QPushButton:hover { background-color: %2; }"
            "QPushButton:pressed { background-color: #222; }"
            "QPushButton:disabled { background-color: #222; color: #555; }"
        ).arg(normalColor).arg(hoverColor)
    );
}

void HandVisualizerWidget::setHand(const ImportedHand& hand, const QString& historyText) {
    clear();

    // 1. Parse text log to extract states
    m_states = HandHistoryParser::parseHistory(historyText, hand.preflop_players, hand.bb_size, hand.ante * hand.bb_size);

    // Populate cluster names
    QMap<QString, QString> clusterMap;
    for (auto& state : m_states) {
        for (auto& p : state.players) {
            if (!clusterMap.contains(p.name)) {
                QString fullCluster = DBManager::instance().getClusterForPlayerName(p.name);
                QString shortCluster = fullCluster;
                if (shortCluster.contains("(")) {
                    int start = shortCluster.indexOf("(") + 1;
                    int end = shortCluster.indexOf(")");
                    if (start > 0 && end > start) {
                        shortCluster = shortCluster.mid(start, end - start);
                    }
                }
                clusterMap[p.name] = shortCluster;
            }
            p.cluster_name = clusterMap[p.name];
        }
    }

    // 2. Populate Action Log List
    actionListWidget->clear();
    for (size_t i = 0; i < m_states.size(); ++i) {
        QListWidgetItem* item = new QListWidgetItem(m_states[i].action_description);
        item->setData(Qt::UserRole, (int)i);
        actionListWidget->addItem(item);
    }

    if (!m_states.empty()) {
        m_currentStep = 0;
        updateUIForStep();
        prevButton->setEnabled(true);
        nextButton->setEnabled(true);
        playButton->setEnabled(true);
        resetButton->setEnabled(true);
    }
}

void HandVisualizerWidget::clear() {
    m_states.clear();
    m_currentStep = 0;
    m_isPlaying = false;
    playTimer->stop();
    playButton->setText("▶ Play");
    styleButton(playButton, "#00c832", "#00e038");

    tableWidget->clearState();
    actionListWidget->clear();
    
    streetLabel->setText("STREET: --");
    potLabel->setText("POT: 0.0");
    actionDescriptionLabel->setText("Select a hand from the list...");
    
    prevButton->setEnabled(false);
    nextButton->setEnabled(false);
    playButton->setEnabled(false);
    resetButton->setEnabled(false);
}

void HandVisualizerWidget::updateUIForStep() {
    if (m_states.empty() || m_currentStep < 0 || m_currentStep >= (int)m_states.size()) return;

    const HandGameState& state = m_states[m_currentStep];

    // Update Virtual table felt widget
    tableWidget->updateState(state);

    // Update labels
    streetLabel->setText(QString("STREET: %1").arg(state.current_street.toUpper()));
    if (m_displayMode == StackDisplayMode::BigBlinds && state.bb_size > 0.0f) {
        potLabel->setText(QString("POT: %1 BB").arg(QString::number(state.pot / state.bb_size, 'f', 1)));
    } else {
        potLabel->setText(QString("POT: %1").arg(state.pot));
    }
    actionDescriptionLabel->setText(state.action_description);

    // Sync Action History Log selected row
    actionListWidget->blockSignals(true);
    actionListWidget->setCurrentRow(m_currentStep);
    actionListWidget->scrollToItem(actionListWidget->currentItem(), QAbstractItemView::PositionAtCenter);
    actionListWidget->blockSignals(false);

    // Button states
    prevButton->setEnabled(m_currentStep > 0);
    nextButton->setEnabled(m_currentStep < (int)m_states.size() - 1);
}

void HandVisualizerWidget::stepForward() {
    if (m_currentStep < (int)m_states.size() - 1) {
        m_currentStep++;
        updateUIForStep();
    } else {
        if (m_isPlaying) {
            togglePlay(); // Pause at the end
        }
    }
}

void HandVisualizerWidget::stepBackward() {
    if (m_currentStep > 0) {
        m_currentStep--;
        updateUIForStep();
    }
}

void HandVisualizerWidget::togglePlay() {
    m_isPlaying = !m_isPlaying;
    if (m_isPlaying) {
        playButton->setText("⏸ Pause");
        styleButton(playButton, "#e08000", "#f89000"); // Orange
        playTimer->start(1200); // Step every 1.2 seconds
    } else {
        playTimer->stop();
        playButton->setText("▶ Play");
        styleButton(playButton, "#00c832", "#00e038"); // Green
    }
}

void HandVisualizerWidget::resetHand() {
    m_currentStep = 0;
    if (m_isPlaying) {
        togglePlay(); // Stop playing
    }
    updateUIForStep();
}

void HandVisualizerWidget::onPlayTimerTimeout() {
    stepForward();
}

void HandVisualizerWidget::onActionLogItemClicked(QListWidgetItem* item) {
    if (m_isPlaying) {
        togglePlay(); // Pause playing if they manually click an item
    }
    int step = item->data(Qt::UserRole).toInt();
    if (step >= 0 && step < (int)m_states.size()) {
        m_currentStep = step;
        updateUIForStep();
    }
}
