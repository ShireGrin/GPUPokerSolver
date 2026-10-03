#!/usr/bin/env python3
"""
Hand History Parser - Parses PokerStars tournament hand histories into structured data.
"""

import re
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class PlayerInfo:
    name: str
    seat: int
    stack: float
    cards: list = field(default_factory=list)
    position: str = ""  # BTN, SB, BB, UTG, MP, CO, etc.
    is_hero: bool = False


@dataclass
class Action:
    player: str
    action: str  # fold, check, call, bet, raise, allin
    amount: float = 0.0
    is_allin: bool = False


@dataclass
class ParsedHand:
    hand_id: str = ""
    tournament_id: str = ""
    buy_in: float = 0.0
    game_type: str = "Hold'em No Limit"
    level: str = ""
    blinds: dict = field(default_factory=dict)  # sb, bb, ante
    date: str = ""
    table_name: str = ""
    max_players: int = 0
    button_seat: int = 0

    players: list = field(default_factory=list)
    hero_name: str = ""

    board: list = field(default_factory=list)  # full board cards
    board_flop: list = field(default_factory=list)
    board_turn: str = ""
    board_river: str = ""

    actions_preflop: list = field(default_factory=list)
    actions_flop: list = field(default_factory=list)
    actions_turn: list = field(default_factory=list)
    actions_river: list = field(default_factory=list)

    pot_total: float = 0.0
    rake: float = 0.0

    winners: list = field(default_factory=list)  # [{name, amount}]
    showdown_hands: dict = field(default_factory=dict)  # {name: [card1, card2]}

    raw_text: str = ""

    def get_hero(self) -> Optional[PlayerInfo]:
        for p in self.players:
            if p.is_hero:
                return p
        return None

    def get_player(self, name) -> Optional[PlayerInfo]:
        for p in self.players:
            if p.name == name:
                return p
        return None

    def get_actions_for_street(self, street):
        return {
            "preflop": self.actions_preflop,
            "flop": self.actions_flop,
            "turn": self.actions_turn,
            "river": self.actions_river,
        }.get(street, [])

    def get_hero_actions(self):
        """Get all actions taken by the hero, organized by street."""
        hero = self.get_hero()
        if not hero:
            return {}
        result = {}
        for street in ["preflop", "flop", "turn", "river"]:
            actions = self.get_actions_for_street(street)
            hero_actions = [a for a in actions if a.player == hero.name]
            if hero_actions:
                result[street] = hero_actions
        return result

    @property
    def effective_stack(self):
        """Return the effective stack (smallest stack among active players at start)."""
        if len(self.players) < 2:
            return 0
        stacks = sorted(p.stack for p in self.players)
        return stacks[-2] if len(stacks) >= 2 else stacks[0]

    @property
    def effective_stack_bb(self):
        """Effective stack in big blinds."""
        bb = self.blinds.get("bb", 1)
        if bb == 0:
            return 0
        return round(self.effective_stack / bb, 1)

    def summary(self):
        """One-line summary of the hand."""
        hero = self.get_hero()
        hero_cards = " ".join(hero.cards) if hero and hero.cards else "??"
        board_str = " ".join(self.board) if self.board else "no board"
        hero_won = any(w["name"] == hero.name for w in self.winners) if hero else False
        won_str = "✅ WON" if hero_won else "❌ LOST"
        return (
            f"#{self.hand_id} | {hero_cards} | {board_str} | "
            f"pot={self.pot_total} | {self.effective_stack_bb}bb eff | {won_str}"
        )


def parse_card(card_str):
    """Normalize a card string like 'Tc' -> 'Tc'."""
    return card_str.strip()


