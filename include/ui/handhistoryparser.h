#ifndef HANDHISTORYPARSER_H
#define HANDHISTORYPARSER_H

#include <QString>
#include <QStringList>
#include <vector>
#include <map>
#include <memory>
#include "include/ui/pt4importdialog.h" // For PreflopPlayer structure

struct PlayerState {
    int seat = -1;
    QString name;
    QString position_label; // "BTN", "SB", "BB", "UTG", "CO", etc.
    float stack = 0.0f; // Stack size at start of current state
    float initial_stack = 0.0f; // Stack size at start of hand
    QString card1;
    QString card2;
    float current_bet = 0.0f; // Bet size on current street
    bool folded = false;
    bool all_in = false;
    bool active = false; // True if it's this player's turn to act
    QString cluster_name; // Associated cluster type (e.g. "TAG", "LAG")
};

struct HandGameState {
    std::vector<PlayerState> players;
    QStringList board_cards; // List of board cards, e.g., ["Ac", "Kd", "Qh"]
    float pot = 0.0f;
    float sb_size = 0.0f;
    float bb_size = 1.0f; // Big blind size in chips/cents
    float ante_size = 0.0f;
    QString current_street; // "Preflop", "Flop", "Turn", "River", "Showdown", "Summary"
    QString action_description; // Descriptive text (e.g. "garikban777 raises to 150")
    int acting_player_idx = -1; // Index of the player who performed the action
};

struct HandMetadata {
    QString site;
    QString hand_id;
    QString tournament_id;
    QString stakes;
    QString date;
};

class HandHistoryParser {
public:
    // Main entry point to parse a raw PokerStars hand history text
    static std::vector<HandGameState> parseHistory(const QString& historyText, const std::vector<PreflopPlayer>& dbPlayers, float default_bb = 100.0f, float default_ante = 0.0f);
    
    // Extract metadata from a raw PokerStars hand history text
    static HandMetadata extractMetadata(const QString& historyText);
};

#endif // HANDHISTORYPARSER_H
