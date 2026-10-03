import psycopg2
import json
import os
from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

def setup_and_migrate():
    conn = psycopg2.connect(
        host=os.environ.get("DB_SOLVER_HOST", "localhost"),
        port=int(os.environ.get("DB_SOLVER_PORT", "5432")),
        database=os.environ.get("DB_SOLVER_POSTGRES_DB", "dbname"),
        user=os.environ.get("DB_SOLVER_USER", "postgres"),
        password=os.environ.get("DB_SOLVER_PASSWORD", "dbpass")
    )
    cur = conn.cursor()
    
    # 1. Ensure tables exist
    cur.execute("""
        CREATE TABLE IF NOT EXISTS profile_clusters (id_cluster SERIAL PRIMARY KEY, cluster_name VARCHAR(255) UNIQUE, vpip_mean FLOAT, pfr_mean FLOAT, threebet_mean FLOAT, af_mean FLOAT, wtsd_mean FLOAT, ranges_json JSONB, betting_profiles_json JSONB);
        CREATE TABLE IF NOT EXISTS positions (id_position SERIAL PRIMARY KEY, name VARCHAR(10) UNIQUE NOT NULL, seat_order INT NOT NULL);
        CREATE TABLE IF NOT EXISTS action_scenarios (id_scenario SERIAL PRIMARY KEY, name VARCHAR(40) UNIQUE NOT NULL, description TEXT, frequency SMALLINT NOT NULL DEFAULT 128, typical_pot_bb REAL NOT NULL DEFAULT 6.5, parent_scenario_id INT REFERENCES action_scenarios(id_scenario));
        CREATE TABLE IF NOT EXISTS stack_clusters (id_stack_cluster SERIAL PRIMARY KEY, cluster_name VARCHAR(50) UNIQUE NOT NULL, bb_min REAL NOT NULL, bb_max REAL NOT NULL);
        CREATE TABLE IF NOT EXISTS cluster_ranges (id SERIAL PRIMARY KEY, id_cluster INT NOT NULL REFERENCES profile_clusters(id_cluster) ON DELETE CASCADE, id_position INT NOT NULL REFERENCES positions(id_position), id_scenario INT NOT NULL REFERENCES action_scenarios(id_scenario), id_stack_cluster INT NOT NULL REFERENCES stack_clusters(id_stack_cluster), range_text TEXT NOT NULL, UNIQUE(id_cluster, id_position, id_scenario, id_stack_cluster));
    """)
    
    # 2. Populate Positions
    positions = [
        ("UTG", 1), ("UTG+1", 2), ("UTG+2", 3), ("LJ", 4), 
        ("HJ", 5), ("CO", 6), ("BTN", 7), ("SB", 8), ("BB", 9)
    ]
    for name, order in positions:
        cur.execute("INSERT INTO positions (name, seat_order) VALUES (%s, %s) ON CONFLICT (name) DO NOTHING", (name, order))
        
    # 3. Populate Stack Clusters
    stack_clusters = [
        ("Ultra-Short", 0.0, 8.0),
        ("Short", 8.0, 20.0),
        ("Medium", 20.0, 35.0),
        ("Standard", 35.0, 60.0),
        ("Deep", 60.0, 1000.0)
    ]
    for name, bmin, bmax in stack_clusters:
        cur.execute("INSERT INTO stack_clusters (cluster_name, bb_min, bb_max) VALUES (%s, %s, %s) ON CONFLICT (cluster_name) DO NOTHING", (name, bmin, bmax))
        
    # 4. Populate Action Scenarios
    scenarios = [
        ("RFI", "Raise First In", 255, 6.5),
        ("limp", "Limping Preflop", 128, 6.5),
        ("isolate", "Isolating a Limper", 150, 10.0),
        ("call_vs_pfr", "Cold Calling a Raise", 200, 15.0),
        ("3bet_vs_pfr", "3-Betting vs PFR", 180, 22.0),
        ("4bet_vs_3bet", "4-Betting vs 3-Bet", 80, 45.0)
    ]
    for name, desc, freq, pot in scenarios:
        cur.execute("INSERT INTO action_scenarios (name, description, frequency, typical_pot_bb) VALUES (%s, %s, %s, %s) ON CONFLICT (name) DO NOTHING", (name, desc, freq, pot))

    conn.commit()

    # 5. Fetch IDs to migrate legacy ranges
    cur.execute("SELECT name, id_position FROM positions")
    pos_map = {row[0]: row[1] for row in cur.fetchall()}
    
    cur.execute("SELECT cluster_name, id_stack_cluster FROM stack_clusters")
    stack_map = {row[0]: row[1] for row in cur.fetchall()}
    
    cur.execute("SELECT name, id_scenario FROM action_scenarios")
    scen_map = {row[0]: row[1] for row in cur.fetchall()}

    # 6. Migrate Old Data (Hybrid/Basic Mapping)
    cur.execute("SELECT id_cluster, ranges_json FROM profile_clusters WHERE ranges_json IS NOT NULL")
    rows = cur.fetchall()
    
    def insert_range(c_id, p_id, s_id, st_id, rng_txt):
        if not rng_txt: return
        cur.execute("""
            INSERT INTO cluster_ranges (id_cluster, id_position, id_scenario, id_stack_cluster, range_text)
            VALUES (%s, %s, %s, %s, %s)
            ON CONFLICT (id_cluster, id_position, id_scenario, id_stack_cluster) DO UPDATE 
            SET range_text = EXCLUDED.range_text
        """, (c_id, p_id, s_id, st_id, rng_txt))

    print(f"Migrating {len(rows)} profiles into new schema (multiplying across positions and stack clusters)...")
    for row in rows:
        cluster_id = row[0]
        ranges_json = row[1]
        
        btn_range = ranges_json.get('BTN', '')
        utg_range = ranges_json.get('UTG', '')
        srp_call = ranges_json.get('SRP_call', '')
        bb_defend = ranges_json.get('BB_defend', '')
        threebp_call = ranges_json.get('3BP_call', '')
        threebp_raise = ranges_json.get('3BP_raise', '')
        srp_raise = ranges_json.get('SRP_raise', '')
        
        # Multiply across all stack sizes for now
        for stack_name, stack_id in stack_map.items():
            
            # Map UTG range to early/middle pos, BTN range to late pos RFI
            for pos_name, pos_id in pos_map.items():
                if pos_name in ['UTG', 'UTG+1', 'UTG+2', 'LJ']:
                    insert_range(cluster_id, pos_id, scen_map['RFI'], stack_id, utg_range)
                elif pos_name in ['HJ', 'CO', 'BTN']:
                    insert_range(cluster_id, pos_id, scen_map['RFI'], stack_id, btn_range)
                else: # SB, BB
                    insert_range(cluster_id, pos_id, scen_map['RFI'], stack_id, btn_range)

                # vs_pfr
                if pos_name == 'BB':
                    insert_range(cluster_id, pos_id, scen_map['vs_pfr'], stack_id, bb_defend)
                else:
                    insert_range(cluster_id, pos_id, scen_map['vs_pfr'], stack_id, srp_call)

                # 3-bet
                insert_range(cluster_id, pos_id, scen_map['vs_3bet'], stack_id, threebp_call)
                insert_range(cluster_id, pos_id, scen_map['vs_4bet'], stack_id, threebp_raise)
                
    conn.commit()
    print("Migration and basic interpolation completed.")
    cur.close()
    conn.close()

if __name__ == "__main__":
    setup_and_migrate()
