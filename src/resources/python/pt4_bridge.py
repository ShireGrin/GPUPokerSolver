#!/usr/bin/env python3
"""
PT4 Bridge - Connects to PokerTracker 4's PostgreSQL database.
Provides clean APIs for fetching player stats, hand histories, and profiles.
"""

import psycopg2
import psycopg2.extras
import os

# Default connection settings (Docker container pt4-database)
DB_CONFIG = {
    "host": os.environ.get("PT4_HOST", "localhost"),
    "port": int(os.environ.get("PT4_PORT", "5432")),
    "user": os.environ.get("PT4_USER", "postgres"),
    "password": os.environ.get("PT4_PASSWORD", "dbpass"),
    "dbname": os.environ.get("PT4_DBNAME", "PT4"),
}


class PT4Bridge:
    """Interface to PokerTracker 4's PostgreSQL database."""

    def __init__(self, config=None):
        self.config = config or DB_CONFIG
        self.conn = None

    def connect(self):
        """Open a database connection."""
        self.conn = psycopg2.connect(**self.config)
        return self

    def close(self):
        """Close the database connection."""
        if self.conn:
            self.conn.close()
            self.conn = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    # ── Player Lookup ──────────────────────────────────────────────

    def find_player(self, name):
        """Find a player by name (case-insensitive partial match)."""
        cur = self.conn.cursor(cursor_factory=psycopg2.extras.DictCursor)
        cur.execute(
            "SELECT id_player, player_name FROM player "
            "WHERE player_name ILIKE %s ORDER BY player_name",
            (f"%{name}%",),
        )
        return [dict(r) for r in cur.fetchall()]

    def get_player_id(self, name):
        """Get exact player ID by name."""
        cur = self.conn.cursor()
        cur.execute(
            "SELECT id_player FROM player WHERE player_name = %s", (name,)
        )
        row = cur.fetchone()
        return row[0] if row else None

    # ── Aggregate Stats (tourney_cache) ────────────────────────────

    def get_player_stats(self, player_name):
        """
        Get aggregate stats for a player from tourney_cache.
        Returns a dict with human-readable stat names and percentages.
        """
        pid = self.get_player_id(player_name)
        if pid is None:
            # Try partial match
            matches = self.find_player(player_name)
            if not matches:
                return None
            pid = matches[0]["id_player"]
            player_name = matches[0]["player_name"]

        cur = self.conn.cursor(cursor_factory=psycopg2.extras.DictCursor)
        cur.execute(
            """
            SELECT
                cnt_hands,
                cnt_vpip,
                cnt_pfr,
                cnt_pfr_opp,
                cnt_p_3bet,
                cnt_p_3bet_opp,
                cnt_p_fold,
                cnt_steal_att,
                cnt_steal_opp,
                cnt_steal_def_action_fold,
                cnt_steal_def_opp,
                cnt_f_cbet,
                cnt_f_cbet_opp,
                cnt_f_cbet_def_action_fold,
                cnt_f_cbet_def_opp,
                cnt_f_fold,
                cnt_f_saw_won,
                cnt_t_cbet,
                cnt_t_cbet_opp,
                cnt_t_cbet_def_action_fold,
                cnt_t_cbet_def_opp,
                cnt_r_cbet,
                cnt_r_cbet_opp,
                cnt_r_cbet_def_action_fold,
                cnt_r_cbet_def_opp,
                cnt_won_hand,
                cnt_wtsd_won,
                amt_bb_won,
                cnt_hands_won
            FROM tourney_cache
            WHERE id_player = %s
            """,
            (pid,),
        )
        rows = cur.fetchall()
        if not rows:
            return {"player_name": player_name, "id_player": pid, "hands": 0}

        # Sum across all cache rows (there may be multiple game types)
        totals = {}
        for row in rows:
            for key in row.keys():
                val = row[key]
                if val is None:
                    val = 0
                totals[key] = totals.get(key, 0) + float(val)

        hands = totals.get("cnt_hands", 0)
        if hands == 0:
            return {"player_name": player_name, "id_player": pid, "hands": 0}

        def pct(num_key, denom_key):
            d = totals.get(denom_key, 0)
            if d == 0:
                return None
            return round(100.0 * totals.get(num_key, 0) / d, 1)

        stats = {
            "player_name": player_name,
            "id_player": pid,
            "hands": int(hands),
            "vpip": pct("cnt_vpip", "cnt_hands"),
            "pfr": pct("cnt_pfr", "cnt_pfr_opp"),
            "3bet": pct("cnt_p_3bet", "cnt_p_3bet_opp"),
            "fold_preflop": pct("cnt_p_fold", "cnt_hands"),
            "steal_attempt": pct("cnt_steal_att", "cnt_steal_opp"),
            "fold_to_steal": pct("cnt_steal_def_action_fold", "cnt_steal_def_opp"),
            "flop_cbet": pct("cnt_f_cbet", "cnt_f_cbet_opp"),
            "fold_to_flop_cbet": pct("cnt_f_cbet_def_action_fold", "cnt_f_cbet_def_opp"),
            "turn_cbet": pct("cnt_t_cbet", "cnt_t_cbet_opp"),
            "fold_to_turn_cbet": pct("cnt_t_cbet_def_action_fold", "cnt_t_cbet_def_opp"),
            "river_cbet": pct("cnt_r_cbet", "cnt_r_cbet_opp"),
            "fold_to_river_cbet": pct("cnt_r_cbet_def_action_fold", "cnt_r_cbet_def_opp"),
            "win_rate": pct("cnt_won_hand", "cnt_hands"),
            "bb_won": round(totals.get("amt_bb_won", 0), 1),
        }
        return stats

    # ── Player Profile Classification ──────────────────────────────

    def get_player_profile(self, player_name):
        """
        Classify a player based on their stats.
        Returns profile type and the raw stats.
        """
        stats = self.get_player_stats(player_name)
        if not stats or stats["hands"] < 10:
            return {"profile": "Unknown (insufficient data)", "stats": stats}

        vpip = stats.get("vpip") or 0
        pfr = stats.get("pfr") or 0

        # Classification based on VPIP and PFR
        if vpip > 45 and pfr < 12:
            profile = "Calling Station"
        elif vpip > 45 and pfr > 25:
            profile = "Maniac"
        elif vpip > 30 and pfr > 20:
            profile = "LAG (Loose-Aggressive)"
        elif vpip > 30:
            profile = "LP (Loose-Passive / Fish)"
        elif vpip > 20 and pfr > 15:
            profile = "TAG (Tight-Aggressive)"
        elif vpip < 16 and pfr < 14:
            profile = "Nit"
        else:
            profile = "Regular"

        return {"profile": profile, "stats": stats}

    # ── Hand Histories ─────────────────────────────────────────────

    def get_recent_hands(self, player_name=None, n=20):
        """
        Get the N most recent hand IDs and basic info for a player.
        If player_name is None, returns all hands.
        """
        cur = self.conn.cursor(cursor_factory=psycopg2.extras.DictCursor)

        if player_name:
            pid = self.get_player_id(player_name)
            if pid is None:
                return []
            cur.execute(
                """
                SELECT s.id_hand, s.hand_no, s.date_played, s.cnt_players,
                       s.amt_pot, s.card_1, s.card_2, s.card_3, s.card_4, s.card_5,
                       ps.id_holecard, ps.amt_won, ps.flg_vpip, ps.flg_showdown,
                       ps.flg_won_hand, ps.position
                FROM tourney_hand_summary s
                JOIN tourney_hand_player_statistics ps ON s.id_hand = ps.id_hand
                WHERE ps.id_player = %s
                ORDER BY s.date_played DESC
                LIMIT %s
                """,
                (pid, n),
            )
        else:
            cur.execute(
                """
                SELECT id_hand, hand_no, date_played, cnt_players, amt_pot,
                       card_1, card_2, card_3, card_4, card_5
                FROM tourney_hand_summary
                ORDER BY date_played DESC
                LIMIT %s
                """,
                (n,),
            )

        return [dict(r) for r in cur.fetchall()]

    def get_hand_history(self, hand_id):
        """Get the raw hand history text for a given hand ID."""
        cur = self.conn.cursor()
        cur.execute(
            "SELECT history FROM tourney_hand_histories WHERE id_hand = %s",
            (hand_id,),
        )
        row = cur.fetchone()
        return row[0] if row else None

    def get_hand_with_stats(self, hand_id):
        """Get hand history text plus per-player stats for a hand."""
        history = self.get_hand_history(hand_id)

        cur = self.conn.cursor(cursor_factory=psycopg2.extras.DictCursor)
        cur.execute(
            """
            SELECT ps.*, p.player_name,
                   lh.description as holecard_desc
            FROM tourney_hand_player_statistics ps
            JOIN player p ON ps.id_player = p.id_player
            LEFT JOIN lookup_hole_cards lh ON ps.id_holecard = lh.id_holecard
            WHERE ps.id_hand = %s
            """,
            (hand_id,),
        )
        player_stats = [dict(r) for r in cur.fetchall()]

        return {"history": history, "player_stats": player_stats}

    # ── Opponent Frequency Stats (for node locking) ────────────────

    def get_opponent_frequencies(self, player_name):
        """
        Get detailed per-street action frequencies for a player.
        These are the raw numbers needed to generate node lock CSVs.
        """
        stats = self.get_player_stats(player_name)
        if not stats or stats["hands"] < 10:
            return None

        return {
            "player_name": stats["player_name"],
            "hands": stats["hands"],
            "preflop": {
                "vpip_pct": stats.get("vpip"),
                "pfr_pct": stats.get("pfr"),
                "3bet_pct": stats.get("3bet"),
                "fold_pct": stats.get("fold_preflop"),
                "steal_pct": stats.get("steal_attempt"),
                "fold_to_steal_pct": stats.get("fold_to_steal"),
            },
            "flop": {
                "cbet_pct": stats.get("flop_cbet"),
                "fold_to_cbet_pct": stats.get("fold_to_flop_cbet"),
            },
            "turn": {
                "cbet_pct": stats.get("turn_cbet"),
                "fold_to_cbet_pct": stats.get("fold_to_turn_cbet"),
            },
            "river": {
                "cbet_pct": stats.get("river_cbet"),
                "fold_to_cbet_pct": stats.get("fold_to_river_cbet"),
            },
        }

    # ── Most-played opponents ─────────────────────────────────────

    def get_top_opponents(self, player_name, n=20):
        """Get the players you've played the most hands against."""
        pid = self.get_player_id(player_name)
        if pid is None:
            return []

        cur = self.conn.cursor(cursor_factory=psycopg2.extras.DictCursor)
        cur.execute(
            """
            SELECT p.player_name, COUNT(*) as hands_together
            FROM tourney_hand_player_statistics ps1
            JOIN tourney_hand_player_statistics ps2
                ON ps1.id_hand = ps2.id_hand AND ps1.id_player != ps2.id_player
            JOIN player p ON ps2.id_player = p.id_player
            WHERE ps1.id_player = %s
            GROUP BY p.player_name
            ORDER BY hands_together DESC
            LIMIT %s
            """,
            (pid, n),
        )
        return [dict(r) for r in cur.fetchall()]


