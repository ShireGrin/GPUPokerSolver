import psycopg2
import json
import os
from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

TS_DB_HOST = os.environ.get("DB_SOLVER_HOST", "localhost")
TS_DB_PORT = int(os.environ.get("DB_SOLVER_PORT", 5432))
TS_DB_NAME = os.environ.get("DB_SOLVER", "dbname")
TS_DB_USER = os.environ.get("DB_SOLVER_USER", "postgres")
TS_DB_PASS = os.environ.get("DB_SOLVER_PASSWORD", "dbpass")

def main():
    json_path = os.path.join(os.path.dirname(__file__), "profiles.json")
    if not os.path.exists(json_path):
        print(f"File not found: {json_path}")
        return

    with open(json_path, "r") as f:
        data = json.load(f)

    try:
        conn = psycopg2.connect(host=TS_DB_HOST, port=TS_DB_PORT, dbname=TS_DB_NAME, user=TS_DB_USER, password=TS_DB_PASS)
        cursor = conn.cursor()
        
        # Initialize schema
        schema = [
            "CREATE TABLE IF NOT EXISTS sites (id_site SERIAL PRIMARY KEY, site_name VARCHAR(255) UNIQUE)",
            "CREATE TABLE IF NOT EXISTS tourneys (id_tourney SERIAL PRIMARY KEY, id_site INT, tourney_no VARCHAR(255), buyin FLOAT, date_played TIMESTAMP, UNIQUE(id_site, tourney_no))",
            "CREATE TABLE IF NOT EXISTS players (id_player SERIAL PRIMARY KEY, id_site INT, player_name VARCHAR(255), UNIQUE(id_site, player_name))",
            "CREATE TABLE IF NOT EXISTS profile_clusters (id_cluster SERIAL PRIMARY KEY, cluster_name VARCHAR(255) UNIQUE, vpip_mean FLOAT, pfr_mean FLOAT, threebet_mean FLOAT, af_mean FLOAT, wtsd_mean FLOAT, ranges_json JSONB, betting_profiles_json JSONB)",
            "CREATE TABLE IF NOT EXISTS player_profiles (id_player INT, id_cluster INT, vpip FLOAT, pfr FLOAT, threebet FLOAT, af FLOAT, wtsd FLOAT, cnt_hands INT, stack_depth_bb_min FLOAT, stack_depth_bb_max FLOAT, blind_level_min FLOAT, blind_level_max FLOAT, PRIMARY KEY(id_player, stack_depth_bb_min, stack_depth_bb_max, blind_level_min, blind_level_max))",
            "CREATE TABLE IF NOT EXISTS hands (id_hand SERIAL PRIMARY KEY, id_tourney INT, hand_no VARCHAR(255), date_played TIMESTAMP, cnt_players INT, bb_size FLOAT, ante FLOAT, pot FLOAT, effective_stack FLOAT, board VARCHAR(255), ip_player_id INT, oop_player_id INT, turn_card VARCHAR(10), river_card VARCHAR(10), UNIQUE(id_tourney, hand_no))",
            "CREATE TABLE IF NOT EXISTS player_gto_decisions (id_decision SERIAL PRIMARY KEY, id_hand INT, id_player INT, street VARCHAR(20), action_sequence TEXT, action_taken VARCHAR(50), gto_action VARCHAR(50), ev_action FLOAT, ev_gto FLOAT, ev_loss FLOAT)",
            "CREATE TABLE IF NOT EXISTS solves (id_solve SERIAL PRIMARY KEY, id_hand INT, solve_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP, config_text TEXT, exploitability FLOAT, strategy_blob BYTEA)"
        ]
        for q in schema:
            cursor.execute(q)
        conn.commit()
        
    except Exception as e:
        print(f"Failed to connect to or initialize database: {e}")
        return

    profiles = data.get("profiles", {})
    
    # Extract the base ones from JSON to use as templates.
    templates = {}
    for cluster_key, cluster_data in profiles.items():
        pname = cluster_data.get("profile_name")
        if pname:
            # Prefer to keep the latest added if duplicates
            templates[pname] = cluster_data

    # Map our new 8 heuristic names to the closest template from the 5 original ones
    heuristic_mapping = {
        "Nit": "Tight-Passive",
        "Nit-Aggressive": "Tight-Aggressive (TAG)",
        "Tight-Passive": "Tight-Passive",
        "Tight-Aggressive (TAG)": "Tight-Aggressive (TAG)",
        "Loose-Passive (Calling Station)": "Loose Passive",
        "Loose-Limper": "Loose Passive",
        "Loose-Aggressive (LAG)": "Loose Aggressive (LAG)",
        "Aggressive Maniac": "Loose Aggressive (LAG)",
        "Whale": "Loose Passive"
    }

    # Gather all names to insert (those in JSON + our heuristic names)
    names_to_insert = list(heuristic_mapping.keys())
    for cluster_key, cluster_data in profiles.items():
        pname = cluster_data.get("profile_name")
        if pname and pname not in names_to_insert:
            names_to_insert.append(pname)
            
    for name in names_to_insert:
        # We need to handle potential duplicate names (e.g. "Nit 1") dynamically, but for now we insert the base ones.
        # This will ensure the DB has ranges for the base heuristic names.
        if name in templates:
            cdata = templates[name]
        elif name in heuristic_mapping and heuristic_mapping[name] in templates:
            cdata = templates[heuristic_mapping[name]]
        else:
            cdata = list(templates.values())[0] if templates else {}

        stats = cdata.get("stats", {})
        vpip = stats.get("VPIP", 20.0)
        pfr = stats.get("PFR", 15.0)
        threebet = stats.get("3Bet", 5.0)
        af = stats.get("AF", 2.0)
        wtsd = stats.get("WTSD", 30.0)
        
        ranges = cdata.get("ranges", {})
        ranges_json = json.dumps(ranges)
        
        betting = {k: v for k, v in cdata.items() if k not in ["stats", "profile_name", "ranges"]}
        betting_json = json.dumps(betting)
        
        cursor.execute("""
            INSERT INTO profile_clusters 
            (cluster_name, vpip_mean, pfr_mean, threebet_mean, af_mean, wtsd_mean, ranges_json, betting_profiles_json)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            ON CONFLICT (cluster_name) DO UPDATE SET
            vpip_mean=EXCLUDED.vpip_mean, pfr_mean=EXCLUDED.pfr_mean, threebet_mean=EXCLUDED.threebet_mean,
            af_mean=EXCLUDED.af_mean, wtsd_mean=EXCLUDED.wtsd_mean, ranges_json=EXCLUDED.ranges_json, 
            betting_profiles_json=EXCLUDED.betting_profiles_json
        """, (name, vpip, pfr, threebet, af, wtsd, ranges_json, betting_json))

    conn.commit()
    cursor.close()
    conn.close()
    print(f"Successfully imported {len(names_to_insert)} base profile templates into database.")

if __name__ == "__main__":
    main()
