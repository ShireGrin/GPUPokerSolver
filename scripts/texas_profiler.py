import psycopg2
from psycopg2.extras import execute_values
import pandas as pd
import numpy as np
from sklearn.cluster import KMeans
from sklearn.preprocessing import StandardScaler
import json
import os

# Database configs
DB_PT4 = {
    "host": os.environ.get("DB_PT4_HOST", "localhost"),
    "port": int(os.environ.get("DB_PT4_PORT", 5432)),
    "database": os.environ.get("DB_PT4", "dbname"),
    "user": os.environ.get("DB_PT4_USER", "postgres"),
    "password": os.environ.get("DB_PT4_PASSWORD", "dbpass")
}

DB_SOLVER = {
    "host": os.environ.get("DB_SOLVER_HOST", "localhost"),
    "port": int(os.environ.get("DB_SOLVER_PORT", 5432)),
    "database": os.environ.get("DB_SOLVER", "dbname"),
    "user": os.environ.get("DB_SOLVER_USER", "postgres"),
    "password": os.environ.get("DB_SOLVER_PASSWORD", "texassolver")
}


def extract_player_bucket_stats():
    print("Connecting to PokerTracker 4 Database...")
    try:
        conn = psycopg2.connect(**DB_PT4)
    except Exception as e:
        print(f"Failed to connect to PT4 database: {e}")
        return None
    
    # Aggregated query with stack depth & stage buckets
    query = """
    SELECT 
        p.player_name,
        -- Stack buckets: Short (<15 BB), Medium (15-30 BB), Deep (30-50 BB), Very Deep (50+ BB)
        CASE 
            WHEN (b.amt_bb > 0 AND (ps.amt_before / b.amt_bb) < 15) THEN 0.0
            WHEN (b.amt_bb > 0 AND (ps.amt_before / b.amt_bb) >= 15 AND (ps.amt_before / b.amt_bb) < 30) THEN 15.0
            WHEN (b.amt_bb > 0 AND (ps.amt_before / b.amt_bb) >= 30 AND (ps.amt_before / b.amt_bb) < 50) THEN 30.0
            ELSE 50.0
        END as stack_min,
        CASE 
            WHEN (b.amt_bb > 0 AND (ps.amt_before / b.amt_bb) < 15) THEN 15.0
            WHEN (b.amt_bb > 0 AND (ps.amt_before / b.amt_bb) >= 15 AND (ps.amt_before / b.amt_bb) < 30) THEN 30.0
            WHEN (b.amt_bb > 0 AND (ps.amt_before / b.amt_bb) >= 30 AND (ps.amt_before / b.amt_bb) < 50) THEN 50.0
            ELSE 999.9
        END as stack_max,
        -- Stage buckets: Early (BB <= 200), Mid (200 < BB <= 1000), Late (BB > 1000)
        CASE 
            WHEN b.amt_bb <= 200 THEN 0.0
            WHEN b.amt_bb > 200 AND b.amt_bb <= 1000 THEN 200.0
            ELSE 1000.0
        END as blind_min,
        CASE 
            WHEN b.amt_bb <= 200 THEN 200.0
            WHEN b.amt_bb > 200 AND b.amt_bb <= 1000 THEN 1000.0
            ELSE 999999.9
        END as blind_max,
        COUNT(ps.id_hand) as cnt_hands,
        SUM(CASE WHEN ps.flg_vpip THEN 1 ELSE 0 END) as cnt_vpip,
        SUM(CASE WHEN ps.cnt_p_raise > 0 THEN 1 ELSE 0 END) as cnt_pfr,
        SUM(CASE WHEN ps.flg_blind_b AND NOT ps.flg_p_face_raise THEN 1 ELSE 0 END) as cnt_walks, 
        SUM(CASE WHEN ps.flg_p_3bet_opp THEN 1 ELSE 0 END) as cnt_p_3bet_opp,
        SUM(CASE WHEN ps.flg_p_3bet THEN 1 ELSE 0 END) as cnt_p_3bet,
        SUM(
            (CASE WHEN ps.flg_f_bet THEN 1 ELSE 0 END) + ps.cnt_f_raise + 
            (CASE WHEN ps.flg_t_bet THEN 1 ELSE 0 END) + ps.cnt_t_raise + 
            (CASE WHEN ps.flg_r_bet THEN 1 ELSE 0 END) + ps.cnt_r_raise
        ) as cnt_aggr,
        SUM(ps.cnt_f_call + ps.cnt_t_call + ps.cnt_r_call) as cnt_pass,
        SUM(CASE WHEN ps.flg_showdown THEN 1 ELSE 0 END) as cnt_wtsd,
        SUM(CASE WHEN ps.flg_f_saw THEN 1 ELSE 0 END) as cnt_f_saw
    FROM tourney_hand_player_statistics ps
    JOIN player p ON ps.id_player = p.id_player
    JOIN tourney_hand_summary s ON ps.id_hand = s.id_hand
    JOIN tourney_blinds b ON s.id_blinds = b.id_blinds
    GROUP BY p.player_name, stack_min, stack_max, blind_min, blind_max
    HAVING COUNT(ps.id_hand) >= 10
    """
    
    df = pd.read_sql_query(query, conn)
    conn.close()
    
    if df.empty:
        print("No players/buckets found with >= 10 hands.")
        return df
        
    # Calculate stats
    df['VPIP'] = np.where((df['cnt_hands'] - df['cnt_walks']) > 0, 
                          (df['cnt_vpip'] / (df['cnt_hands'] - df['cnt_walks'])) * 100, 0)
    df['PFR'] = np.where(df['cnt_hands'] > 0, 
                         (df['cnt_pfr'] / df['cnt_hands']) * 100, 0)
    df['3Bet'] = np.where(df['cnt_p_3bet_opp'] > 0, 
                          (df['cnt_p_3bet'] / df['cnt_p_3bet_opp']) * 100, 0)
    df['AF'] = np.where(df['cnt_pass'] > 0, 
                        df['cnt_aggr'] / df['cnt_pass'], df['cnt_aggr'])
    df['WTSD'] = np.where(df['cnt_f_saw'] > 0,
                          (df['cnt_wtsd'] / df['cnt_f_saw']) * 100, 0)
                          
    return df

