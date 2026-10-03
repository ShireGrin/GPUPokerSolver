import os
from dotenv import load_dotenv
import random
import subprocess
import psycopg2
import argparse

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

def get_random_board():
    ranks = "23456789TJQKA"
    suits = "cdhs"
    deck = [r+s for r in ranks for s in suits]
    return ",".join(random.sample(deck, 3))

def generate_samples(num_samples=10, solver_path="./build/TexasSolverGui"):
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

    scenarios_ids = [s[0] for s in scenarios]
    scenarios_weights = [s[2] for s in scenarios]
    
    completed = 0
    attempts = 0
    max_attempts = num_samples * 20  # allow plenty of retries for missing ranges
    
    while completed < num_samples and attempts < max_attempts:
        attempts += 1
        i = completed
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
        
        # 4. Sample Stack Cluster (stay on the short side to reduce VRAM usage)
        short_stack_clusters = [sc for sc in stack_clusters if sc[2] <= 35.0]
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
        board = get_random_board()
        
        # Create Solver Script
        script_content = f"""set_pot {pot_val:.2f}
set_effective_stack {stack_val:.2f}
set_board {board}
set_range_oop {oop_range}
set_range_ip {ip_range}
set_bet_sizes oop,flop,bet,30,70
set_bet_sizes ip,flop,bet,30,70
set_bet_sizes oop,turn,bet,30,70
set_bet_sizes ip,turn,bet,30,70
set_bet_sizes oop,river,bet,30,70
set_bet_sizes ip,river,bet,30,70
set_bet_sizes oop,flop,raise,50
set_bet_sizes ip,flop,raise,50
set_bet_sizes oop,turn,raise,50
set_bet_sizes ip,turn,raise,50
set_bet_sizes oop,river,raise,50
set_bet_sizes ip,river,raise,50
set_bet_sizes oop,river,allin
set_bet_sizes ip,river,allin
set_allin_threshold 0.8
build_tree
set_use_isomorphism 1
set_accuracy 0.05
set_max_iteration 1200
set_print_interval 200
start_solve
dump_training_sample {oop_cluster[0]},{ip_cluster[0]},{oop_pos[0]},{ip_pos[0]},{id_scenario},{stack_cluster[0]},0.05
"""
        script_file = "batch_script.txt"
        with open(script_file, "w") as f:
            f.write(script_content)
            
        print(f"[{i+1}/{num_samples}] Solving Scenario {scenario[1]} | OOP: {oop_cluster[1]} ({oop_pos[1]}) | IP: {ip_cluster[1]} ({ip_pos[1]}) | Board: {board} | Pot: {pot_val:.2f} | Stack: {stack_val:.2f}")
        
        try:
            my_env = os.environ.copy()
            my_env["LD_LIBRARY_PATH"] = "/opt/rocm/core-7.13/lib:" + my_env.get("LD_LIBRARY_PATH", "")
            subprocess.run([solver_path, "-c", "-i", f"scripts/{script_file}"], check=True, env=my_env, cwd=".")
            completed += 1
        except subprocess.CalledProcessError as e:
            print(f"Error solving sample {i+1}: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate Training Data for Neural Network")
    parser.add_argument("--samples", type=int, default=10, help="Number of samples to generate")
    args = parser.parse_args()
    
    generate_samples(args.samples)
