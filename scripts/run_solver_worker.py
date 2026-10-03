import os
import subprocess
import psycopg2
import time
import argparse
from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

def get_db_connection():
    conn = psycopg2.connect(
        host=os.environ.get("DB_SOLVER_HOST", "localhost"),
        port=int(os.environ.get("DB_SOLVER_PORT", "5432")),
        database=os.environ.get("DB_SOLVER", "dbname"),
        user=os.environ.get("DB_SOLVER_USER", "postgres"),
        password=os.environ.get("DB_SOLVER_PASSWORD", "dbpass")
    )
    return conn

def run_worker(solver_path="./build/TexasSolverGui", max_jobs=None):
    conn = get_db_connection()
    conn.autocommit = True  # We will handle transactions manually for the select
    
    jobs_processed = 0
    
    while True:
        if max_jobs is not None and jobs_processed >= max_jobs:
            print(f"Reached max jobs ({max_jobs}). Exiting.")
            break
            
        cur = conn.cursor()
        
        # 1. Grab a pending job
        cur.execute("""
            SELECT id, board_text, pot, effective_stack, oop_range_text, ip_range_text 
            FROM training_samples 
            WHERE status = 'pending' 
            FOR UPDATE SKIP LOCKED 
            LIMIT 1
        """)
        
        row = cur.fetchone()
        if not row:
            print("No pending jobs found. Waiting...")
            time.sleep(5)
            cur.close()
            continue
            
        row_id, board, pot_val, stack_val, oop_range, ip_range = row
        
        # Mark as processing
        cur.execute("UPDATE training_samples SET status = 'processing' WHERE id = %s", (row_id,))
        conn.commit()
        
        print(f"Processing Job {row_id} | Board: {board} | Pot: {pot_val:.2f} | Stack: {stack_val:.2f}")
        
        # 2. Build the batch script
        def build_script(use_fp16=False):
            script = f"""set_pot {pot_val:.2f}
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
"""
            if use_fp16:
                script += "set_use_fp16 1\n"
            script += f"""set_accuracy 0.05
set_max_iteration 1200
set_print_interval 200
start_solve
dump_training_sample {row_id},0.05,0.0
"""
            return script

        script_file = f"scripts/batch_script_{row_id}.txt"
        with open(script_file, "w") as f:
            f.write(build_script(use_fp16=False))
            
        # 3. Run Solver
        start_time = time.time()
        try:
            my_env = os.environ.copy()
            my_env["LD_LIBRARY_PATH"] = "/opt/rocm/core-7.13/lib:" + my_env.get("LD_LIBRARY_PATH", "")
            
            # Using Popen to capture status 255 gracefully
            # Run from the project root (where build/TexasSolverGui is located)
            result = subprocess.run([solver_path, "-c", "-i", script_file], 
                                    env=my_env, check=False)
            
            duration = time.time() - start_time
            
            if result.returncode == 255:
                print(f"Job {row_id} exceeded VRAM limit in FP32. Retrying with FP16...")
                
                # Overwrite script file with FP16 parameter correctly placed before start_solve
                with open(script_file, "w") as f:
                    f.write(build_script(use_fp16=True))
                
                # Retry solver
                start_time_fp16 = time.time()
                result = subprocess.run([solver_path, "-c", "-i", script_file],
                                        env=my_env, check=False)
                duration = time.time() - start_time_fp16
                
                if result.returncode == 255:
                    print(f"Job {row_id} exceeded VRAM limit even with FP16. Marking as skipped.")
                    cur.execute("UPDATE training_samples SET status = 'vram_exceeded' WHERE id = %s", (row_id,))
                elif result.returncode != 0:
                    print(f"Job {row_id} failed with exit code {result.returncode} during FP16 retry.")
                    cur.execute("UPDATE training_samples SET status = 'error' WHERE id = %s", (row_id,))
                else:
                    cur.execute("UPDATE training_samples SET solve_duration_sec = %s WHERE id = %s AND status = 'solved'", (duration, row_id))
                    print(f"Job {row_id} solved successfully in {duration:.1f}s (with FP16).")
            elif result.returncode != 0:
                print(f"Job {row_id} failed with exit code {result.returncode}.")
                cur.execute("UPDATE training_samples SET status = 'error' WHERE id = %s", (row_id,))
            else:
                # The C++ solver sets status='solved', but we want to log duration.
                cur.execute("UPDATE training_samples SET solve_duration_sec = %s WHERE id = %s AND status = 'solved'", (duration, row_id))
                print(f"Job {row_id} solved successfully in {duration:.1f}s.")
                
            conn.commit()
            jobs_processed += 1
            
        except Exception as e:
            print(f"Error executing solver for Job {row_id}: {e}")
            cur.execute("UPDATE training_samples SET status = 'error' WHERE id = %s", (row_id,))
            conn.commit()
            
        cur.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Consume and solve pending scenarios")
    parser.add_argument("--count", type=int, default=None, help="Number of jobs to process before exiting")
    args = parser.parse_args()
    
    run_worker(max_jobs=args.count)
