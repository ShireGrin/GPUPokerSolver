#ifndef TEXASSOLVER_PREFLOPRULE_H
#define TEXASSOLVER_PREFLOPRULE_H

#include <vector>
#include <string>
#include "include/Deck.h"

using namespace std;

class PreflopRule {
public:
    int num_players;
    vector<float> player_commits; // Total chips committed by each player in the hand
    vector<float> player_stacks;  // Starting stack sizes of each player
    vector<bool> active_players;   // Whether each player is still active (has not folded)
    
    float small_blind;
    float big_blind;
    float ante;                  // Ante size per player (crucial for MTTs/SNGs)
    
    int current_player;          // Player index whose turn it is to act (0 to num_players - 1)
    
    PreflopRule(
        int num_players,
        const vector<float>& player_stacks,
        float small_blind,
        float big_blind,
        float ante = 0.0f
    );

    // Compute the total chips in the pot (commits + antes)
    float get_pot() const;

    // Get the maximum commitment on the current street
    float get_max_commit() const;

    // Get the amount needed for the current player to call
    float get_call_amount() const;

    // Get the remaining stack of a player
    float get_remaining_stack(int player_idx) const;

    // Check if the game is over (e.g. only one player remains)
    bool is_terminal() const;

    // Get the index of the last remaining active player (if only one remains)
    int get_winner() const;

    // Advance to the next active player
    void advance_player();

    // Perform an action: fold, call/check, or raise to a specific target commit
    void apply_fold();
    void apply_call();
    void apply_raise(float target_commit);
};

#endif // TEXASSOLVER_PREFLOPRULE_H
