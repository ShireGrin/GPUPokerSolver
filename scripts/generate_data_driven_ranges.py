import psycopg2
import math
import argparse
import os
from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

DB_PT4 = {
    "host": os.environ.get("DB_PT4_HOST", "localhost"),
    "port": int(os.environ.get("DB_PT4_PORT", 5432)),
    "database": os.environ.get("DB_PT4", "PT4 DB"),
    "user": os.environ.get("DB_PT4_USER", "postgres"),
    "password": os.environ.get("DB_PT4_PASSWORD", "texassolver")
}

DB_SOLVER = {
    "host": os.environ.get("DB_SOLVER_HOST", "localhost"),
    "port": int(os.environ.get("DB_SOLVER_PORT", 5432)),
    "database": os.environ.get("DB_SOLVER", "dbname"),
    "user": os.environ.get("DB_SOLVER_USER", "postgres"),
    "password": os.environ.get("DB_SOLVER_PASSWORD", "texassolver")
}

def get_db_connections():
    conn_pt4 = psycopg2.connect(**DB_PT4)
    conn_solver = psycopg2.connect(**DB_SOLVER)
    return conn_pt4, conn_solver

def get_player_stats(cur_pt4):
    print("Fetching player stats from PT4...")
    cur_pt4.execute("""
        SELECT 
            id_player,
            SUM(cnt_hands) as cnt_hands,
            SUM(cnt_vpip) as cnt_vpip,
            SUM(cnt_walks) as cnt_walks,
            SUM(cnt_pfr) as cnt_pfr,
            SUM(cnt_pfr_opp) as cnt_pfr_opp
        FROM tourney_cache
        GROUP BY id_player
        HAVING SUM(cnt_hands) >= 5
    """)
    
    player_stats = {}
    for row in cur_pt4.fetchall():
        id_player, cnt_hands, cnt_vpip, cnt_walks, cnt_pfr, cnt_pfr_opp = row
        
        hands_for_vpip = cnt_hands - cnt_walks
        vpip = (cnt_vpip / hands_for_vpip * 100.0) if hands_for_vpip > 0 else 0
        pfr = (cnt_pfr / cnt_pfr_opp * 100.0) if cnt_pfr_opp > 0 else 0
        
        player_stats[id_player] = {
            'cnt_hands': cnt_hands,
            'vpip': vpip,
            'pfr': pfr,
            'weight': min(1.0, cnt_hands / 100.0)
        }
    print(f"Loaded stats for {len(player_stats)} players.")
    return player_stats

def get_clusters(cur_solver):
    cur_solver.execute("SELECT id_cluster, cluster_name, vpip_mean, pfr_mean FROM profile_clusters")
    clusters = []
    for row in cur_solver.fetchall():
        clusters.append({
            'id': row[0],
            'name': row[1],
            'vpip': row[2],
            'pfr': row[3]
        })
    return clusters

def map_players_to_clusters(player_stats, clusters):
    cluster_mapping = {}
    for pid, stats in player_stats.items():
        vpip = stats['vpip']
        pfr = stats['pfr']
        
        best_cluster = None
        min_dist = float('inf')
        
        for c in clusters:
            if c['vpip'] is None or c['pfr'] is None:
                continue
            dist = math.sqrt((vpip - float(c['vpip']))**2 + (pfr - float(c['pfr']))**2)
            if dist < min_dist:
                min_dist = dist
                best_cluster = c['id']
                
        if best_cluster is not None:
            cluster_mapping[pid] = best_cluster
            
    return cluster_mapping

def load_holecard_lookup(cur_pt4):
    cur_pt4.execute("SELECT id_holecard, hole_cards FROM lookup_hole_cards WHERE id_gametype = 1")
    return {row[0]: row[1] for row in cur_pt4.fetchall()}