def parse_hand_history(text, hero_name=None):
    """
    Parse a PokerStars hand history text into a ParsedHand object.

    Args:
        text: Raw hand history text
        hero_name: If provided, marks this player as hero. If None,
                   the player whose cards are "Dealt to" is the hero.
    """
    hand = ParsedHand(raw_text=text)
    lines = text.strip().split("\n")

    current_street = None  # None = header, "preflop", "flop", "turn", "river", "showdown", "summary"

    for line in lines:
        line = line.strip()
        if not line:
            continue

        # ── Header line ────────────────────────────────────────
        m = re.match(
            r"PokerStars Hand #(\d+): Tournament #(\d+), "
            r"\$?([\d.]+)\+\$?([\d.]+)",
            line,
        )
        if m:
            hand.hand_id = m.group(1)
            hand.tournament_id = m.group(2)
            hand.buy_in = float(m.group(3)) + float(m.group(4))
            # Extract level and blinds
            level_m = re.search(r"Level (\S+) \((\d+)/(\d+)\)", line)
            if level_m:
                hand.level = level_m.group(1)
                hand.blinds["sb"] = float(level_m.group(2))
                hand.blinds["bb"] = float(level_m.group(3))
            # Extract date
            date_m = re.search(r"(\d{4}/\d{2}/\d{2} \d+:\d+:\d+)", line)
            if date_m:
                hand.date = date_m.group(1)
            continue

        # ── Table line ─────────────────────────────────────────
        m = re.match(r"Table '(.+)' (\d+)-max Seat #(\d+) is the button", line)
        if m:
            hand.table_name = m.group(1)
            hand.max_players = int(m.group(2))
            hand.button_seat = int(m.group(3))
            continue

        # ── Seat line ──────────────────────────────────────────
        m = re.match(r"Seat (\d+): (.+?) \((\d+) in chips\)", line)
        if m:
            player = PlayerInfo(
                name=m.group(2),
                seat=int(m.group(1)),
                stack=float(m.group(3)),
            )
            if hero_name and player.name == hero_name:
                player.is_hero = True
            hand.players.append(player)
            continue

        # ── Ante posting ───────────────────────────────────────
        m = re.match(r"(.+?): posts the ante (\d+)", line)
        if m:
            hand.blinds["ante"] = float(m.group(2))
            continue

        # ── Blind posting ──────────────────────────────────────
        m = re.match(r"(.+?): posts small blind (\d+)", line)
        if m:
            continue
        m = re.match(r"(.+?): posts big blind (\d+)", line)
        if m:
            continue

        # ── Hole cards ─────────────────────────────────────────
        if line == "*** HOLE CARDS ***":
            current_street = "preflop"
            continue

        m = re.match(r"Dealt to (.+?) \[(.+?)\]", line)
        if m:
            player_name = m.group(1)
            cards = m.group(2).split()
            player = hand.get_player(player_name)
            if player:
                player.cards = cards
                if hero_name is None:
                    player.is_hero = True
                    hand.hero_name = player_name
                elif player_name == hero_name:
                    hand.hero_name = hero_name
            continue

        # ── Street markers ─────────────────────────────────────
        m = re.match(r"\*\*\* FLOP \*\*\* \[(.+?)\]", line)
        if m:
            current_street = "flop"
            cards = m.group(1).split()
            hand.board_flop = cards
            hand.board = list(cards)
            continue

        m = re.match(r"\*\*\* TURN \*\*\* \[.+?\] \[(.+?)\]", line)
        if m:
            current_street = "turn"
            hand.board_turn = m.group(1)
            hand.board = hand.board_flop + [hand.board_turn]
            continue

        m = re.match(r"\*\*\* RIVER \*\*\* \[.+?\] \[(.+?)\]", line)
        if m:
            current_street = "river"
            hand.board_river = m.group(1)
            hand.board = hand.board_flop + [hand.board_turn, hand.board_river]
            continue

        if line == "*** SHOW DOWN ***":
            current_street = "showdown"
            continue

        if line == "*** SUMMARY ***":
            current_street = "summary"
            continue

        # ── Actions ────────────────────────────────────────────
        if current_street in ("preflop", "flop", "turn", "river"):
            # Capture non-showdown wins (e.g. everyone folded)
            m = re.match(r"(.+?) collected (\d+) from pot", line)
            if m:
                hand.winners.append({
                    "name": m.group(1),
                    "amount": float(m.group(2)),
                })
                continue

            action = _parse_action(line)
            if action:
                target = {
                    "preflop": hand.actions_preflop,
                    "flop": hand.actions_flop,
                    "turn": hand.actions_turn,
                    "river": hand.actions_river,
                }.get(current_street)
                if target is not None:
                    target.append(action)
                continue

        # ── Showdown reveals ───────────────────────────────────
        if current_street == "showdown":
            m = re.match(r"(.+?): shows \[(.+?)\]", line)
            if m:
                name = m.group(1)
                cards = m.group(2).split()
                hand.showdown_hands[name] = cards
                player = hand.get_player(name)
                if player and not player.cards:
                    player.cards = cards
                continue

            m = re.match(r"(.+?) collected (\d+) from pot", line)
            if m:
                hand.winners.append({
                    "name": m.group(1),
                    "amount": float(m.group(2)),
                })
                continue

        # ── Summary section ────────────────────────────────────
        if current_street == "summary":
            m = re.match(r"Total pot (\d+)", line)
            if m:
                hand.pot_total = float(m.group(1))
                rake_m = re.search(r"Rake (\d+)", line)
                if rake_m:
                    hand.rake = float(rake_m.group(1))
                continue

            # Winner from non-showdown (someone collected without showdown)
            m = re.match(r"(.+?) collected (\d+) from pot", line)
            if m and not hand.winners:
                hand.winners.append({
                    "name": m.group(1),
                    "amount": float(m.group(2)),
                })

    # Assign positions based on button seat
    _assign_positions(hand)

    # Set hero name if we found one
    hero = hand.get_hero()
    if hero:
        hand.hero_name = hero.name

    return hand


