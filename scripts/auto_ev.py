import psycopg2
import psycopg2.extras
import argparse
import os
import subprocess
import json
import uuid
from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

DB_PT4 = {
    "host": os.environ.get("DB_PT4_HOST", "localhost"),
    "port": int(os.environ.get("DB_PT4_PORT", 5432)),
    "database": os.environ.get("DB_PT4", "PT4 DB"),
    "user": os.environ.get("DB_PT4_USER", "postgres"),
    "password": os.environ.get("DB_PT4_PASSWORD", "texassolver")
}

def init_db(conn):
    with conn.cursor() as cur:
        cur.execute("""
            CREATE TABLE IF NOT EXISTS public.texas_solver_ev (
                id SERIAL PRIMARY KEY,
                id_hand INTEGER NOT NULL,
                id_player INTEGER NOT NULL,
                holecard_str VARCHAR(10) NOT NULL,
                ev_value NUMERIC(10, 4) NOT NULL,
                strategy_json_path TEXT,
                evaluated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE(id_hand, id_player)
            )
        """)
        conn.commit()

def fetch_unevaluated_hands(conn, player_name, limit=50):
    query = """
        SELECT 
            thps.id_hand,
            thps.id_player,
            ths.id_site,
            ths.date_played,
            ths.amt_pot_f,
            ths.amt_pot_t,
            ths.amt_pot_r,
            thps.amt_f_effective_stack as hero_f_stack,
            thps.flg_f_has_position,
            thps.id_holecard,
            lhc.hole_cards,
            ths.str_aggressors_p,
            ths.str_actors_p,
            ths.cnt_players_f,
            ths.card_1, ths.card_2, ths.card_3, ths.card_4, ths.card_5,
            p.player_name,
            (
                SELECT p2.player_name 
                FROM tourney_hand_player_statistics thps2 
                JOIN player p2 ON p2.id_player = thps2.id_player 
                WHERE thps2.id_hand = thps.id_hand 
                  AND thps2.id_player != thps.id_player 
                  AND thps2.flg_f_saw = true
                LIMIT 1
            ) as villain_name
        FROM tourney_hand_player_statistics thps
        JOIN tourney_hand_summary ths ON ths.id_hand = thps.id_hand
        JOIN player p ON p.id_player = thps.id_player
        LEFT JOIN lookup_hole_cards lhc ON lhc.id_holecard = thps.id_holecard AND lhc.id_gametype = 1
        LEFT JOIN texas_solver_ev tse ON tse.id_hand = thps.id_hand AND tse.id_player = thps.id_player
        WHERE p.player_name = %s
          AND thps.flg_f_saw = true
          AND ths.cnt_players_f = 2
          AND ths.id_gametype = 1
          AND lhc.hole_cards IS NOT NULL
          AND tse.id IS NULL
        ORDER BY ths.date_played DESC
        LIMIT %s
    """
    with conn.cursor(cursor_factory=psycopg2.extras.DictCursor) as cur:
        cur.execute(query, (player_name, limit))
        return cur.fetchall()

