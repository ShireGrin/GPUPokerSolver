import psycopg2
import os
from dotenv import load_dotenv
from value_network import PokerValueNetwork
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

class PokerDataset(Dataset):
    def __init__(self, db_params):
        self.conn = psycopg2.connect(**db_params)
        self.cur = self.conn.cursor()
        
        # We can fetch all IDs and then fetch data on the fly, or just load everything into memory if it fits
        self.cur.execute("SELECT id FROM training_samples WHERE status = 'solved'")
        self.ids = [row[0] for row in self.cur.fetchall()]
        print(f"Dataset initialized with {len(self.ids)} samples.")
        
    def __len__(self):
        return len(self.ids)
        
    def __getitem__(self, idx):
        sample_id = self.ids[idx]
        self.cur.execute("""
            SELECT board_text, pot, effective_stack, spr, oop_range, ip_range, 
                   oop_evs, ip_evs, id_cluster_oop, id_cluster_ip, id_position_oop, 
                   id_position_ip, id_scenario, id_stack_cluster
            FROM training_samples WHERE id = %s
        """, (sample_id,))
        
        row = self.cur.fetchone()
        
        # Parse board string to 52-dim vector
        board_text = row[0] # e.g. 'As,Kd,2h'
        board_vec = np.zeros(52, dtype=np.float32)
        if board_text:
            ranks = "23456789TJQKA"
            suits = "cdhs"
            deck = [r+s for r in ranks for s in suits]
            for card in board_text.split(','):
                if card in deck:
                    board_vec[deck.index(card)] = 1.0
                
        # Cont features
        pot_stack_spr = np.array([row[1], row[2], row[3]], dtype=np.float32)
        
        # Ranges
        oop_range = np.array(row[4], dtype=np.float32)
        ip_range = np.array(row[5], dtype=np.float32)
        
        # Targets
        ev_oop = np.array(row[6], dtype=np.float32)
        ev_ip = np.array(row[7], dtype=np.float32)
        
        # Categorical
        oop_c_id = row[8]
        ip_c_id = row[9]
        oop_p_id = row[10]
        ip_p_id = row[11]
        scen_id = row[12]
        stack_c_id = row[13]
        
        return (
            torch.tensor(board_vec), 
            torch.tensor(pot_stack_spr), 
            torch.tensor(oop_range), 
            torch.tensor(ip_range),
            torch.tensor(oop_c_id, dtype=torch.long), 
            torch.tensor(ip_c_id, dtype=torch.long), 
            torch.tensor(oop_p_id, dtype=torch.long), 
            torch.tensor(ip_p_id, dtype=torch.long),
            torch.tensor(scen_id, dtype=torch.long), 
            torch.tensor(stack_c_id, dtype=torch.long),
            torch.tensor(ev_oop), 
            torch.tensor(ev_ip)
        )

def train_model():
    db_params = {
        "host": os.environ.get("DB_SOLVER_HOST", "localhost"),
        "port": int(os.environ.get("DB_SOLVER_PORT", 5432)),
        "database": os.environ.get("DB_SOLVER", "dbname"),
        "user": os.environ.get("DB_SOLVER_USER", "postgres"),
        "password": os.environ.get("DB_SOLVER_PASSWORD", "dbpass")
    }
    
    dataset = PokerDataset(db_params)
    if len(dataset) == 0:
        print("No training data found in database. Run batch generator first!")
        return
        
    dataloader = DataLoader(dataset, batch_size=32, shuffle=True, num_workers=0)
    
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on device: {device}")
    
    # Define model
    # Note: Ensure num_clusters etc. are large enough to cover the max IDs in DB
    model = PokerValueNetwork(
        num_clusters=100, 
        num_positions=20, 
        num_scenarios=100, 
        num_stack_clusters=20
    ).to(device)

    if os.path.exists("value_network.pth"):
        print("Found existing value_network.pth checkpoint. Loading weights to continue training...")
        model.load_state_dict(torch.load("value_network.pth", weights_only=True))
    else:
        print("No existing checkpoint found. Initializing model from scratch.")
    
    
    criterion = nn.MSELoss()
    optimizer = optim.Adam(model.parameters(), lr=1e-3)
    
    num_epochs = 20
    
    for epoch in range(num_epochs):
        model.train()
        total_loss = 0.0
        
        for batch in dataloader:
            (board_vec, pot_stack_spr, oop_range, ip_range, 
             oop_c_id, ip_c_id, oop_p_id, ip_p_id, scen_id, stack_c_id, 
             ev_oop, ev_ip) = [b.to(device) for b in batch]
             
            optimizer.zero_grad()
            
            pred_oop, pred_ip = model(
                board_vec, pot_stack_spr, oop_range, ip_range,
                oop_c_id, ip_c_id, oop_p_id, ip_p_id, scen_id, stack_c_id
            )
            
            # Loss is MSE between predicted EVs and actual CFR EVs
            # We can optionally mask out hands with 0 reach prob from the loss, but for EVs, BestResponse gives EVs for all 1326!
            loss_oop = criterion(pred_oop, ev_oop)
            loss_ip = criterion(pred_ip, ev_ip)
            loss = loss_oop + loss_ip
            
            loss.backward()
            optimizer.step()
            
            total_loss += loss.item()
            
        print(f"Epoch {epoch+1}/{num_epochs} | Loss: {total_loss/len(dataloader):.6f}")
        
    torch.save(model.state_dict(), "value_network.pth")
    print("Training complete. Model saved to value_network.pth")

if __name__ == "__main__":
    train_model()
