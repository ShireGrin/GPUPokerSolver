import os
from dotenv import load_dotenv
import random
import psycopg2
import argparse

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

def get_random_board():
    ranks = "23456789TJQKA"
    suits = "cdhs"
    deck = [r+s for r in ranks for s in suits]
    return ",".join(random.sample(deck, 3))

def populate_samples(num_samples=1000):
    conn = psycopg2.connect(
        host=os.environ.get("DB_SOLVER_HOST", "localhost"),
        port=int(os.environ.get("DB_SOLVER_PORT", "5432")),
        database=os.environ.get("DB_SOLVER", "dbname"),
        user=os.environ.get("DB_SOLVER_USER", "postgres"),
        password=os.environ.get("DB_SOLVER_PASSWORD", "dbpass")
    )
    cur = conn.cursor()
    
    # Load all needed data
    cur.execute("SELECT id_scenario, name, frequency, typical_pot_bb FROM action_scenarios")
    scenarios = cur.fetchall()
    
    cur.execute("SELECT id_position, name, seat_order FROM positions ORDER BY seat_order")
    positions = cur.fetchall()
    
    cur.execute("SELECT id_cluster, cluster_name FROM profile_clusters")
    clusters = cur.fetchall()
    
    cur.execute("SELECT id_stack_cluster, bb_min, bb_max FROM stack_clusters")
    stack_clusters = cur.fetchall()
    
    # Load all ranges into a dict for fast lookup
    cur.execute("SELECT id_cluster, id_position, id_scenario, id_stack_cluster, range_text FROM cluster_ranges")
    ranges_map = {}
    for row in cur.fetchall():
        ranges_map[(row[0], row[1], row[2], row[3])] = row[4]
        
    print(f"Loaded {len(scenarios)} scenarios, {len(positions)} positions, {len(clusters)} clusters, {len(stack_clusters)} stack clusters, {len(ranges_map)} range entries.")

    scenarios_weights = [s[2] for s in scenarios]
    
    inserted = 0
    attempts = 0
    max_attempts = num_samples * 20  # allow plenty of retries for missing ranges
    
    while inserted < num_samples and attempts < max_attempts:
        attempts += 1
        
        # 1. Sample Scenario
        scenario = random.choices(scenarios, weights=scenarios_weights, k=1)[0]
        id_scenario = scenario[0]
        typical_pot = scenario[3]
        
        # 2. Sample Positions (OOP must act before IP)
        if len(positions) < 2: continue
        oop_idx = random.randint(0, len(positions)-2)
        ip_idx = random.randint(oop_idx+1, len(positions)-1)
        oop_pos = positions[oop_idx]
        ip_pos = positions[ip_idx]
        
        # 3. Sample Player Profiles
        oop_cluster = random.choice(clusters)
        ip_cluster = random.choice(clusters)
        
        # 4. Sample Stack Cluster (up to 60 BB)
        short_stack_clusters = [sc for sc in stack_clusters if sc[2] <= 60.0]
        if not short_stack_clusters: continue
        stack_cluster = random.choice(short_stack_clusters)
        stack_val = random.uniform(stack_cluster[1], stack_cluster[2])
        
        # 5. Get ranges — skip if either range is missing from the DB
        oop_key = (oop_cluster[0], oop_pos[0], id_scenario, stack_cluster[0])
        ip_key = (ip_cluster[0], ip_pos[0], id_scenario, stack_cluster[0])
        if oop_key not in ranges_map or ip_key not in ranges_map:
            continue
        oop_range = ranges_map[oop_key].replace(" ", "")
        ip_range = ranges_map[ip_key].replace(" ", "")
        
        # 6. Randomize Pot
        pot_val = typical_pot * random.uniform(0.85, 1.15)
        
        # 7. Generate a random flop board (3 cards)
        board_text = get_random_board()
        
        # We don't need to parse board_text to cards here, TexasSolver does that. 
        # But for schema requirements we can just store -1 if we don't want to parse.
        # Actually, let's let python store it. It's just simple string manipulation.
        
        # Insert pending task
        cur.execute("""
            INSERT INTO training_samples (
                board_text, oop_range_text, ip_range_text, board_card0, board_card1, board_card2,
                pot, effective_stack, spr, status,
                id_cluster_oop, id_cluster_ip, id_position_oop, id_position_ip, id_scenario, id_stack_cluster
            ) VALUES (%s, %s, %s, -1, -1, -1, %s, %s, %s, 'pending', %s, %s, %s, %s, %s, %s)
        """, (
            board_text, oop_range, ip_range,
            pot_val, stack_val, stack_val / pot_val,
            oop_cluster[0], ip_cluster[0], oop_pos[0], ip_pos[0], id_scenario, stack_cluster[0]
        ))
        
        inserted += 1
        if inserted % 1000 == 0:
            conn.commit()
            print(f"Inserted {inserted} / {num_samples} scenarios...")

    conn.commit()
    cur.close()
    conn.close()
    print(f"Successfully populated {inserted} scenarios.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Populate Pending Training Scenarios")
    parser.add_argument("--samples", type=int, default=1000, help="Number of samples to pre-generate")
    args = parser.parse_args()
    
    populate_samples(args.samples)