# ── Standalone test ───────────────────────────────────────────────
if __name__ == "__main__":
    with PT4Bridge() as pt4:
        # Your stats
        print("=" * 60)
        print("YOUR STATS")
        print("=" * 60)
        profile = pt4.get_player_profile("ShireGrin2")
        print(f"Profile: {profile['profile']}")
        stats = profile["stats"]
        for k, v in stats.items():
            if v is not None:
                suffix = "%" if isinstance(v, float) and k not in ("bb_won",) else ""
                print(f"  {k:25s}: {v}{suffix}")

        # Top opponents
        print("\n" + "=" * 60)
        print("TOP OPPONENTS")
        print("=" * 60)
        opponents = pt4.get_top_opponents("ShireGrin2", 10)
        for opp in opponents:
            print(f"  {opp['player_name']:25s} {opp['hands_together']} hands")

        # Recent hands
        print("\n" + "=" * 60)
        print("RECENT HANDS")
        print("=" * 60)
        hands = pt4.get_recent_hands("ShireGrin2", 5)
        for h in hands:
            won = "✅" if h.get("flg_won_hand") else "❌"
            amt = h.get("amt_won", 0) or 0
            print(f"  #{h['hand_no']} | {h['date_played']} | pot={h['amt_pot']} | won={amt} {won}")
