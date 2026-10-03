#include "include/preflop/PreflopRule.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>

PreflopRule::PreflopRule(
    int num_players,
    const vector<float>& player_stacks,
    float small_blind,
    float big_blind,
    float ante
) {
    this->num_players = num_players;
    this->player_stacks = player_stacks;
    this->small_blind = small_blind;
    this->big_blind = big_blind;
    this->ante = ante;

    this->player_commits = vector<float>(num_players, 0.0f);
    this->active_players = vector<bool>(num_players, true);

    // Standard positions preflop: UTG acts first.
    // If num_players >= 2:
    // Small Blind is posted by player (num_players - 2)
    // Big Blind is posted by player (num_players - 1)
    // UTG is player 0 (acts first)
    if (num_players >= 2) {
        int sb_idx = num_players - 2;
        int bb_idx = num_players - 1;
        player_commits[sb_idx] = small_blind;
        player_commits[bb_idx] = big_blind;
        current_player = 0; // UTG acts first
    } else {
        // Heads-up: SB acts first (player 0), BB is player 1
        player_commits[0] = small_blind;
        player_commits[1] = big_blind;
        current_player = 0;
    }
}

float PreflopRule::get_pot() const {
    float pot = 0.0f;
    for (float commit : player_commits) {
        pot += commit;
    }
    pot += num_players * ante;
    return pot;
}

float PreflopRule::get_max_commit() const {
    float max_c = 0.0f;
    for (int i = 0; i < num_players; ++i) {
        if (active_players[i]) {
            max_c = max(max_c, player_commits[i]);
        }
    }
    return max_c;
}

float PreflopRule::get_call_amount() const {
    float max_commit = get_max_commit();
    float current_commit = player_commits[current_player];
    float needed = max_commit - current_commit;
    
    // Can only call up to the player's remaining stack
    float remaining = get_remaining_stack(current_player);
    return min(needed, remaining);
}

float PreflopRule::get_remaining_stack(int player_idx) const {
    // Stack remaining is starting stack minus ante minus current commits
    float remaining = player_stacks[player_idx] - player_commits[player_idx] - ante;
    return max(0.0f, remaining);
}

bool PreflopRule::is_terminal() const {
    // 1. If only one player is active, the game is over.
    int active_count = 0;
    for (bool active : active_players) {
        if (active) active_count++;
    }
    if (active_count <= 1) return true;

    // 2. If all active players have matched the maximum commitment (and everyone has acted), the round is over.
    // In preflop, BB (player num_players-1) acts last if there is no raise.
    // Let's check if all active players' commitments are equal to the max commitment,
    // and if we've cycled back to a state where no one has a pending option.
    // Typically, the round ends when the next_player's call amount is 0 and they are not the BB with an option.
    // A simple way to track this is to check if the next player to act has already matched the max commitment
    // and is not the big blind with an option to check/raise.
    float max_commit = get_max_commit();
    int next_p = current_player;
    
    // If the next player's commit equals the max commit and they have already acted, the round is done.
    // For BB, if max_commit == big_blind and next_p == num_players - 1 (BB), the BB still has an option.
    if (player_commits[next_p] == max_commit) {
        bool is_bb = (next_p == num_players - 1);
        if (is_bb && max_commit == big_blind) {
            // BB has option, round not terminal yet
            return false;
        }
        return true;
    }

    return false;
}

int PreflopRule::get_winner() const {
    int active_count = 0;
    int winner_idx = -1;
    for (int i = 0; i < num_players; ++i) {
        if (active_players[i]) {
            active_count++;
            winner_idx = i;
        }
    }
    if (active_count == 1) return winner_idx;
    return -1;
}

void PreflopRule::advance_player() {
    do {
        current_player = (current_player + 1) % num_players;
    } while (!active_players[current_player]);
}

void PreflopRule::apply_fold() {
    active_players[current_player] = false;
    advance_player();
}

void PreflopRule::apply_call() {
    float call_amt = get_call_amount();
    player_commits[current_player] += call_amt;
    advance_player();
}

void PreflopRule::apply_raise(float target_commit) {
    float current_commit = player_commits[current_player];
    float needed = target_commit - current_commit;
    float remaining = get_remaining_stack(current_player);
    
    float actual_raise = min(needed, remaining);
    player_commits[current_player] += actual_raise;
    advance_player();
}
