#include "include/ui/handhistoryparser.h"
#include <QRegularExpression>
#include <QDebug>

// Helper to assign position labels based on PT4 positions or seat arrangements
QString getPlayerPositionLabel(int position, int totalPlayers) {
    if (position == 9) return "SB";
    if (position == 8) return "BB";
    if (position == 0) return "BTN";
    if (position == 1) return "CO";
    if (position == 2) return "HJ";
    if (position == 3) return "LJ";
    
    // Fallback for Heads up
    if (totalPlayers == 2) {
        if (position == 0) return "SB/BTN";
        if (position == 9) return "SB";
        if (position == 8) return "BB";
    }
    
    // General fallback
    if (position >= 4 && position <= 7) {
        return QString("UTG+%1").arg(position - 4);
    }
    return QString("UTG");
}

std::vector<HandGameState> HandHistoryParser::parseHistory(const QString& historyText, const std::vector<PreflopPlayer>& dbPlayers, float default_bb, float default_ante) {
    std::vector<HandGameState> states;
    
    // Split the text into lines
    QStringList lines = historyText.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    if (lines.isEmpty()) return states;

    // First pass: extract seats, stacks, and names
    std::vector<PlayerState> initialPlayers;
    int buttonSeat = -1;
    bool setup_ended = false;
    
    QRegularExpression seatRegex("^Seat (\\d+): (.+?) \\(\\$?(\\d+(?:\\.\\d+)?)(?: in chips)?\\)");
    QRegularExpression buttonRegex("Seat #(\\d+) is the button");
    
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        
        if (trimmed.startsWith("***") || trimmed.contains(" posts ") || trimmed.contains(" posts the ante")) {
            setup_ended = true;
        }
        
        if (!setup_ended) {
            QRegularExpressionMatch seatMatch = seatRegex.match(trimmed);
            if (seatMatch.hasMatch()) {
                PlayerState p;
                p.seat = seatMatch.captured(1).toInt();
                p.name = seatMatch.captured(2).trimmed();
                p.stack = seatMatch.captured(3).toFloat();
                p.initial_stack = p.stack;
                
                // Avoid duplicates (same seat or same name)
                bool duplicate = false;
                for (const auto& existing : initialPlayers) {
                    if (existing.seat == p.seat || existing.name.compare(p.name, Qt::CaseInsensitive) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    initialPlayers.push_back(p);
                }
                continue;
            }
        }
        
        QRegularExpressionMatch btnMatch = buttonRegex.match(trimmed);
        if (btnMatch.hasMatch()) {
            buttonSeat = btnMatch.captured(1).toInt();
        }
    }
    
    if (initialPlayers.empty()) {
        // If we failed to parse seats from text, try to rebuild from dbPlayers
        if (!dbPlayers.empty()) {
            for (size_t i = 0; i < dbPlayers.size(); ++i) {
                // Defensive duplicate check
                bool duplicate = false;
                for (const auto& existing : initialPlayers) {
                    if (existing.name.compare(dbPlayers[i].name, Qt::CaseInsensitive) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;

                PlayerState p;
                p.seat = i + 1; // Fake seat numbers
                p.name = dbPlayers[i].name;
                p.stack = dbPlayers[i].stack * default_bb; // Scale to chips
                p.initial_stack = p.stack;
                p.position_label = getPlayerPositionLabel(dbPlayers[i].position, dbPlayers.size());
                initialPlayers.push_back(p);
            }
            buttonSeat = 1;
        } else {
            return states; // Cannot do anything
        }
    }
    
    // Map position labels
    int totalPlayers = initialPlayers.size();
    for (PlayerState& p : initialPlayers) {
        // First try to match with dbPlayers
        bool matched = false;
        for (const PreflopPlayer& dbP : dbPlayers) {
            if (dbP.name.compare(p.name, Qt::CaseInsensitive) == 0) {
                p.position_label = getPlayerPositionLabel(dbP.position, totalPlayers);
                matched = true;
                break;
            }
        }
        
        // Dynamic seat-based position estimation if not matched
        if (!matched && buttonSeat != -1) {
            // Sort players clockwise starting from the button seat
            std::vector<int> sortedIndices;
            for (size_t i = 0; i < initialPlayers.size(); ++i) {
                sortedIndices.push_back(i);
            }
            std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
                int distA = (initialPlayers[a].seat - buttonSeat + 10) % 10;
                int distB = (initialPlayers[b].seat - buttonSeat + 10) % 10;
                return distA < distB;
            });
            
            // sortedIndices[0] is BTN
            // sortedIndices[1] is SB
            // sortedIndices[2] is BB
            int idx = -1;
            for (size_t i = 0; i < sortedIndices.size(); ++i) {
                if (initialPlayers[sortedIndices[i]].name == p.name) {
                    idx = i;
                    break;
                }
            }
            
            if (idx == 0) {
                p.position_label = (totalPlayers == 2) ? "SB/BTN" : "BTN";
            } else if (idx == 1) {
                p.position_label = (totalPlayers == 2) ? "BB" : "SB";
            } else if (idx == 2) {
                p.position_label = "BB";
            } else if (idx == totalPlayers - 1) {
                p.position_label = "CO";
            } else if (idx == totalPlayers - 2 && totalPlayers >= 5) {
                p.position_label = "HJ";
            } else {
                p.position_label = QString("UTG+%1").arg(idx - 3);
            }
        }
    }
    
    // Create initial state
    HandGameState currentState;
    currentState.players = initialPlayers;
    currentState.pot = 0.0f;
    currentState.bb_size = default_bb;
    currentState.sb_size = default_bb / 2.0f;
    currentState.ante_size = default_ante;
    currentState.current_street = "Setup";
    currentState.action_description = "Hand Setup - Blinds & Antes";
    states.push_back(currentState);
    
    // Parse actions line-by-line
    QRegularExpression anteRegex("^(.+?): posts the ante \\$?(\\d+(?:\\.\\d+)?)");
    QRegularExpression sbRegex("^(.+?): posts small blind \\$?(\\d+(?:\\.\\d+)?)");
    QRegularExpression bbRegex("^(.+?): posts big blind \\$?(\\d+(?:\\.\\d+)?)");
    QRegularExpression dealtRegex("^Dealt to (.+?) \\[([2-9TJQKA][cdhs]) ([2-9TJQKA][cdhs])\\]");
    QRegularExpression foldRegex("^(.+?): folds");
    QRegularExpression checkRegex("^(.+?): checks");
    QRegularExpression callRegex("^(.+?): calls \\$?(\\d+(?:\\.\\d+)?)");
    QRegularExpression betRegex("^(.+?): bets \\$?(\\d+(?:\\.\\d+)?)");
    QRegularExpression raiseRegex("^(.+?): raises \\$?(\\d+(?:\\.\\d+)?) to \\$?(\\d+(?:\\.\\d+)?)");
    QRegularExpression showRegex("^(.+?): shows \\[([2-9TJQKA][cdhs]) ([2-9TJQKA][cdhs])\\]");
    QRegularExpression collectsRegex("^(.+?) collected \\$?(\\d+(?:\\.\\d+)?) from pot");
    QRegularExpression collectsMainRegex("^(.+?) collected \\$?(\\d+(?:\\.\\d+)?) from main pot");
    QRegularExpression collectsSideRegex("^(.+?) collected \\$?(\\d+(?:\\.\\d+)?) from side pot");
    QRegularExpression uncalledRegex("^Uncalled bet \\(\\$?(\\d+(?:\\.\\d+)?)\\) returned to (.+)");
    
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        HandGameState nextState = states.back(); // Copy previous state
        bool stateChanged = false;
        int activeIdx = -1;
        
        // Helper to find player index
        auto findActPlayer = [&](const QString& name) -> int {
            for (size_t i = 0; i < nextState.players.size(); ++i) {
                if (nextState.players[i].name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
                    return i;
                }
            }
            return -1;
        };
        
        // 1. Street transitions
        if (trimmed.startsWith("*** HOLE CARDS ***")) {
            nextState.current_street = "Preflop";
            nextState.action_description = "Hole Cards Dealt";
            // Preflop bets are retained (blinds and antes)
            states.push_back(nextState);
            continue;
        } else if (trimmed.startsWith("*** FLOP ***")) {
            // Deal flop cards
            QRegularExpression flopCardsRegex("\\[([2-9TJQKA][cdhs]) ([2-9TJQKA][cdhs]) ([2-9TJQKA][cdhs])\\]");
            QRegularExpressionMatch match = flopCardsRegex.match(trimmed);
            if (match.hasMatch()) {
                nextState.board_cards.clear();
                nextState.board_cards.push_back(match.captured(1));
                nextState.board_cards.push_back(match.captured(2));
                nextState.board_cards.push_back(match.captured(3));
            }
            nextState.current_street = "Flop";
            nextState.action_description = "Dealing Flop: " + nextState.board_cards.join(" ");
            // Clear current bets for next round
            for (auto& p : nextState.players) p.current_bet = 0.0f;
            states.push_back(nextState);
            continue;
        } else if (trimmed.startsWith("*** TURN ***")) {
            // Deal turn card
            QRegularExpression turnCardRegex("\\] \\[([2-9TJQKA][cdhs])\\]");
            QRegularExpressionMatch match = turnCardRegex.match(trimmed);
            if (match.hasMatch()) {
                nextState.board_cards.push_back(match.captured(1));
            }
            nextState.current_street = "Turn";
            nextState.action_description = "Dealing Turn: " + nextState.board_cards.last();
            for (auto& p : nextState.players) p.current_bet = 0.0f;
            states.push_back(nextState);
            continue;
        } else if (trimmed.startsWith("*** RIVER ***")) {
            // Deal river card
            QRegularExpression riverCardRegex("\\] \\[([2-9TJQKA][cdhs])\\]");
            QRegularExpressionMatch match = riverCardRegex.match(trimmed);
            if (match.hasMatch()) {
                nextState.board_cards.push_back(match.captured(1));
            }
            nextState.current_street = "River";
            nextState.action_description = "Dealing River: " + nextState.board_cards.last();
            for (auto& p : nextState.players) p.current_bet = 0.0f;
            states.push_back(nextState);
            continue;
        } else if (trimmed.startsWith("*** SHOW DOWN ***")) {
            nextState.current_street = "Showdown";
            nextState.action_description = "Showdown!";
            for (auto& p : nextState.players) p.current_bet = 0.0f;
            states.push_back(nextState);
            continue;
        } else if (trimmed.startsWith("*** SUMMARY ***")) {
            nextState.current_street = "Summary";
            nextState.action_description = "Summary of Hand";
            for (auto& p : nextState.players) p.current_bet = 0.0f;
            states.push_back(nextState);
            continue;
        }
        
        // 2. Action matching
        QRegularExpressionMatch match;
        
        // Ante
        if ((match = anteRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                float amt = match.captured(2).toFloat();
                nextState.players[activeIdx].current_bet += amt;
                nextState.players[activeIdx].stack -= amt;
                nextState.pot += amt;
                nextState.ante_size = amt; // Store parsed ante size
                nextState.action_description = QString("%1 posts ante %2").arg(nextState.players[activeIdx].name).arg(amt);
                stateChanged = true;
            }
        }
        // Small Blind
        else if ((match = sbRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                float amt = match.captured(2).toFloat();
                nextState.players[activeIdx].current_bet = amt;
                nextState.players[activeIdx].stack -= amt;
                nextState.pot += amt;
                nextState.sb_size = amt; // Store parsed small blind size
                nextState.action_description = QString("%1 posts small blind %2").arg(nextState.players[activeIdx].name).arg(amt);
                stateChanged = true;
            }
        }
        // Big Blind
        else if ((match = bbRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                float amt = match.captured(2).toFloat();
                nextState.players[activeIdx].current_bet = amt;
                nextState.players[activeIdx].stack -= amt;
                nextState.pot += amt;
                nextState.bb_size = amt; // Set the active big blind size from the posts big blind amount
                nextState.action_description = QString("%1 posts big blind %2").arg(nextState.players[activeIdx].name).arg(amt);
                stateChanged = true;
            }
        }
        // Dealt Cards
        else if ((match = dealtRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                nextState.players[activeIdx].card1 = match.captured(2);
                nextState.players[activeIdx].card2 = match.captured(3);
                // Hole cards dealt doesn't change stack/pot, just card visibility
                nextState.action_description = QString("Dealt to %1 [%2 %3]").arg(nextState.players[activeIdx].name).arg(match.captured(2)).arg(match.captured(3));
                stateChanged = true;
            }
        }
        // Fold
        else if ((match = foldRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                nextState.players[activeIdx].folded = true;
                nextState.action_description = QString("%1 folds").arg(nextState.players[activeIdx].name);
                stateChanged = true;
            }
        }
        // Check
        else if ((match = checkRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                nextState.action_description = QString("%1 checks").arg(nextState.players[activeIdx].name);
                stateChanged = true;
            }
        }
        // Call
        else if ((match = callRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                float amt = match.captured(2).toFloat();
                nextState.players[activeIdx].current_bet += amt;
                nextState.players[activeIdx].stack -= amt;
                nextState.pot += amt;
                if (nextState.players[activeIdx].stack <= 0.001f) {
                    nextState.players[activeIdx].all_in = true;
                    nextState.action_description = QString("%1 calls %2 and is all-in").arg(nextState.players[activeIdx].name).arg(amt);
                } else {
                    nextState.action_description = QString("%1 calls %2").arg(nextState.players[activeIdx].name).arg(amt);
                }
                stateChanged = true;
            }
        }
        // Bet
        else if ((match = betRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                float amt = match.captured(2).toFloat();
                nextState.players[activeIdx].current_bet += amt;
                nextState.players[activeIdx].stack -= amt;
                nextState.pot += amt;
                if (nextState.players[activeIdx].stack <= 0.001f) {
                    nextState.players[activeIdx].all_in = true;
                    nextState.action_description = QString("%1 bets %2 and is all-in").arg(nextState.players[activeIdx].name).arg(amt);
                } else {
                    nextState.action_description = QString("%1 bets %2").arg(nextState.players[activeIdx].name).arg(amt);
                }
                stateChanged = true;
            }
        }
        // Raise
        else if ((match = raiseRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                float raise_amt = match.captured(2).toFloat();
                float total_amt = match.captured(3).toFloat();
                float diff = total_amt - nextState.players[activeIdx].current_bet;
                nextState.players[activeIdx].current_bet = total_amt;
                nextState.players[activeIdx].stack -= diff;
                nextState.pot += diff;
                if (nextState.players[activeIdx].stack <= 0.001f) {
                    nextState.players[activeIdx].all_in = true;
                    nextState.action_description = QString("%1 raises %2 to %3 and is all-in").arg(nextState.players[activeIdx].name).arg(raise_amt).arg(total_amt);
                } else {
                    nextState.action_description = QString("%1 raises %2 to %3").arg(nextState.players[activeIdx].name).arg(raise_amt).arg(total_amt);
                }
                stateChanged = true;
            }
        }
        // Shows cards
        else if ((match = showRegex.match(trimmed)).hasMatch()) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                nextState.players[activeIdx].card1 = match.captured(2);
                nextState.players[activeIdx].card2 = match.captured(3);
                nextState.action_description = QString("%1 shows [%2 %3]").arg(nextState.players[activeIdx].name).arg(match.captured(2)).arg(match.captured(3));
                stateChanged = true;
            }
        }
        // Uncalled Bet Returned
        else if ((match = uncalledRegex.match(trimmed)).hasMatch()) {
            float amt = match.captured(1).toFloat();
            activeIdx = findActPlayer(match.captured(2));
            if (activeIdx != -1) {
                nextState.players[activeIdx].current_bet -= amt;
                nextState.players[activeIdx].stack += amt;
                nextState.pot -= amt;
                nextState.action_description = QString("Uncalled bet (%1) returned to %2").arg(amt).arg(nextState.players[activeIdx].name);
                stateChanged = true;
            }
        }
        // Collect pot
        else if (((match = collectsRegex.match(trimmed)).hasMatch()) || 
                 ((match = collectsMainRegex.match(trimmed)).hasMatch()) ||
                 ((match = collectsSideRegex.match(trimmed)).hasMatch())) {
            activeIdx = findActPlayer(match.captured(1));
            if (activeIdx != -1) {
                float amt = match.captured(2).toFloat();
                nextState.players[activeIdx].stack += amt;
                nextState.action_description = QString("%1 collects %2 from pot").arg(nextState.players[activeIdx].name).arg(amt);
                stateChanged = true;
            }
        }
        
        if (stateChanged) {
            nextState.acting_player_idx = activeIdx;
            // Set active highlight on players
            for (size_t i = 0; i < nextState.players.size(); ++i) {
                nextState.players[i].active = ((int)i == activeIdx);
            }
            states.push_back(nextState);
        }
    }
    
    return states;
}

