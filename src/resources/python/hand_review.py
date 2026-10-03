#!/usr/bin/env python3
"""
Hand Review Mode - Review your poker hands against GTO solutions.

Workflow:
1. Lists your recent hands from PT4
2. You pick a hand to review
3. For each decision, shows what GTO recommends vs what you did
4. Optionally pipes the hand into the solver for full analysis

Usage:
    PT4_PASSWORD="your_password" python3 hand_review.py
    PT4_PASSWORD="your_password" python3 hand_review.py --last 5
    PT4_PASSWORD="your_password" python3 hand_review.py --hand 260802727036
"""

import sys
import os
import argparse

# Add parent dir to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pt4_bridge import PT4Bridge
from hand_parser import parse_hand_history


# ── Formatting helpers ─────────────────────────────────────────────

COLORS = {
    "reset": "\033[0m",
    "bold": "\033[1m",
    "red": "\033[91m",
    "green": "\033[92m",
    "yellow": "\033[93m",
    "blue": "\033[94m",
    "magenta": "\033[95m",
    "cyan": "\033[96m",
    "dim": "\033[2m",
}

def c(text, color):
    return f"{COLORS.get(color, '')}{text}{COLORS['reset']}"

def print_header(text):
    print(f"\n{c('═' * 60, 'dim')}")
    print(f"  {c(text, 'bold')}")
    print(f"{c('═' * 60, 'dim')}")

def print_card(cards):
    """Format cards with suit symbols and colors."""
    suit_map = {"h": c("♥", "red"), "d": c("♦", "blue"),
                "c": c("♣", "green"), "s": c("♠", "dim")}
    result = []
    for card in cards:
        rank = card[0]
        suit = suit_map.get(card[1], card[1])
        result.append(f"{c(rank, 'bold')}{suit}")
    return " ".join(result)


# ── Hand listing ───────────────────────────────────────────────────

def list_hands(pt4, player_name, n=20):
    """Display recent hands for selection."""
    hands = pt4.get_recent_hands(player_name, n)
    if not hands:
        print("No hands found.")
        return []

    print_header(f"Recent Hands for {player_name}")
    print(f"  {'#':>3}  {'Hand ID':>15}  {'Date':>19}  {'Pot':>8}  {'Won':>8}  {'Result'}")
    print(f"  {'─'*3}  {'─'*15}  {'─'*19}  {'─'*8}  {'─'*8}  {'─'*6}")

    parsed_hands = []
    for i, h in enumerate(hands):
        history = pt4.get_hand_history(h["id_hand"])
        if not history:
            continue
        parsed = parse_hand_history(history, hero_name=player_name)
        parsed_hands.append((h, parsed))

        hero = parsed.get_hero()
        won = h.get("flg_won_hand", False)
        amt = h.get("amt_won", 0) or 0
        result = c("WIN", "green") if won else c("LOSS", "red")
        cards = print_card(hero.cards) if hero and hero.cards else "??"

        has_postflop = "📊" if parsed.actions_flop else "  "

        print(
            f"  {i+1:>3}  {h['hand_no']:>15}  {str(h['date_played']):>19}  "
            f"{float(h.get('amt_pot', 0)):>8.0f}  {float(amt):>8.0f}  "
            f"{result}  {cards}  {has_postflop}"
        )

    return parsed_hands


# ── Hand Review ────────────────────────────────────────────────────