def run_pipeline():
    conn_pt4, conn_solver = get_db_connections()
    cur_pt4 = conn_pt4.cursor()
    cur_solver = conn_solver.cursor()
    
    hc_lookup = load_holecard_lookup(cur_pt4)
    player_stats = get_player_stats(cur_pt4)
    clusters = get_clusters(cur_solver)
    
    player_to_cluster = map_players_to_clusters(player_stats, clusters)
    
    pt4_pos_to_name = {
        8: "BB", 9: "SB", 0: "BTN", 1: "CO", 2: "HJ", 3: "LJ", 4: "UTG+2", 5: "UTG+1", 6: "UTG"
    }
    
    cur_solver.execute("SELECT id_position, name FROM positions")
    name_to_id_pos = {row[1]: row[0] for row in cur_solver.fetchall()}
    
    print("Querying showdown hands from PT4...")
    cur_pt4.execute("""
        SELECT 
            thps.id_player,
            thps.position,
            thps.id_holecard,
            thps.flg_p_first_raise,
            thps.flg_p_3bet,
            thps.flg_p_4bet,
            thps.flg_p_ccall,
            thps.flg_p_limp,
            thps.flg_p_open_opp,
            thps.enum_allin,
            thps.enum_p_3bet_action,
            thps.flg_p_3bet_def_opp,
            thps.cnt_p_call,
            (thps.amt_p_effective_stack / NULLIF(tb.amt_bb, 0)) AS stack_bb
        FROM tourney_hand_player_statistics thps
        JOIN tourney_blinds tb ON thps.id_blinds = tb.id_blinds
        WHERE thps.flg_showdown = true AND thps.id_holecard BETWEEN 1 AND 169
    """)
    
    cur_solver.execute("SELECT id_scenario, name FROM action_scenarios")
    scenarios = {row[1]: row[0] for row in cur_solver.fetchall()}
    
    cur_solver.execute("SELECT id_stack_cluster, bb_min, bb_max FROM stack_clusters")
    stack_clusters_db = cur_solver.fetchall()
    
    aggregated_ranges = {}
    
    count = 0
    for row in cur_pt4.fetchall():
        id_player = row[0]
        pos = row[1]
        id_holecard = row[2]
        rfi = row[3]
        threebet = row[4]
        fourbet = row[5]
        ccall = row[6]
        limp = row[7]
        open_opp = row[8]
        enum_allin = row[9]
        enum_p_3bet_action = row[10]
        flg_p_3bet_def_opp = row[11]
        cnt_p_call = row[12]
        stack_bb = row[13]
        
        if id_player not in player_to_cluster:
            continue
            
        cluster_id = player_to_cluster[id_player]
        weight = player_stats[id_player]['weight']
        hc_str = hc_lookup.get(id_holecard, "")
        if not hc_str: continue
        
        pos_name = pt4_pos_to_name.get(pos, "UTG")
        pos_id = name_to_id_pos.get(pos_name)
        if not pos_id: continue
        
        scen_name = None
        if rfi and open_opp:
            if enum_allin == 'P':
                scen_name = "open_jam"
            else:
                scen_name = "RFI"
        elif rfi and not open_opp:
            scen_name = "isolate"
        elif threebet:
            if enum_allin == 'P':
                scen_name = "3bet_jam"
            else:
                scen_name = "3bet_vs_pfr" 
        elif fourbet:
            scen_name = "4bet_vs_3bet"
        elif flg_p_3bet_def_opp and enum_p_3bet_action == 'C':
            scen_name = "call_vs_3bet"
        elif ccall or (cnt_p_call > 0 and not limp):
            scen_name = "call_vs_pfr"
        elif limp:
            scen_name = "limp"
            
        if not scen_name or scen_name not in scenarios:
            continue
            
        scen_id = scenarios[scen_name]
        
        if stack_bb is None:
            continue
            
        stack_id = None
        for s_id, bb_min, bb_max in stack_clusters_db:
            if float(bb_min) <= float(stack_bb) <= float(bb_max):
                stack_id = s_id
                break
                
        if not stack_id:
            continue
        
        key = (cluster_id, pos_id, scen_id, stack_id)
        if key not in aggregated_ranges:
            aggregated_ranges[key] = {}
            
        if hc_str not in aggregated_ranges[key]:
            aggregated_ranges[key][hc_str] = 0.0
        aggregated_ranges[key][hc_str] += weight
        
        count += 1
        
    print(f"Processed {count} valid showdown actions.")
    
    print("Updating cluster_ranges in database...")
    inserted = 0
    for key, hc_counts in aggregated_ranges.items():
        cluster_id, pos_id, scen_id, stack_id = key
        
        total_count = sum(hc_counts.values())
        max_count = max(hc_counts.values()) if hc_counts else 1.0
        
        sorted_counts = sorted(hc_counts.values(), reverse=True)
        ceiling_count = sum(sorted_counts[:5]) / min(5, len(sorted_counts)) if sorted_counts else 1.0
        
        range_text_parts = []
        range_true_parts = []
        range_counts_parts = []
        
        for hc, c in hc_counts.items():
            range_counts_parts.append(f"{hc}:{c:.1f}")
            
            prob_true = min(1.0, c / max_count)
            range_true_parts.append(f"{hc}:{prob_true:.2f}")
            
            if c / ceiling_count >= 0.05:
                prob_clean = min(1.0, c / ceiling_count)
                range_text_parts.append(f"{hc}:{prob_clean:.2f}")
                
        if range_counts_parts:
            range_text_str = ",".join(range_text_parts)
            range_true_str = ",".join(range_true_parts)
            range_counts_str = ",".join(range_counts_parts)
            
            cur_solver.execute("""
                INSERT INTO cluster_ranges (id_cluster, id_position, id_scenario, id_stack_cluster, range_text, range_true, range_counts)
                VALUES (%s, %s, %s, %s, %s, %s, %s)
                ON CONFLICT (id_cluster, id_position, id_scenario, id_stack_cluster) 
                DO UPDATE SET 
                    range_text = EXCLUDED.range_text,
                    range_true = EXCLUDED.range_true,
                    range_counts = EXCLUDED.range_counts
            """, (cluster_id, pos_id, scen_id, stack_id, range_text_str, range_true_str, range_counts_str))
            inserted += 1
            
    conn_solver.commit()
    print(f"Successfully updated {inserted} ranges!")
    
    cur_pt4.close()
    cur_solver.close()
    conn_pt4.close()
    conn_solver.close()

if __name__ == "__main__":
    run_pipeline()
