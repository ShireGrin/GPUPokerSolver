-- PostgreSQL Schema for TexasSolver standalone database (db)

-- Disable triggers and foreign keys temporarily during schema setup if needed
SET statement_timeout = 0;
SET lock_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

-- Create tables
CREATE TABLE IF NOT EXISTS sites (
    id_site SERIAL PRIMARY KEY,
    site_name VARCHAR(100) UNIQUE NOT NULL
);

CREATE TABLE IF NOT EXISTS tourneys (
    id_tourney SERIAL PRIMARY KEY,
    id_site INT REFERENCES sites(id_site) ON DELETE CASCADE,
    tourney_no VARCHAR(100) NOT NULL,
    buyin NUMERIC(10, 2),
    currency VARCHAR(10) DEFAULT 'USD',
    date_played TIMESTAMP,
    UNIQUE (id_site, tourney_no)
);

CREATE TABLE IF NOT EXISTS players (
    id_player SERIAL PRIMARY KEY,
    id_site INT REFERENCES sites(id_site) ON DELETE CASCADE,
    player_name VARCHAR(100) NOT NULL,
    UNIQUE (id_site, player_name)
);

CREATE TABLE IF NOT EXISTS hands (
    id_hand SERIAL PRIMARY KEY,
    id_tourney INT REFERENCES tourneys(id_tourney) ON DELETE CASCADE,
    hand_no VARCHAR(100) NOT NULL,
    date_played TIMESTAMP,
    cnt_players INT,
    bb_size NUMERIC(10, 2),
    ante NUMERIC(10, 2),
    pot NUMERIC(10, 2),
    effective_stack NUMERIC(10, 2),
    board VARCHAR(50),
    ip_player_id INT REFERENCES players(id_player) ON DELETE SET NULL,
    oop_player_id INT REFERENCES players(id_player) ON DELETE SET NULL,
    turn_card VARCHAR(10),
    river_card VARCHAR(10),
    UNIQUE (id_tourney, hand_no)
);

CREATE TABLE IF NOT EXISTS profile_clusters (
    id_cluster SERIAL PRIMARY KEY,
    cluster_name VARCHAR(100) UNIQUE NOT NULL,
    vpip_mean NUMERIC(5, 2),
    pfr_mean NUMERIC(5, 2),
    threebet_mean NUMERIC(5, 2),
    af_mean NUMERIC(5, 2),
    wtsd_mean NUMERIC(5, 2),
    ranges_json JSONB,
    betting_profiles_json JSONB
);

CREATE TABLE IF NOT EXISTS player_profiles (
    id_player_profile SERIAL PRIMARY KEY,
    id_player INT REFERENCES players(id_player) ON DELETE CASCADE,
    id_cluster INT REFERENCES profile_clusters(id_cluster) ON DELETE SET NULL,
    vpip NUMERIC(5, 2),
    pfr NUMERIC(5, 2),
    threebet NUMERIC(5, 2),
    af NUMERIC(5, 2),
    wtsd NUMERIC(5, 2),
    cnt_hands INT,
    stack_depth_bb_min NUMERIC(5, 1) DEFAULT 0.0,
    stack_depth_bb_max NUMERIC(5, 1) DEFAULT 999.9,
    blind_level_min NUMERIC(10, 2) DEFAULT 0.0, -- Small blind value
    blind_level_max NUMERIC(10, 2) DEFAULT 999999.9, -- Small blind value
    UNIQUE (id_player, stack_depth_bb_min, stack_depth_bb_max, blind_level_min, blind_level_max)
);

CREATE TABLE IF NOT EXISTS player_gto_decisions (
    id_decision SERIAL PRIMARY KEY,
    id_hand INT REFERENCES hands(id_hand) ON DELETE CASCADE,
    id_player INT REFERENCES players(id_player) ON DELETE CASCADE,
    street VARCHAR(10), -- PREFLOP, FLOP, TURN, RIVER
    action_sequence TEXT,
    action_taken VARCHAR(50),
    gto_action VARCHAR(50),
    ev_action NUMERIC(10, 4),
    ev_gto NUMERIC(10, 4),
    ev_loss NUMERIC(10, 4)
);

CREATE TABLE IF NOT EXISTS solves (
    id_solve SERIAL PRIMARY KEY,
    id_hand INT REFERENCES hands(id_hand) ON DELETE CASCADE,
    config_text TEXT,
    solve_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    exploitability NUMERIC(10, 6),
    strategy_blob BYTEA
);

CREATE TABLE IF NOT EXISTS nn_training_data (
    id_training_data SERIAL PRIMARY KEY,
    board VARCHAR(50),
    oop_range TEXT,
    ip_range TEXT,
    pot NUMERIC(10, 2),
    effective_stack NUMERIC(10, 2),
    oop_ev NUMERIC(10, 4),
    ip_ev NUMERIC(10, 4)
);