def review_hand(pt4, parsed, player_name):
    """Review a single hand in detail."""
    hero = parsed.get_hero()
    if not hero:
        print("Could not find hero in this hand.")
        return

    print_header(f"Hand #{parsed.hand_id}")

    # Game info
    bb = parsed.blinds.get("bb", 0)
    sb = parsed.blinds.get("sb", 0)
    ante = parsed.blinds.get("ante", 0)
    print(f"  Tournament: #{parsed.tournament_id}")
    print(f"  Blinds: {sb}/{bb} (ante {ante})")
    print(f"  Players: {len(parsed.players)}")
    print()

    # Player stacks
    print(f"  {'Pos':>5}  {'Player':25s}  {'Stack':>8}  {'BB':>6}  Cards")
    print(f"  {'─'*5}  {'─'*25}  {'─'*8}  {'─'*6}  {'─'*10}")
    for p in sorted(parsed.players, key=lambda x: x.seat):
        stack_bb = round(p.stack / bb, 1) if bb > 0 else 0
        cards = print_card(p.cards) if p.cards else c("hidden", "dim")
        marker = c(" ← YOU", "cyan") if p.is_hero else ""
        print(f"  {p.position:>5}  {p.name:25s}  {p.stack:>8.0f}  {stack_bb:>6.1f}  {cards}{marker}")

    print(f"\n  Effective stack: {c(f'{parsed.effective_stack_bb}bb', 'bold')}")

    # Board
    if parsed.board:
        print(f"\n  Board: {print_card(parsed.board)}")

    # Street-by-street action replay
    hero_decision_count = 0
    streets = [
        ("PREFLOP", parsed.actions_preflop, []),
        ("FLOP", parsed.actions_flop, parsed.board_flop),
        ("TURN", parsed.actions_turn, parsed.board_flop + [parsed.board_turn] if parsed.board_turn else []),
        ("RIVER", parsed.actions_river, parsed.board if parsed.board_river else []),
    ]

    for street_name, actions, board_cards in streets:
        if not actions:
            continue

        print(f"\n  {c(f'── {street_name} ', 'bold')}", end="")
        if board_cards:
            print(f"{print_card(board_cards)}", end="")
        print(f" {c('─' * 30, 'dim')}")

        pot = _estimate_pot_at_street(parsed, street_name.lower())

        for action in actions:
            is_hero = action.player == hero.name
            player_str = c(action.player, "cyan") if is_hero else action.player

            action_str = action.action.upper()
            if action.amount:
                if bb > 0:
                    bb_amt = round(action.amount / bb, 1)
                    action_str += f" {action.amount:.0f} ({bb_amt}bb)"
                else:
                    action_str += f" {action.amount:.0f}"
            if action.is_allin:
                action_str += c(" ALL-IN", "red")

            if is_hero:
                hero_decision_count += 1
                marker = c(f"  ★ Decision #{hero_decision_count}", "yellow")
                print(f"    {player_str}: {c(action_str, 'bold')}{marker}")
            else:
                print(f"    {player_str}: {action_str}")

    # Result
    print(f"\n  {c('── RESULT ', 'bold')}{c('─' * 40, 'dim')}")
    if parsed.showdown_hands:
        for name, cards in parsed.showdown_hands.items():
            is_hero = name == hero.name
            marker = c(" ← YOU", "cyan") if is_hero else ""
            print(f"    {name}: {print_card(cards)}{marker}")

    for w in parsed.winners:
        is_hero = w["name"] == hero.name
        if is_hero:
            won_amt = w["amount"]
            print(f"    {c(f'You won {won_amt:.0f}', 'green')}")
        else:
            won_amt = w["amount"]
            print(f"    {w['name']} won {won_amt:.0f}")

    if not any(w["name"] == hero.name for w in parsed.winners):
        print(f"    {c('You lost this hand.', 'red')}")

    # Hero decision summary
    print(f"\n  {c('── YOUR DECISIONS ', 'bold')}{c('─' * 32, 'dim')}")
    hero_actions = parsed.get_hero_actions()
    if not hero_actions:
        print("    No decisions to review (folded preflop or walked).")
    else:
        for street, actions in hero_actions.items():
            for a in actions:
                amt_str = f" {a.amount:.0f}" if a.amount else ""
                allin_str = " (all-in)" if a.is_allin else ""
                print(f"    {street:>8}: {a.action.upper()}{amt_str}{allin_str}")

    # Solver setup hint (only for hands with postflop decisions)
    eff_at_flop = _estimate_stack_at_flop(parsed)
    if parsed.board_flop and eff_at_flop > 0:
        print(f"\n  {c('── SOLVER SETUP ', 'bold')}{c('─' * 34, 'dim')}")
        board_str = ",".join(parsed.board_flop)
        if parsed.board_turn:
            board_str += f",{parsed.board_turn}"
        if parsed.board_river:
            board_str += f",{parsed.board_river}"
        print(f"    Board: {board_str}")
        print(f"    Pot at flop: ~{_estimate_pot_at_street(parsed, 'flop'):.0f}")
        print(f"    Effective stack at flop: ~{eff_at_flop:.0f}")
        print()
        print(f"    {c('To analyze in solver:', 'dim')}")
        print(f"    set_board {board_str}")
        print(f"    set_pot {_estimate_pot_at_street(parsed, 'flop'):.0f}")
        print(f"    set_effective_stack {eff_at_flop:.0f}")
    elif parsed.board_flop and eff_at_flop <= 0:
        print(f"\n  {c('── ALL-IN PREFLOP ', 'yellow')}{c('─' * 32, 'dim')}")
        print(f"    No postflop decisions to analyze (all-in before the flop).")

    # Opponent stats
    print(f"\n  {c('── OPPONENT STATS ', 'bold')}{c('─' * 32, 'dim')}")
    for p in parsed.players:
        if p.name == hero.name:
            continue
        profile = pt4.get_player_profile(p.name)
        if profile and profile["stats"] and profile["stats"].get("hands", 0) > 5:
            s = profile["stats"]
            print(f"    {c(p.name, 'bold')}: {profile['profile']}")
            print(f"      Hands: {s['hands']} | VPIP: {s.get('vpip', '?')}% | PFR: {s.get('pfr', '?')}%")
            fcbet = s.get("fold_to_flop_cbet")
            cbet = s.get("flop_cbet")
            if fcbet is not None:
                print(f"      Fold to Cbet: {fcbet}% | Cbet: {cbet}%")
        else:
            print(f"    {p.name}: {c('Unknown (not enough data)', 'dim')}")