HandMetadata HandHistoryParser::extractMetadata(const QString& historyText) {
    HandMetadata md;
    QStringList lines = historyText.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    if (lines.isEmpty()) return md;

    QString firstLine = lines.first().trimmed();
    
    // PokerStars format parsing
    // "PokerStars Hand #261086621937: Tournament #4006022899, $0.98+$0.12 USD Hold'em No Limit - Level XIV (300/600) - 2026/06/09 12:50:57 MT [2026/06/09 14:50:57 ET]"
    QRegularExpression psRegex("^(PokerStars) Hand #(\\d+): (?:Tournament #(\\d+), )?(.+?) - Level.+ - (.+) \\[");
    QRegularExpressionMatch psMatch = psRegex.match(firstLine);
    if (psMatch.hasMatch()) {
        md.site = psMatch.captured(1);
        md.hand_id = psMatch.captured(2);
        md.tournament_id = psMatch.captured(3);
        md.stakes = psMatch.captured(4);
        md.date = psMatch.captured(5);
        return md;
    }

    // Attempt to parse cash game format if tournament failed
    // "PokerStars Hand #261086621937:  Hold'em No Limit ($0.05/$0.10 USD) - 2026/06/09 12:50:57 MT"
    QRegularExpression psCashRegex("^(PokerStars) Hand #(\\d+):  (.+?) - (.+) \\[");
    QRegularExpressionMatch psCashMatch = psCashRegex.match(firstLine);
    if (psCashMatch.hasMatch()) {
        md.site = psCashMatch.captured(1);
        md.hand_id = psCashMatch.captured(2);
        md.stakes = psCashMatch.captured(3);
        md.date = psCashMatch.captured(4);
        return md;
    }

    return md;
}