def card_num_to_str(card_id):
    if card_id <= 0: return ""
    ranks = "23456789TJQKA"
    suits = "csdh"
    card_id -= 1  # PT4 1-based to 0-based
    rank = (card_id % 13)
    suit = (card_id // 13)
    if rank < len(ranks) and suit < len(suits):
        return ranks[rank] + suits[suit]
    return ""

def generate_params_script(hand, p1_range, p2_range, script_path, output_json, evs_json):
    # Construct board string
    board_cards = []
    for c in [hand['card_1'], hand['card_2'], hand['card_3'], hand['card_4'], hand['card_5']]:
        if c > 0:
            board_cards.append(card_num_to_str(c))
    board = ",".join(board_cards)

    pot = hand['amt_pot_f']
    eff_stack = hand['hero_f_stack'] # Assuming hero eff stack is a good approximation for HU postflop
    if eff_stack <= 0:
        eff_stack = 0.01  # Prevent tree building failure on 0 stack

    # A typical parameters file for TexasSolverGui -c
    lines = [
        f"set_board {board}",
        f"set_range_ip {p1_range}",
        f"set_range_oop {p2_range}",
        f"set_pot {pot}",
        f"set_effective_stack {eff_stack}",
        "build_tree",
        "set_max_iteration 300",
        "set_accuracy 0.5",
        "start_solve",
        f"dump_result {output_json}",
        f"dump_evs {evs_json}"
    ]

    with open(script_path, 'w') as f:
        f.write("\n".join(lines))

def get_holecard_ev(evs_json_path, holecard_str, board_cards, is_ip):
    if not os.path.exists(evs_json_path):
        return None
    with open(evs_json_path, 'r') as f:
        data = json.load(f)
        
    ev_matrix = data
    
    ranks = "23456789TJQKA"
    suits = "cdhs"
    
    def card_to_int(rank, suit):
        r = ranks.index(rank) + 2
        s = suits.index(suit)
        return (r - 2) * 4 + s
        
    combos = []
    if not holecard_str:
        return None
        
    if len(holecard_str) == 2: # pair
        rank = holecard_str[0]
        for i in range(4):
            for j in range(i+1, 4):
                combos.append((card_to_int(rank, suits[i]), card_to_int(rank, suits[j])))
    elif len(holecard_str) == 3:
        rank1, rank2, type_ = holecard_str
        if type_ == 's':
            for s in suits:
                combos.append((card_to_int(rank1, s), card_to_int(rank2, s)))
        elif type_ == 'o':
            for s1 in suits:
                for s2 in suits:
                    if s1 != s2:
                        combos.append((card_to_int(rank1, s1), card_to_int(rank2, s2)))

    board_ints = []
    for c in board_cards:
        if c:
            board_ints.append(card_to_int(c[0], c[1]))

    total_ev = 0.0
    count = 0
    for c1, c2 in combos:
        if c1 in board_ints or c2 in board_ints:
            continue
        evs = ev_matrix[c1][c2]
        if not evs:
            evs = ev_matrix[c2][c1]
        
        if evs and len(evs) > 0:
            oop_ev = evs[0]
            if is_ip:
                pass
            total_ev += oop_ev
            count += 1
            
    if count == 0:
        return None
    return total_ev / count

def expand_poker_range(range_str):
    ranks = "23456789TJQKA"
    expanded = []
    
    for token in range_str.replace(" ", "").split(','):
        if not token: continue
        
        if '+' in token:
            base = token.replace('+', '')
            if len(base) == 2: # e.g. 22+
                rank = base[0]
                idx = ranks.index(rank)
                for i in range(idx, len(ranks)):
                    expanded.append(ranks[i] + ranks[i])
            elif len(base) == 3: # e.g. A2s+
                high_rank = base[0]
                low_rank = base[1]
                suit = base[2]
                
                high_idx = ranks.index(high_rank)
                low_idx = ranks.index(low_rank)
                
                for i in range(low_idx, high_idx):
                    expanded.append(high_rank + ranks[i] + suit)
        else:
            expanded.append(token)
            
    # remove duplicates
    result = []
    seen = set()
    for e in expanded:
        if e not in seen:
            seen.add(e)
            result.append(e)
            
    return ",".join(result)

def get_player_range(player_name, is_ip, profiles_data, full_range):
    if not profiles_data or not player_name:
        return full_range
        
    player_mapping = profiles_data.get('player_mapping', {})
    profiles = profiles_data.get('profiles', {})
    
    if player_name not in player_mapping:
        return full_range
        
    profile_name = player_mapping[player_name]
    
    # Find the cluster that matches this profile_name
    matched_ranges = None
    for cluster_id, cluster_data in profiles.items():
        if cluster_data.get('profile_name') == profile_name:
            matched_ranges = cluster_data.get('ranges', {})
            break
            
    if not matched_ranges:
        return full_range
        
    if is_ip:
        return matched_ranges.get('BTN', full_range)
    else:
        return matched_ranges.get('BB_defend', full_range)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("player_name", help="Hero's player name in PT4")
    parser.add_argument("--limit", type=int, default=10, help="Number of hands to evaluate")
    args = parser.parse_args()

    conn = psycopg2.connect(**DB_PT4)
    init_db(conn)

    print(f"Fetching unevaluated hands for {args.player_name}...")
    hands = fetch_unevaluated_hands(conn, args.player_name, args.limit)
    print(f"Found {len(hands)} hands.")

    if len(hands) == 0:
        return

    profiles_path = os.path.join(os.path.dirname(__file__), "profiles.json")
    profiles_data = {}
    if os.path.exists(profiles_path):
        with open(profiles_path, "r") as f:
            profiles_data = json.load(f)

    full_range = "AA,KK,QQ,JJ,TT,99,88,77,66,55,44,33,22,AKs,AQs,AJs,ATs,A9s,A8s,A7s,A6s,A5s,A4s,A3s,A2s,KQs,KJs,KTs,K9s,K8s,K7s,K6s,K5s,K4s,K3s,K2s,QJs,QTs,Q9s,Q8s,Q7s,Q6s,Q5s,Q4s,Q3s,Q2s,JTs,J9s,J8s,J7s,J6s,J5s,J4s,J3s,J2s,T9s,T8s,T7s,T6s,T5s,T4s,T3s,T2s,98s,97s,96s,95s,94s,93s,92s,87s,86s,85s,84s,83s,82s,76s,75s,74s,73s,72s,65s,64s,63s,62s,54s,53s,52s,43s,42s,32s,AKo,AQo,AJo,ATo,A9o,A8o,A7o,A6o,A5o,A4o,A3o,A2o,KQo,KJo,KTo,K9o,K8o,K7o,K6o,K5o,K4o,K3o,K2o,QJo,QTo,Q9o,Q8o,Q7o,Q6o,Q5o,Q4o,Q3o,Q2o,JTo,J9o,J8o,J7o,J6o,J5o,J4o,J3o,J2o,T9o,T8o,T7o,T6o,T5o,T4o,T3o,T2o,98o,97o,96o,95o,94o,93o,92o,87o,86o,85o,84o,83o,82o,76o,75o,74o,73o,72o,65o,64o,63o,62o,54o,53o,52o,43o,42o,32o"

    solver_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build')
    
    for hand in hands:
        print(f"Evaluating hand ID {hand['id_hand']}...")
        
        hero_name = hand['player_name']
        villain_name = hand['villain_name']
        is_ip = hand['flg_f_has_position']
        
        if is_ip:
            p1_range = get_player_range(hero_name, True, profiles_data, full_range)
            p2_range = get_player_range(villain_name, False, profiles_data, full_range)
        else:
            p1_range = get_player_range(villain_name, True, profiles_data, full_range)
            p2_range = get_player_range(hero_name, False, profiles_data, full_range)
            
        p1_range = expand_poker_range(p1_range)
        p2_range = expand_poker_range(p2_range)
        
        print(f"Ranges -> IP: {p1_range[:30]}..., OOP: {p2_range[:30]}...")
        
        run_id = uuid.uuid4().hex[:8]
        script_path = os.path.join(solver_dir, f"params_{run_id}.txt")
        output_json = os.path.join(solver_dir, f"strategy_{run_id}.json")
        evs_json = os.path.join(solver_dir, f"evs_{run_id}.json")

        generate_params_script(hand, p1_range, p2_range, script_path, output_json, evs_json)

        cmd = ["./TexasSolverGui", "-c", "-r", "../resources", "-i", script_path]
        print(f"Running solver for hand {hand['id_hand']}...")
        subprocess.run(cmd, cwd=solver_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        board_cards = [
            card_num_to_str(hand['card_1']),
            card_num_to_str(hand['card_2']),
            card_num_to_str(hand['card_3']),
            card_num_to_str(hand['card_4']),
            card_num_to_str(hand['card_5'])
        ]
        
        holecard_str = hand['hole_cards']
        is_ip = hand['flg_f_has_position']
        ev = get_holecard_ev(evs_json, holecard_str, board_cards, is_ip)
        
        if ev is not None:
            # Insert into database
            with conn.cursor() as cur:
                cur.execute(
                    "INSERT INTO texas_solver_ev (id_hand, id_player, holecard_str, ev_value, strategy_json_path) VALUES (%s, %s, %s, %s, %s) ON CONFLICT (id_hand, id_player) DO UPDATE SET ev_value = EXCLUDED.ev_value",
                    (hand['id_hand'], hand['id_player'], holecard_str, ev, output_json)
                )
            conn.commit()
            print(f"Recorded EV {ev:.4f} for hand {hand['id_hand']} with holecards {holecard_str}")
        else:
            print(f"Failed to extract EV for hand {hand['id_hand']}")
            
        # cleanup
        if ev is not None:
            for f in [script_path, output_json, evs_json]:
                if os.path.exists(f):
                    os.remove(f)

if __name__ == "__main__":
    main()
