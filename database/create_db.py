import psycopg2
from psycopg2.extensions import ISOLATION_LEVEL_AUTOCOMMIT
import os
from dotenv import load_dotenv

DB_SOLVER = {
    "host": os.environ.get("DB_SOLVER_HOST", "localhost"),
    "port": int(os.environ.get("DB_SOLVER_PORT", 5432)),
    "database": os.environ.get("DB_SOLVER", "dbname"),
    "user": os.environ.get("DB_SOLVER_USER", "postgres"),
    "password": os.environ.get("DB_SOLVER_PASSWORD", "texassolver")
}

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

def create_database():
    # Connect to default postgres DB
    setup_params = DB_SOLVER.copy()
    setup_params["database"] = "postgres"

    conn = psycopg2.connect(
        **setup_params
    )
    conn.set_isolation_level(ISOLATION_LEVEL_AUTOCOMMIT)
    cursor = conn.cursor()
    
    # Check if database exists
    cursor.execute(f"SELECT 1 FROM pg_catalog.pg_database WHERE datname = '{DB_SOLVER['database']}'")
    exists = cursor.fetchone()
    
    if not exists:
        print(f"Creating database {DB_SOLVER['database']}...")
        cursor.execute(f"CREATE DATABASE {DB_SOLVER['database']}")
    else:
        print(f"Database {DB_SOLVER['database']} already exists.")
        
    cursor.close()
    conn.close()

def apply_schema():
    print(f"Applying schema to {DB_SOLVER['database']}...")
    conn = psycopg2.connect(
        **DB_SOLVER
    )
    cursor = conn.cursor()
    
    schema_path = os.path.join(os.path.dirname(__file__), "pg_schema.sql")
    with open(schema_path, "r") as f:
        schema_sql = f.read()
        
    cursor.execute(schema_sql)
    conn.commit()
    print("Schema applied successfully!")
    
    cursor.close()
    conn.close()

if __name__ == "__main__":
    create_database()
    apply_schema()
