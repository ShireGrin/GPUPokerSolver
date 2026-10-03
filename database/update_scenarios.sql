TRUNCATE TABLE cluster_ranges;
TRUNCATE TABLE training_samples;
DELETE FROM action_scenarios;

INSERT INTO action_scenarios (id_scenario, name, description) VALUES
(1, 'RFI', 'Raise First In'),
(2, 'limp', 'Limping Preflop'),
(3, 'call_vs_pfr', 'Cold Calling a Raise'),
(4, '3bet_vs_pfr', '3-Betting vs PFR'),
(5, '4bet_vs_3bet', '4-Betting vs 3-Bet');

SELECT setval('action_scenarios_id_scenario_seq', (SELECT MAX(id_scenario) FROM action_scenarios));