def cluster_profiles(df):
    print("Clustering player-bucket profiles...")
    features = ['VPIP', 'PFR', '3Bet', 'AF', 'WTSD']
    X = df[features].fillna(0)
    
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)
    
    num_clusters = min(8, len(df))
    kmeans = KMeans(n_clusters=num_clusters, random_state=42, n_init=10)
    df['Cluster'] = kmeans.fit_predict(X_scaled)
    
    centroids = pd.DataFrame(scaler.inverse_transform(kmeans.cluster_centers_), columns=features)
    
    centroids['ClusterID'] = centroids.index
    
    profile_names = []
    for idx, row in centroids.iterrows():
        vpip = row['VPIP']
        pfr = row['PFR']
        af = row['AF']
        
        name = ""
        if vpip < 15:
            if af > 2: name = "Nit-Aggressive"
            else: name = "Nit"
        elif vpip < 25:
            if af > 2: name = "Tight-Aggressive (TAG)"
            else: name = "Tight-Passive"
        elif vpip < 40:
            if pfr < 10: name = "Loose-Passive (Calling Station)"
            elif af > 2.5: name = "Loose-Aggressive (LAG)"
            else: name = "Loose-Limper"
        else:
            if af > 2.5: name = "Aggressive Maniac"
            else: name = "Whale"
            
        base_name = name
        counter = 1
        while name in profile_names:
            name = f"{base_name} {counter}"
            counter += 1
        profile_names.append(name)
        
    cluster_to_profile = {}
    for idx, row in centroids.iterrows():
        cluster_to_profile[int(row['ClusterID'])] = profile_names[idx]
        
    df['profile_name'] = df['Cluster'].map(cluster_to_profile)
    return df

def write_to_ts_db(df):
    print("Writing profiles to standing database (optimized)...")
    try:
        conn = psycopg2.connect(**DB_SOLVER)
        cursor = conn.cursor()
    except Exception as e:
        print(f"Failed to connect to standing database: {e}")
        return
        
    cursor.execute("SELECT id_cluster, cluster_name FROM profile_clusters")
    cluster_map = {name: id_c for id_c, name in cursor.fetchall()}
    
    cursor.execute("SELECT id_site FROM sites WHERE site_name = 'PokerStars'")
    site_id = cursor.fetchone()[0]

    unique_players = df['player_name'].unique()
    print(f"Ensuring {len(unique_players)} players exist in players table...")
    
    # Batch insert players in batches of 500
    batch_size = 500
    for i in range(0, len(unique_players), batch_size):
        batch = unique_players[i:i+batch_size]
        # Build multi-row insert query safely
        placeholders = ",".join(["(%s, %s)"] * len(batch))
        params = []
        for p in batch:
            params.extend([site_id, p])
        cursor.execute(f"INSERT INTO players (id_site, player_name) VALUES {placeholders} ON CONFLICT (id_site, player_name) DO NOTHING", params)
    
    print("Fetching all player IDs...")
    cursor.execute("SELECT player_name, id_player FROM players WHERE id_site = %s", (site_id,))
    player_id_map = {name: pid for name, pid in cursor.fetchall()}

    print(f"Preparing {len(df)} player profiles for insert...")
    profile_data = []
    for _, row in df.iterrows():
        player_name = row['player_name']
        profile_name = row['profile_name']
        cluster_id = cluster_map.get(profile_name)
        
        if cluster_id is None:
            continue
            
        player_id = player_id_map.get(player_name)
        if player_id is None:
            continue
            
        profile_data.append((
            player_id, cluster_id, float(row['VPIP']), float(row['PFR']), float(row['3Bet']), 
            float(row['AF']), float(row['WTSD']), int(row['cnt_hands']), 
            float(row['stack_min']), float(row['stack_max']), float(row['blind_min']), float(row['blind_max'])
        ))

    insert_query = """
        INSERT INTO player_profiles (
            id_player, id_cluster, vpip, pfr, threebet, af, wtsd, cnt_hands, 
            stack_depth_bb_min, stack_depth_bb_max, blind_level_min, blind_level_max
        ) VALUES %s
        ON CONFLICT (id_player, stack_depth_bb_min, stack_depth_bb_max, blind_level_min, blind_level_max) 
        DO UPDATE SET 
            id_cluster = EXCLUDED.id_cluster,
            vpip = EXCLUDED.vpip,
            pfr = EXCLUDED.pfr,
            threebet = EXCLUDED.threebet,
            af = EXCLUDED.af,
            wtsd = EXCLUDED.wtsd,
            cnt_hands = EXCLUDED.cnt_hands
    """
    
    print("Executing batch profiles insertion...")
    for i in range(0, len(profile_data), batch_size):
        batch = profile_data[i:i+batch_size]
        execute_values(cursor, insert_query, batch)
        
    conn.commit()
    print(f"Successfully processed and stored {len(profile_data)} player-bucket profiles in database!")
    cursor.close()
    conn.close()

def main():
    df = extract_player_bucket_stats()
    if df is None or df.empty:
        return
    print(f"Extracted stats for {len(df)} player-bucket combinations.")
    df = cluster_profiles(df)
    write_to_ts_db(df)

if __name__ == "__main__":
    main()