def _estimate_pot_at_street(parsed, street):
    """Rough estimate of pot size at the start of a given street."""
    bb = parsed.blinds.get("bb", 0)
    sb = parsed.blinds.get("sb", 0)
    ante = parsed.blinds.get("ante", 0)
    n = len(parsed.players)

    pot = sb + bb + (ante * n)

    if street == "preflop":
        return pot

    # Add preflop action
    for a in parsed.actions_preflop:
        if a.action in ("call", "bet", "raise"):
            pot += a.amount

    if street == "flop":
        return pot

    for a in parsed.actions_flop:
        if a.action in ("call", "bet", "raise"):
            pot += a.amount

    if street == "turn":
        return pot

    for a in parsed.actions_turn:
        if a.action in ("call", "bet", "raise"):
            pot += a.amount

    return pot


def _estimate_stack_at_flop(parsed):
    """Estimate the effective stack remaining at the flop."""
    hero = parsed.get_hero()
    if not hero:
        return 0

    ante = parsed.blinds.get("ante", 0)

    # Track the hero's total commitment in the preflop betting round.
    # "raises to X" means total round commitment is X (includes blind).
    # "calls X" means X is additional on top of current commitment.
    round_bet = 0
    if hero.position == "SB":
        round_bet = parsed.blinds.get("sb", 0)
    elif hero.position == "BB":
        round_bet = parsed.blinds.get("bb", 0)

    for a in parsed.actions_preflop:
        if a.player != hero.name:
            continue
        if a.action == "raise":
            # "raises to X" — X is the new total round commitment
            round_bet = a.amount
        elif a.action in ("call", "bet"):
            # call amount is additional
            round_bet += a.amount

    total_spent = ante + round_bet
    return max(0, hero.stack - total_spent)


# ── Main ───────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Hand Review Mode")
    parser.add_argument("--player", default="ShireGrin2", help="Your player name")
    parser.add_argument("--last", type=int, default=20, help="Number of recent hands to show")
    parser.add_argument("--hand", type=str, help="Review a specific hand ID directly")
    parser.add_argument("--postflop-only", action="store_true",
                       help="Only show hands with postflop action")
    args = parser.parse_args()

    with PT4Bridge() as pt4:
        if args.hand:
            # Try by hand_no first
            cur = pt4.conn.cursor()
            cur.execute(
                "SELECT id_hand FROM tourney_hand_summary WHERE hand_no = %s",
                (args.hand,),
            )
            row = cur.fetchone()
            if not row and args.hand.isdigit():
                # Try as id_hand
                cur.execute(
                    "SELECT id_hand FROM tourney_hand_summary WHERE id_hand = %s",
                    (int(args.hand),),
                )
                row = cur.fetchone()
            if not row:
                print(f"Hand {args.hand} not found.")
                return
            history = pt4.get_hand_history(row[0])
            if not history:
                print(f"No history for hand {args.hand}")
                return
            parsed = parse_hand_history(history, hero_name=args.player)
            review_hand(pt4, parsed, args.player)
            return

        # Interactive mode
        parsed_hands = list_hands(pt4, args.player, args.last)
        if not parsed_hands:
            return

        if args.postflop_only:
            parsed_hands = [(h, p) for h, p in parsed_hands if p.actions_flop]
            if not parsed_hands:
                print("No postflop hands found.")
                return

        print(f"\n  📊 = has postflop action (best for review)")
        print(f"\n  Enter hand number to review (1-{len(parsed_hands)}), or 'q' to quit:")

        while True:
            try:
                choice = input(f"  {c('>', 'cyan')} ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break

            if choice.lower() in ("q", "quit", "exit"):
                break

            try:
                idx = int(choice) - 1
                if 0 <= idx < len(parsed_hands):
                    h, parsed = parsed_hands[idx]
                    review_hand(pt4, parsed, args.player)
                    print(f"\n  Enter another hand number, or 'q' to quit:")
                else:
                    print(f"  Invalid choice. Enter 1-{len(parsed_hands)}")
            except ValueError:
                print(f"  Invalid input. Enter a number or 'q'")


if __name__ == "__main__":
    main()