def _parse_action(line):
    """Parse a single action line and return an Action, or None."""
    # Fold
    m = re.match(r"(.+?): folds", line)
    if m:
        return Action(player=m.group(1), action="fold")

    # Check
    m = re.match(r"(.+?): checks", line)
    if m:
        return Action(player=m.group(1), action="check")

    # Call
    m = re.match(r"(.+?): calls (\d+)(.*)", line)
    if m:
        is_allin = "all-in" in m.group(3)
        return Action(
            player=m.group(1), action="call",
            amount=float(m.group(2)), is_allin=is_allin,
        )

    # Bet
    m = re.match(r"(.+?): bets (\d+)(.*)", line)
    if m:
        is_allin = "all-in" in m.group(3)
        return Action(
            player=m.group(1), action="bet",
            amount=float(m.group(2)), is_allin=is_allin,
        )

    # Raise
    m = re.match(r"(.+?): raises (\d+) to (\d+)(.*)", line)
    if m:
        is_allin = "all-in" in m.group(4)
        return Action(
            player=m.group(1), action="raise",
            amount=float(m.group(3)), is_allin=is_allin,
        )

    # Uncalled bet
    m = re.match(r"Uncalled bet \((\d+)\) returned to (.+)", line)
    if m:
        return None  # Not a player action

    # Collected (non-showdown win during action)
    m = re.match(r"(.+?) collected (\d+) from pot", line)
    if m:
        return None  # This is a result, not an action

    return None


def _assign_positions(hand):
    """Assign position labels based on button seat and player count."""
    if not hand.players:
        return

    n = len(hand.players)
    # Sort players by seat
    seated = sorted(hand.players, key=lambda p: p.seat)

    # Find button index
    btn_idx = None
    for i, p in enumerate(seated):
        if p.seat == hand.button_seat:
            btn_idx = i
            break

    if btn_idx is None:
        return

    # Assign positions going clockwise from button
    positions_2 = ["SB", "BB"]
    positions_3 = ["BTN", "SB", "BB"]
    positions_4 = ["BTN", "CO", "SB", "BB"]
    positions_5 = ["BTN", "CO", "UTG", "SB", "BB"]
    positions_6 = ["BTN", "CO", "MP", "UTG", "SB", "BB"]
    positions_7 = ["BTN", "CO", "MP", "UTG+1", "UTG", "SB", "BB"]
    positions_8 = ["BTN", "CO", "HJ", "MP", "UTG+1", "UTG", "SB", "BB"]
    positions_9 = ["BTN", "CO", "HJ", "LJ", "MP", "UTG+1", "UTG", "SB", "BB"]

    pos_map = {
        2: positions_2, 3: positions_3, 4: positions_4,
        5: positions_5, 6: positions_6, 7: positions_7,
        8: positions_8, 9: positions_9,
    }

    positions = pos_map.get(n, positions_9[:n])

    for i in range(n):
        player_idx = (btn_idx + i) % n
        if i < len(positions):
            seated[player_idx].position = positions[i]


# ── Standalone test ───────────────────────────────────────────────
if __name__ == "__main__":
    # Test with a sample hand
    sample = """PokerStars Hand #260802747677: Tournament #4000314158, $0.45+$0.05 USD Hold'em No Limit - Level XIII (600/1200) - 2026/05/14 7:04:16 MT [2026/05/14 9:04:16 ET]
Table '4000314158 3' 8-max Seat #6 is the button
Seat 1: ShireGrin2 (38976 in chips)
Seat 6: Frannk Lucas (9024 in chips)
ShireGrin2: posts the ante 150
Frannk Lucas: posts the ante 150
Frannk Lucas: posts small blind 600
ShireGrin2: posts big blind 1200
*** HOLE CARDS ***
Dealt to ShireGrin2 [Qh Kh]
Frannk Lucas: raises 7674 to 8874 and is all-in
ShireGrin2: calls 7674
*** FLOP *** [Tc Qs 4c]
*** TURN *** [Tc Qs 4c] [9s]
*** RIVER *** [Tc Qs 4c 9s] [Ts]
*** SHOW DOWN ***
ShireGrin2: shows [Qh Kh] (two pair, Queens and Tens)
Frannk Lucas: shows [6h Ad] (a pair of Tens)
ShireGrin2 collected 18048 from pot
Frannk Lucas finished the tournament in 2nd place and received $3.70.
ShireGrin2 wins the tournament and receives $5.67 - congratulations!
*** SUMMARY ***
Total pot 18048 | Rake 0
Board [Tc Qs 4c 9s Ts]
Seat 1: ShireGrin2 (big blind) showed [Qh Kh] and won (18048) with two pair, Queens and Tens
Seat 6: Frannk Lucas (button) (small blind) showed [6h Ad] and lost with a pair of Tens"""

    hand = parse_hand_history(sample, hero_name="ShireGrin2")
    print(hand.summary())
    print(f"\nHand ID: {hand.hand_id}")
    print(f"Tournament: #{hand.tournament_id}")
    print(f"Blinds: {hand.blinds}")
    print(f"Board: {hand.board}")
    print(f"Players:")
    for p in hand.players:
        print(f"  {p.position:5s} {p.name:20s} stack={p.stack} cards={p.cards}")
    print(f"\nPreflop actions:")
    for a in hand.actions_preflop:
        print(f"  {a.player}: {a.action} {a.amount if a.amount else ''} {'(all-in)' if a.is_allin else ''}")
    print(f"\nShowdown: {hand.showdown_hands}")
    print(f"Winners: {hand.winners}")
    print(f"Effective stack: {hand.effective_stack_bb}bb")
    print(f"\nHero actions: {hand.get_hero_actions()}")
