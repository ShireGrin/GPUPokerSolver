# Multi-GPU analysis of HipPCfrSolver

Analysis only. No solver code is changed by this note. It describes the AMD HIP postflop solver as it exists today, what actually fills a 16 GB card, and an opt-in way to use two Radeon RX 9060 XT cards without changing the single-GPU path.

The two cards are identical gfx1200 parts (RDNA4, 16 GB GDDR6, consumer PCIe, no Infinity Fabric / xGMI between them). The solver already runs on that GPU. `run.sh` prepends `/opt/rocm/lib` and starts `build/GPUPokerSolver`, and the training worker uses `/opt/rocm/core-7.13/lib`. A shell whose `hipcc --version` prints HIP 5.7 / clang 17 is not the compiler this build uses. Details are in [Build and toolchain](#build-and-toolchain).

Numbers below come from the allocation formulas in `HipPCfrSolver::allocateDeviceMemory` and `printExpectedVramUsage`. They are not measurements from a 9060 XT. The log line `ESTIMATED TOTAL DEMAND` is the right thing to compare them against on the real machine.

## Bottom line

The GPU solver unrolls every turn and river card into its own copy of the betting tree, then keeps every copy's regrets and every leaf's counterfactual values resident for the whole solve. On a flop solve that product is about 49 × 48 = 2352 copies of the river tree. A 25bb tree with a few hundred combos fits in 16 GB. A deeper stack or a wider range multiplies the same arrays until it does not. Two cards, split by turn card, hold about twice that bulk. They do not turn an exponential betting tree into a 200bb full-range flop solve.

Leave the CFR update, the fp16 path, and the kernels alone for the first multi-GPU cut. Two ways to use the second card, in that order:

1. **Two processes, no data shared.** `scripts/run_solver_worker.py` already takes one training job at a time. Start a second copy with `HIP_VISIBLE_DEVICES=1`. Each solve is the current solver on one card. Nothing is transferred. A single pot is still limited to 16 GB. Throughput doubles. This is the change to make first.
2. **One larger solve.** Split turn cards (flop) or river cards (turn) across the cards and run the same kernels on each shard. The only values that cross PCIe each iteration are the reach vector going down at a chance node and the utility vector coming back up. Regrets and the average strategy stay on the GPU that owns that card. There is no neural-net activation vector in this loop. See [What crosses the bus](#what-crosses-the-bus).

The fp16 template bug and the 32-bit size counters are real. They are separate from adding a second GPU, and they change numerical behavior, so they stay out of the first cut.

---

## 1. How HipPCfrSolver works

### Where it sits relative to PCfrSolver

`HipPCfrSolver` extends `PCfrSolver`. The constructor chains to the CPU solver with isomorphism enabled, Monte Carlo off, warmup 0, and 12 OpenMP threads:

```cpp
// src/solver/HipPCfrSolver.cpp, HipPCfrSolver::HipPCfrSolver
: PCfrSolver(
            tree, range1, range2, initial_board, compairer, deck,
            iteration_number, false/*debug*/, print_interval, logfile, trainer,
            Solver::MonteCarolAlg::NONE, 0/*warmup*/, accuracy,
            true/*use_isomorphism*/, use_fp16/*use_halffloats*/, 12/*num_threads*/
            )
```

`PCfrSolver`'s constructor filters board-blocked combos, builds `PrivateCardsManager` / `RiverRangeManager`, and calls `setTrainable`. That allocates a vector of null `shared_ptr<Trainable>` on every action node, one slot per deal id. The CPU tree itself is not unrolled per card. A chance node has a single child. The deal id selects which trainable slot inside an action node is live. That is the TexasSolver / GPUPokerSolver layout the CPU CFR walk uses (`PCfrSolver::cfr`, `chanceUtility`, `actionUtility`).

The GPU path does not walk that tree per iteration. In the constructor, before `train()`:

1. `buildGpuStates()` flattens `(node, deal)` pairs into one `GpuState` array, BFS order.
2. `setupShowdowns()` builds showdown combo tables.
3. `allocateDeviceMemory()` uploads that structure and allocates the big buffers.

`train()` then captures one HIP graph of a full CFR iteration and replays it.

The CUDA class `CudaPCfrSolver` is a separate, older layout (a reach buffer and a utility buffer for every state, plus a third strategy array). It is not what runs on these AMD cards. The HIP solver already dropped the per-state reach buffer in favor of two level-sized ping-pong buffers. That change is real and it is already in the tree. It is not the 16 GB wall. The wall is the arrays that still scale with every card.

`use_fp16` is the UI checkbox `gpuFp16CheckBox`. `MainWindow` turns it into `use_halffloats = 2` when checked and `0` when not. `PokerSolver::buildSolverOnly` passes `(use_halffloats > 0)` into `HipPCfrSolver`. The GPU code has no separate SF vs HF mode. The checkbox is one boolean for the whole tree.

### Game tree to GpuState

`GpuState` is 32 bytes (six `int`s, one `float`, one `uint32_t`), defined in `include/solver/GpuTypes.h`:

| Field | Role |
| --- | --- |
| `child_start_idx` | First child in the state array. For a terminal or showdown, overloaded as the leaf-utility slot. |
| `trainable_offset` | Start of this infoset in the flat regret / cumulative-strategy arrays. `-1` if the node has none. |
| `range_size` | Acting player's combo count. |
| `pot` | Node pot. For a fold terminal, replaced by the amount the folder lost. |
| `deal_id` | Same encoding the CPU uses. |
| `parent_idx`, `action_idx_in_parent` | Tree links. |
| `packed` | `node_type` (2 bits), `player+1` (2), `action_count` (8), `turn_card` (7), `river_card` (7). |

Node type values match `GameTreeNode::GameTreeNodeType`: action 0, showdown 1, terminal 2, chance 3. Player 0 in this program is IP and player 1 is OOP (`Rule::get_commit`, root created with `current_player = 1` in `GameTree::GameTree`). Kernel comments that call player 0 "OOP" are stale. The math uses the tree's player index.

Deal ids, from `buildGpuStates`:

- `0` — root street. No dealt turn/river yet. The initial board is only in `initial_board_long`. Hands that collide with it are removed by `noDuplicateRange` before this runs.
- `1 .. card_num` — first chance card. On a flop solve that is the turn. On a turn solve the same field is the river. `card_num` is `deck.getCards().size()` (52 for hold'em).
- `card_num * turn_index + river_index + (1 + card_num)` — river under a turn. Same formula as `PCfrSolver::chanceUtility`.

`action_count` for a chance node is the number of children that do not collide with the board already dealt, not the raw deck size. It has to fit in 8 bits. A 52-card deck fits (at most 49 live turn cards). A generated tree with more than 255 bet sizes on one node would truncate. Normal sizings do not.

BFS pushes children onto the queue while it processes the parent, and records `(level_start, level_count)` pairs in `h_levels`. Within a level, state indices are contiguous. The next level starts at `h_levels[L+1].first`. Kernels index reach and utility as `(state_index - level_start) * (n0 + n1) + hand`. That is why the scratch buffers only need to be as wide as the fattest level, not as long as the whole tree.

There is no suit isomorphism in this unroll. `train()` does call `findGameSpecificIsomorphisms()`, but only after the GPU arrays exist, and nothing in `buildGpuStates` reads `color_iso_offset`. The CPU best-response walk skips non-canonical suits and copies them by suit permutation. The GPU allocates and updates every suit. VRAM is the full 49 × 48, not the isomorphism-reduced set (often closer to half of that on the turn, texture-dependent).

### What is stored in VRAM

`allocateDeviceMemory` creates these device buffers. `prec` is 2 bytes when `use_fp16` is set and 4 otherwise. `R = n0 + n1` is both ranges. `T = total_trainable_size` is the sum, over action states, of `action_count * acting_range`.

| Buffer | Size | Lives for |
| --- | --- | --- |
| `d_states` | `N_states * 32` | whole solve, static |
| `d_regrets` | `T * prec` | whole solve, updated every iteration |
| `d_cum_strategies` | `T * prec` | whole solve, updated every iteration |
| `d_locked` | `T / 8` bytes, one bit per trainable slot | whole solve, static after setup |
| `d_reach_probs_in`, `d_reach_probs_out` | each `max_level * R * 4` | ping-pong, one level at a time |
| `d_root_reach_probs` | `R * 4` | static copy of the root weights |
| `d_utilities_in`, `d_utilities_out` | each `max_level * R * 4` | ping-pong |
| `d_leaf_utilities` | `N_leaves * R * 4` | every terminal and showdown, all cards, all iteration |
| `d_showdowns` | one `ShowdownPrecalc` (5 ints) per showdown state | static |
| `d_showdown_combs` | one `GpuShowdownComb` (4 ints, 16 bytes) per combo per **unique board** | static, cached on `board_long` |
| `d_range_cards_0/1` | `n_player * 8` | static hole-card pairs |
| `d_p0_to_p1_map`, `d_p1_to_p0_map` | `n_player * 4` | same-hand index for fold card removal |
| `d_level_starts`, `d_level_counts` | a few ints | uploaded, not read by the current launches |
| `d_dcfr_coeffs` | 8 bytes | rewritten every iteration |

Reach, both utility ping-pong buffers, and leaf utilities are **always float**, including in fp16 mode. Only regrets and cumulative strategy follow `prec`. The kernels do not honor that split. See [fp16 reach/utility mismatch](#fp16-mode-reads-float-reach-and-utilities-as-half).

Host-side mirrors allocated in the constructor, not in VRAM:

- `h_states`, `h_showdowns`, `h_showdown_combs`
- `h_regrets` and `h_cum_strategies`, both `T` floats, even when the device copy is fp16
- `h_locked`, bit-packed

`train()` also calls `hipHostMalloc` for `T` floats (`h_async_buffer`) and never reads it or frees it.

Showdown combo tables are cached by the 64-bit board mask, so a flop's `C(49,2) = 1176` distinct rivers are stored once, not once per betting node. That buffer is megabytes. The per-node `ShowdownPrecalc` rows are small next to leaf utilities.

### One iteration

`train()` builds a HIP graph on `compute_stream` the first time through (`hipStreamBeginCapture` / `hipGraphInstantiate`). Every later iteration is:

1. Write two DCFR coefficients to `d_dcfr_coeffs` (`hipMemcpyAsync` on the same stream, **outside** the graph).
2. `hipGraphLaunch`.
3. Every 5 iterations, `hipDeviceSynchronize` and sleep 2 ms.
4. Every `print_interval`, synchronize, copy cumulative strategy back, run a full CPU CFR pass for EVs (`calculateEvs`), and run CPU best response.

Inside the captured graph, in order:

1. Zero `d_utilities_in`, `d_utilities_out`, and the entire `d_leaf_utilities`.
2. Device-to-device copy of `d_root_reach_probs` onto the level-0 reach buffer.
3. **Forward**, level 0 .. last:
   - `forward_pass_kernel` reads this level's reach, writes the next level's reach.
   - `showdown_pass_kernel` for showdowns on this level, reading this level's reach, writing `d_leaf_utilities`.
   - `eval_terminal_nodes_kernel` for fold nodes on this level, same reach, same leaf buffer.
   - Swap the reach ping-pong pointers.
4. **Backward**, last level .. 0:
   - `backward_pass_kernel` reads child utilities (leaf buffer or the next level's utility buffer), writes this level's utilities, and updates regrets and cumulative strategy on action nodes.
   - Swap the utility ping-pong pointers.
   - `reach_probs` is passed as `nullptr`. The backward kernel never reads reach.

There is no host/device copy inside the iteration besides the 8-byte coefficient update. States, regrets, reach, and utilities stay on the device. The graph is legal only because the coefficient buffer's **address** is captured and the **values** are refreshed before each launch.

Launches (`src/solver/HipKernels.hip`):

| Kernel | Grid | What one thread does |
| --- | --- | --- |
| `forward_pass_kernel` | `(level_count, ceil(max(n0,n1)/128))`, block 128 | One hand at one node. Action: regret-match, multiply acting player's reach, copy the other player's reach, zero combos that contain the child card. Chance: multiply both reaches by `1/(action_count - 2)`. |
| `showdown_pass_kernel` | `(n_showdowns_on_level)`, block 128 | One showdown. Each thread walks the whole opposing combo list (`O(n0 * n1)` per node). |
| `eval_terminal_nodes_kernel` | `(level_count)`, block 128 | Fold payoff times opponent reach with card removal. Every thread recomputes the same 52-card sums into a private array. |
| `backward_pass_kernel` | same shape as forward | Action: strategy-weighted value, regret delta, DCFR scale, cumulative strategy. Chance: sum child values for hands the dealt card does not block. |

`hip_initialize_device()` is `hipSetDevice(0)` and nothing else. Device 1 is never selected.

### Regret update versus the CPU trainer

The intended algorithm is `DiscountedCfrTrainable::updateRegrets`, not textbook DCFR:

- `alpha_coef = t^1.5 / (t^1.5 + 1)` multiplies the regret **after** the new delta is added, and only when the result is positive.
- Negative regrets are multiplied by the constant `beta = 0.5` every iteration.
- Cumulative strategy is multiplied by `theta = 0.9`, then `current_strategy * (t/(t+1))^2` is added.
- The reach weight on that add is commented out in the CPU trainer (`// * reach_probs[private_id]`). The GPU matches that: average strategy is an exponential moving average of the current strategy, not a reach-weighted CFR average.

The GPU does **not** match the CPU's loop order. The CPU updates every action's regret, then computes one strategy from the finished regret vector, then accumulates. The GPU, inside the action loop, writes action `a`'s new regret and then calls `compute_strategy_prob`, which re-reads every action. Action 0 is accumulated against a half-updated vector and action `k` against a different one. The forward pass of the **next** iteration does regret-match on the finished vector, so the strategy that is played next iteration is consistent. The stored average is not. Fixing that is a single-GPU correctness fix. A dual-GPU run should be compared against the single-GPU path after that fix, or both paths will share the bug and still match each other.

Chance scaling does match the CPU for a full deck on the chance node. CPU `possible_deals = deck - board_cards - 2`. GPU `action_count` is `deck - board_cards` (collisions dropped) and the kernel divides by `action_count - 2`. Same denominator. Blocked combos get reach 0 instead of a per-hand denominator. That is the same approximation the CPU makes.

Showdown payoffs match the tree builder for the trees it actually builds. At showdown the commits are equal, the winner's payoff is the opponent's commit, and that equals `pot/2`. The GPU computes `(win_reach - lose_reach) * pot/2` with ties split half and half, which cancels. The CPU's two-pointer showdown leaves ties at 0, same result when win and lose payoffs are symmetric. Fold terminals store the folder's lost commit in `state.pot` and apply `± pot * opponent_reach` with the same card-removal correction the CPU uses (`oppo_sum - card_sum[c1] - card_sum[c2] + same_hand`).

### Node locking

Locks are CPU-side `Trainable::lockStrategy` on an action node's trainable for one deal (strategy explorer, command line). They are **not** part of `GpuState`.

At graph-capture time `train()` walks `action_state_map`. For a trainable with `isLocked()`:

- It sets a bit in `h_locked` for every action of every locked hand.
- It copies `getAverageStrategy()` into `h_cum_strategies` for those slots. For a fully locked node that vector **is** the locked strategy. For a partial mask, `getAverageStrategy` writes the locked rows over a normalized cumulative strategy.
- `copyToDevice()` uploads regrets (still zero) and cumulative strategy. `d_locked` is uploaded either way.

In the forward and backward kernels, `is_locked(d_locked, trainable_offset + hand)` checks the **action-0** bit of that hand. The setup sets every action's bit, so this means "hand is locked". A locked hand uses the stored cumulative-strategy value as a probability and skips the regret write. An unlocked hand regret-matches. The bitset is immutable for the solve; the graph bakes the pointer in.

A lock on deal `d` belongs to exactly one `(action node, deal)` state. If deals are sharded by turn card, that state and its lock bit live on one GPU. No lock data has to cross PCIe during the iteration.

### What the CPU is still doing during a GPU solve

`syncStrategiesToCpuTree` copies `d_cum_strategies` back, converts fp16 to float if needed, and calls `ActionNode::getTrainable(deal, true, use_halffloats)` for **every** unrolled action state. `getTrainable` allocates a trainer the first time. `setCumRegrets` replaces `cum_r_plus`. It does not copy device regrets into `r_plus`.

Which trainer it allocates is easy to get wrong. `ActionNode::getTrainable` uses half-float storage only on river nodes, and only then: `0` full float, `1` `DiscountedCfrTrainableSF` (float regrets, half EVs), `2` `DiscountedCfrTrainableHF` (half regrets and half cumulative strategy). The GPU checkbox sets `use_halffloats = 2`. `PokerSolver::buildSolverOnly` passes `(use_halffloats > 0)`, a bool, and `HipPCfrSolver` forwards that bool into `PCfrSolver`'s int. `true` becomes `1`. So a checked box selects SF for the CPU mirror on river nodes, and full float on flop and turn. HF is never constructed on this path. Flop and turn CPU trainables are five full float arrays even when the device regrets are half.

`train()` calls this before iteration 0, again every `print_interval`, and once at the end. `calculateEvs` then runs `PCfrSolver::cfr` on the CPU for both players with `use_average_strategy_for_cfr`, which reads that average and writes EVs. Best response is also the CPU `BestResponse` walk, isomorphism included.

So a GPU solve still materializes the CPU trainable set. Each CPU trainable holds on the order of five float arrays of length `actions * range` (`r_plus`, `cum_r_plus`, `evs`, current-strategy cache, average-strategy cache). That is several times the device regret buffer, and it happens even when the iteration math never reads it.

---

## 2. Memory

### What dominates

Ranked by bytes on a flop solve:

1. **`d_leaf_utilities`** — every fold and showdown, every turn, every river, both ranges, float. There is no level ping-pong here. Forward fills it; backward reads it at the end of the iteration; the next iteration memsets all of it.
2. **`d_regrets` + `d_cum_strategies`** — every action, every hand of the acting player, every card. fp16 halves this pair and does not touch (1).
3. **Level scratch** — four float buffers (reach in/out, utility in/out) sized to the widest BFS level. On a flop solve the wide level is a slice of the river, so this is a fraction of the river, not the whole river. Usually third.
4. **Showdown combo tables, `GpuState`, lock bits, range maps.** Tens to a few hundred MB. Not the decision.

The CUDA solver's per-state reach and utility arrays would have been larger than (1) and (3) combined (`N_states * R * 4 * 2`). HIP already avoided that. The remaining big arrays are big because the **data** is big, not because the same data is stored twice.

### Scaling formula

For a flop root, with a 52-card deck and three board cards:

- `C_turn ≈ 49`, `C_river ≈ 48`, river multiplier `M_r = C_turn * C_river ≈ 2352`
- turn multiplier `M_t = C_turn ≈ 49`
- flop multiplier `M_f = 1`

Let `A_s` be the number of action-node **templates** on street `s` (the CPU tree's count, one copy), `L_s` the number of terminal-plus-showdown templates, `a_s` the average action count, `r_s` the acting player's combo count, `R = n0 + n1`.

```
T        = sum_s  A_s * M_s * a_s * r_s
N_leaves = sum_s  L_s * M_s

regret_pair_bytes = T * prec * 2
leaf_bytes        = N_leaves * R * 4
scratch_bytes     = 4 * max_level * R * 4
```

`max_level` is whatever BFS actually produces. A usable upper guess is `(A_river + L_river) * M_r / D` with `D` around 8–16 (betting depth on the river). The log line prints the true `max_level` indirectly via the reach and utility megabyte counts.

River terms dominate whenever `A_river` and `L_river` are even in the same ballpark as the flop and turn templates, because `M_r / M_t ≈ 48` and `M_r / M_f ≈ 2352`. Stack depth and extra bet sizes grow `A_river` and `L_river`. Range width grows `r_s` and `R` linearly. The card multipliers do not depend on stack depth.

A turn solve drops `M_r` to ~48 and has no turn multiplier. A river solve has all multipliers equal to 1. River solves are not the 16 GB problem.

`T` and `N_leaves` are `int`. `INT_MAX` is 2.15e9 trainable slots. At fp16 that is 4 GB for regrets **or** 4 GB for cumulative strategy, 8 GB for the pair. A flop tree crosses that counter at roughly:

```
A_river * 2352 * a * r  ≈  2.15e9
```

With `a = 3` and `r = 200` (a 200-combo acting range), `A_river ≈ 1500`. That is a large betting tree, and it is also the point where the regret pair plus the leaf buffer is filling a 16 GB card. The overflow and the VRAM wall show up together. Past that point the counter wraps, the allocation size is nonsense, and `hipMalloc` either fails or allocates the wrong buffer.

### Worked examples

These use the formula, `a = 3`, a balanced range (`r = R/2`), and `max_level ≈ (A_r + L_r) * M_r / 12`. They assume the river template is the bulky one, which is the usual case. Plug real `A_s` / `L_s` from a future instrumentation of `buildGpuStates` over the log's `Total Game States` and `Total Trainable Slots` before trusting a row for a specific sim.

| Spot | Templates (A_f / A_t / A_r, L_r) | R | T | fp16 regret pair | Leaves | Scratch | fp16 total, rough | Fits one 16 GB card? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Small 25bb flop, narrow | 40 / 60 / 80, L_r = 50 | 300 | ~8.6e7 | 0.34 GB | 0.14 GB | 0.12 GB | under 1 GB | Yes |
| Same tree, wide ranges | same | 2000 | ~5.7e8 | 2.3 GB | 1.0 GB | 0.8 GB | ~4 GB | Yes |
| Medium flop, narrow | 200 / 400 / 800, L_r = 800 | 400 | ~2.3e9 | overflows `int` | — | — | regret pair alone would be ~9 GB | No, counter breaks first |
| Medium flop, just under the counter | A_r ≈ 1500, L_r ≈ 1500, smaller streets negligible | 400 | ~2.1e9 | 8.4 GB | ~5 GB | ~2 GB | ~15 GB | On the edge, if the predictor and the desktop compositor leave the room |
| 100bb-class flop, wide | A_r ≈ 2000, L_r ≈ 2500 | 2000 | ~2.8e10 | would be ~100 GB+ | ~40 GB | — | far past two cards | No |

Reading the table the way the code behaves:

- The solves that work today are the first rows. A short tree and a few hundred combos is a sub-GB to a few-GB problem. That matches "25bb, narrow ranges, one 16 GB card".
- Doubling the range roughly doubles every big buffer.
- Growing the river betting tree from ~80 action templates to ~1500 (more streets of betting, more sizes, higher raise limit) is what walks into the card **and** into the `int` overflow.
- Two cards, if the bulk is split in half, move the "on the edge" row to comfortable and move a ~30 GB tree onto the pair. They do not move the 100 GB+ row.

Leaf memset is also a bandwidth cost, not just a capacity cost. An 8 GB leaf buffer zeroed every iteration on a 9060 XT (128-bit GDDR6, on the order of 300 GB/s) is tens of milliseconds before any CFR math. Worth knowing when judging the PCIe exchange below: the iteration already streams the big buffers through DRAM.

### Why a previous memory micro-optimization could not change this

Anything that does not shrink `T` or `N_leaves` is fighting over the showdown-comb table and the `GpuState` array. Those are the small terms. The HIP path already:

- stores regrets and the average in place and recomputes the current strategy from regrets (the CUDA path keeps a third array),
- stores reach and backed-up utilities for one BFS level, not for every node,
- bit-packs the lock mask,
- shares showdown combos across nodes with the same board.

Further packing of `GpuShowdownComb`, dropping `d_level_starts`, or overlapping the two utility buffers does not get a 100bb flop into 16 GB. The levers that do are: fewer unrolled cards (isomorphism or subtree streaming), fewer stored leaves, smaller `prec` on the river regret pair, or a second card holding half the cards.

### Host RAM is the other wall

For a tree whose fp16 regret pair is 8 GB, `T ≈ 2e9`. The host then holds:

- `h_regrets` + `h_cum_strategies` ≈ 16 GB of floats,
- the unused pinned `h_async_buffer` ≈ 8 GB,
- and, on the first `syncStrategiesToCpuTree`, a CPU trainable per action state. Five float vectors of length `actions * range` is on the order of 20 bytes per trainable slot, ~40 GB.

A machine with 64 GB of RAM can die in `train()` after VRAM allocation succeeded, or during the first exploitability print. Dual GPU does not fix that. The device arrays are the ones that should stay resident. The CPU copies should be created at export time, for the nodes the UI actually opens.

`printExpectedVramUsage` can also refuse a solve that would have fit. It charges `T` bytes for the lock buffer; the allocator charges `T/8`. On a multi-GB regret array that overestimate is most of a gigabyte, and the function calls `exit(-1)` inside the GUI process when the estimate exceeds 95% of free VRAM.

---

## 3. Dual-GPU options

### What crosses the bus

There is no activation vector in `HipPCfrSolver`. The neural-net value head in `implementation_plan_nn_pipeline.md` is a different pipeline and is not on this iteration. The GPU CFR loop moves two plain float vectors, one entry per combo, and only at a chance node (the turn or river card). Action nodes never send anything to the other card.

Picture one flop decision that ends in a check-check or a call, so the next node is "deal the turn". Both players have a reach vector: `n0` floats for IP, `n1` floats for OOP, stored back to back. That vector is "how much weight each combo still has, given the strategies played to get here". The forward kernel multiplies it by `1/(valid_cards - 2)` and writes a zero for any combo that contains the turn card. Each turn card therefore has its own reach vector of length `R = n0 + n1`.

That is the downward transfer. GPU 0 computes it, because the flop trunk lives there. The rows for GPU 1's turn cards are copied across. GPU 1 then runs the normal forward/backward kernels on those turn-plus-river subtrees. Every regret update under those cards stays on GPU 1. No strategy vector and no regret vector moves.

At the end of that subtree, each turn card has a utility vector, also length `R`: "for each combo, the counterfactual value of this whole turn and river". Those rows are copied back. GPU 0 adds them, in card order, into the chance node and finishes the flop backward pass, including the flop regret update. The chance scale stays `1/(all valid cards - 2)`, including cards that live on the other GPU. Summing only the local cards would halve the flop values.

So the "activation" people remember from a network is, here, just those two vectors:

| Direction | What | Length | When |
| --- | --- | --- | --- |
| Down | Reach. Weight of each combo on this card. | `R` floats per chance child | Once per iteration, at the street boundary |
| Up | Utility. Counterfactual value of each combo for that card's subtree. | `R` floats per chance child | Once per iteration, same boundary |

Bytes are `N_chance * cards_on_the_other_gpu * R * 4` each way. A few hundred chance nodes, 25 remote cards, and 2000 combos is about 40–400 MB. That is the whole exchange. `run.sh` sets `HSA_ENABLE_SDMA=0`, which turns off the SDMA copy engine and is already part of the working launch. Keep it. Host-staged copies still work with SDMA off. Do not flip that variable on in order to chase peer-to-peer.

### What the hardware allows

- Two devices, `hipSetDevice(0)` and `hipSetDevice(1)`, each with its own streams and its own HIP graph. A graph captured on device 0 cannot launch kernels on device 1.
- No xGMI. Bandwidth between the cards is PCIe. On a consumer board the x16 slot is often split x8/x8, so plan on roughly 8–12 GB/s device-to-host and the same back, not 25 GB/s.
- `hipDeviceCanAccessPeer` on Radeon (as opposed to Instinct) is frequently false, even with resizable BAR. Peer access is a memcpy optimization to probe at startup. It is not a memory-pooling mechanism. `hipMalloc` memory is not visible to the other device either way.
- Unified memory (`hipMallocManaged`) is a bad fit. The working set is the whole subtree, every iteration. On two discrete cards the driver would migrate those pages across PCIe continuously. That is slower than an explicit exchange and it fails in harder-to-see ways. A previous attempt that "put the big buffers in managed memory and let the runtime shard them" would look exactly like a failed memory optimization.

### Option A — shard by chance-card subtree (recommended)

Split the unrolled tree at the first chance street.

- Flop solve: one shard owns a set of turn cards and **every river under those turns**. 24 cards on one GPU, 25 on the other.
- Turn solve: split the river cards the same way.
- River solve: there is no chance node. Run on device 0. Two GPUs do not add capacity or speed here, and the spot already fits.

Every turn card has the same betting-tree shape. Blockers zero out combos; they do not delete nodes. Memory per turn card is identical, so a 24/25 split is balanced to about 4%.

What moves each iteration, per chance node (there is one chance node per betting line that reaches the next street, not one per solve):

```
bytes_one_way ≈ N_chance * (cards_on_remote_gpu) * R * 4
```

| N_chance | Remote cards | R | One direction |
| --- | --- | --- | --- |
| 50 | 25 | 400 | 2 MB |
| 200 | 25 | 2000 | 40 MB |
| 2000 | 25 | 2000 | 400 MB |

At 8 GB/s host-staged, 400 MB is about 50 ms down and 50 ms back, plus a stream sync. The leaf memset of a tree that large is already in that range or larger, and the showdown kernel is `O(n0 * n1)` per showdown node with 128 threads and no shared card-sum. Communication is real and it is not the limiter. It happens **once per iteration**, at the street boundary, not once per BFS level.

Capacity: each GPU stores half of `d_leaf_utilities`, half of the river (and turn) regret pair, half of the showdown combos, and the flop trunk. The trunk is `M_f / M_r ≈ 1/2352` of a comparable river, plus the turn nodes of the cards it does not own are absent. Realistic useful gain is about **1.8× to 1.9×** on the bytes that dominate a flop solve, not 2.0×, because the trunk is duplicated or kept on device 0 and because of allocator slack and the display compositor. The largest single piece that must sit on one GPU is **one turn card's subtree**, about 1/49 of the flop bulk. That piece is still well under 16 GB for any tree that two cards can hold together. A second split (rivers under one fat turn) is not required for two 16 GB cards.

Correctness constraint: the chance scale is `1/(global_valid_cards - 2)`, not `1/(cards_on_this_gpu - 2)`. And the chance node's backed-up value is the **sum of every child**, including children that live on the other GPU. Running two independent half-solves and averaging strategies is a different algorithm. The trunk regret update has to see the full sum, once.

Fit with the code: the kernels already take `(level_start, level_count)` and already treat a subtree as a contiguous BFS. They do not know about devices. The part that does not shard cleanly is the single captured graph and the chance kernel's use of one integer as both "how many children I will touch" and "what the deal probability is".

Complexity: medium. New coordinator, per-device allocation, one kernel distinction for chance nodes, an exchange at the street boundary. The single-GPU graph stays as it is.

### Option B — shard by hand / range index

Split `n0` and `n1` in half. Regret updates are per hand and would split. Showdown and fold card-removal are cross-hand reductions: every showdown needs the opponent's full reach vector.

That reach has to be assembled at every level that contains leaves. The volume per iteration is on the order of the leaf buffer itself (gigabytes), and it is synchronized many times per iteration because the BFS levels are dependent. PCIe latency would sit on the critical path of every level.

The current kernels use one block-column per node and a thread per hand, and showdown is an all-pairs loop over both ranges. Cutting the range dimension in half touches every kernel, the combo tables, the same-hand maps, and node locking. Capacity gain is about 2× on regrets and leaves. The communication cost eats the speedup and makes the result sensitive to memcpy placement.

This is the wrong cut for two PCIe cards. It becomes interesting only if a **single river tree** (no card multiplier) does not fit in 16 GB. `L_r * R * 4` exceeds 16 GB only for absurd template sizes (tens of thousands of river terminals and thousands of combos). Not the problem being solved here.

### Option C — peer-to-peer versus host staging

Same bytes either way. Peer-to-peer skips the bounce through host RAM and saves a copy. On these cards it may not exist. Probe once:

```
hipDeviceCanAccessPeer(&can, 0, 1)
hipDeviceCanAccessPeer(&can, 1, 0)
```

If both return true, enable it and use `hipMemcpyAsync(..., hipMemcpyDeviceToDevice)` on a stream of the destination device. If either returns false, allocate one pinned buffer with `hipHostMalloc` and do device→host→device. Use two events so GPU1 does not read the buffer before GPU0's copy completes.

Do not put the exchange inside a captured graph in the first version. Capture a graph per device for the kernel sequence that actually stays on that device. Launch the copies between the graphs. Graph capture across devices is brittle on ROCm and buys little: the launches being saved are dwarfed by showdown work.

### Option D — one GPU, stream subtrees from host RAM

The same partition as option A, but the "second device" is host memory. Each iteration, copy one turn-card's regrets and cumulative strategy to the GPU, run that subtree, copy them back.

PCIe cost is `2 * (that shard's regret pair + that shard's leaves)` per shard per iteration if leaves are also streamed, or just the regret pair if leaves are recomputed and discarded. For a 32 GB regret pair, streaming all of it is several seconds per iteration on PCIe x8. That is a legitimate way to solve a tree that does not fit, and it is painful. It is the fallback when two resident GPUs are not enough. It should be the same coordinator with a storage backend, not a third solver.

### Recommendation

Do not change the regret update, the strategy accumulation, fp16, or isomorphism as part of turning the second GPU on. Those are later, and they change the strategy the solver produces.

First, run two workers. Same binary, same `run.sh` environment (`LD_LIBRARY_PATH=/opt/rocm/lib`, `HSA_ENABLE_SDMA=0`), one job each:

```
HIP_VISIBLE_DEVICES=0 python3 scripts/run_solver_worker.py
HIP_VISIBLE_DEVICES=1 python3 scripts/run_solver_worker.py
```

`run_solver_worker.py` already locks a pending row with `FOR UPDATE SKIP LOCKED`, so the two processes will not take the same job. No vector is transferred. This does not make one pot larger than 16 GB.

When a single pot has to be larger than one card, build option A and nothing else. Same kernels, same DCFR coefficients, same lock behavior. The only new traffic is the reach vector down and the utility vector up from [What crosses the bus](#what-crosses-the-bus). Host-staged copies are the transport. Probe peer-to-peer (option C) later, and leave `HSA_ENABLE_SDMA=0` in place. Leave the hand split (option B) unused. Streaming from host RAM (option D) is a later backend of the same card split, not a second design.

Expected result on this box: flop and turn solves up to roughly 1.8× the bytes that fit on the fuller of the two cards (the display card will be a bit smaller). Iteration time on a tree that already fits on one card should drop to something like 55–65% (the exchange plus the duplicated trunk plus imperfect overlap). A tree that does not fit on one card has no one-GPU time to compare; the success criterion is that it allocates and that its root strategy matches a smaller configuration, as in [Verification](#6-how-to-verify-on-the-machine).

Two cards do not change the shape of the betting tree. Going from a 25bb tree (`A_river` in the tens or low hundreds) to a fat 100bb tree (`A_river` in the thousands) is a larger ratio than 1.8. Range width is the linear knob: a tree that fits narrow and misses by less than 2× when the ranges are widened is the tree this hardware can pick up.

### Implementation plan

Opt-in. `HipPCfrSolver` with one device keeps today's constructor, today's graph, and today's kernel launches. The default UI selection stays one GPU.

**Step 0 — do not change solver math in this change.** The second GPU calls the kernels that already ship. No edit to `compute_strategy_prob`, the DCFR scale, fp16, or the chance formula. The fp16 template bug, the 32-bit counters, and the unused pinned buffer are listed under bugs so they are not forgotten. They are not part of turning the second card on, because each one changes memory layout or numerical results. Count trainable slots per device in `size_t` inside the new coordinator so a sharded build does not wrap a single global `int`, and leave the one-GPU counters alone until you choose to touch them.

**Step 1 — tag states, still one allocation.** In `buildGpuStates`, record on the host side (not necessarily in `GpuState`) the shard key:

- deal 0 → trunk
- deal `1 .. card_num` → shard `deal - 1`
- deal above `card_num` → shard `turn_index` (`c_deal / card_num`)

When the device count is 1, ignore the key. Add a debug print of states and trainable slots per shard and check that the shard sizes differ by at most one card.

**Step 2 — a flag, default off.** An environment variable or a constructor argument `int num_gpus = 1`. `PokerSolver::buildSolverOnly` keeps passing 1 until a UI checkbox exists. Do not infer "2" from `hipGetDeviceCount()`. A machine with two cards should still solve on one card exactly as it does now.

**Step 3 — per-device storage.** New type, separate from `HipPCfrSolver`, for example a `HipShardSolver` used only when `num_gpus > 1`. Each device gets:

- its own `hipSetDevice`, stream, and later its own graph
- the trunk states plus the states for its cards, with **local** indices
- its own regrets, cumulative strategy, lock bits, leaf utilities, showdown tables, and level scratch
- trunk regrets resident on device 0 only; device 1 gets a copy at the start of the iteration (the trunk regret pair is a rounding error next to one turn card)

`setupShowdowns` already keys off `board_long`. Run it per device on that device's states so combo memory splits too.

**Step 4 — the one kernel distinction.** `forward_pass_kernel` and `backward_pass_kernel` use `unpack_action_count` both to walk children and, on chance nodes, as the deal denominator. Add an explicit denominator argument, or store it in an unused pad of `GpuState`. On the single-GPU path pass the same number that is packed today (`action_count - 2` on chance nodes). On a sharded chance node pass the **global** valid-card count minus 2, and iterate only the local children. Do not compact the child list in a way that changes the denominator.

**Step 5 — three phases per iteration, no cross-device graph.**

1. Device 0 (and device 1, redundantly, or device 1 after a trunk-regret copy) runs forward from the root through each first-chance node, using the global denominator. Child reach for remote cards is written into a compact `[local_card][R]` buffer, not into the other GPU's level buffer.
2. Exchange that reach. Device 1 runs the existing forward, showdown, terminal, and backward kernels on its subtree levels only. Device 0 does the same for its cards. Subtree backward stops at the subtree root and writes a utility vector of length `R` per chance child. It must not update trunk regrets.
3. Copy those utility vectors back. Device 0 runs chance-node backward and the rest of the trunk backward, summing **all** children in a fixed card order, and updates the one trunk regret / cumulative-strategy copy.

DCFR coefficients are computed once on the host and copied to both devices (8 bytes). Lock bits are uploaded at setup with the shard they belong to.

**Step 6 — strategy readback.** Each device copies its cumulative-strategy slice into the offset the single-GPU `h_cum_strategies` layout would have used. `syncStrategiesToCpuTree` can stay, but it should not be called every print interval on a tree this size. Call it at the end, and on demand. Mid-solve exploitability on a 30 GB tree is a CPU memory and time problem, not a GPU problem.

**Step 7 — river and the empty-shard case.** If the root street is the river, `num_gpus > 1` logs that the second device is idle and runs the existing path on device 0. If a requested device fails `hipSetDevice` or `hipMalloc`, fail the solve with an error. Do not silently fall back to one card after the user asked for two; that hides a bad verification run.

What this does not change: `PCfrSolver`, the CPU trainers, node locking's CPU API, solve save/load, the preflop solver, and the neural-net evaluator plan. Those never see a second device. Save/load keeps writing the CPU trainables produced by the final sync.

---

## 4. Lower-risk memory savings

Ranked by payoff against effort. "Low risk" here means the single-GPU numerical result either stays the same or moves in a way the CPU solver already accepts. Isomorphism on the GPU is the largest single-GPU win and it is **not** in this list; suit-permutation bugs are silent. It is a separate project after the easy fixes.

| Rank | Change | Effort | Payoff | Risk |
| --- | --- | --- | --- | --- |
| 1 | Stop building a CPU trainable for every deal at iter 0 and every print. Sync at the end, or for nodes the UI opens. Drop `h_regrets` / `h_cum_strategies` as full-solve residents; keep one scratch buffer for the copy. Free or delete `h_async_buffer`. | Small, localized to `train` and `syncStrategiesToCpuTree` | Host RAM. Often the difference between "VRAM allocated" and "process killed". Tens of GB on the trees that touch 16 GB VRAM. | Low for the GPU iteration. Export and the exploitability button must still sync first. |
| 2 | Fix the fp16 kernel template so only regrets and cumulative strategy are half, matching the allocation. | Small | Makes the existing checkbox a real ~2× cut on the regret pair instead of a wrong solver. On a flop solve the regret pair is roughly a third to a half of VRAM, so overall VRAM drops by something like 20–40%, not 2×. | The current fp16 checkbox is already wrong. After the fix, fp16 regrets still saturate: weights default to 1.0 per combo, so a flop counterfactual value is on the order of `pot/2 * (sum of opponent weights)`. A wide range and a 100bb pot exceed fp16's 65504, and positive regrets accumulate. Prefer fp16 **only on the river**, which is what `ActionNode::getTrainable` already does for `DiscountedCfrTrainableHF` (`use_halffloats` is applied only when `getRound() == RIVER`). River slots are ~98% of `T` on a flop solve. Flop and turn stay float. That is almost the full regret-pair saving, and the values on the river have already been scaled by ~1/47 and ~1/46. |
| 3 | `size_t` counters and an honest VRAM predictor (lock bits, no `exit`). | Small | Does not shrink a buffer. Removes a false refusal and a wraparound that corrupts a "successful" allocation right at the sizes in the table. | None. |
| 4 | Scaled fp16 **leaf utilities**, per leaf or per board: store `scale * fp16` with one float scale equal to the max absolute value on that leaf. | Medium | Almost 2× on the leaf buffer, which is often the largest single allocation. Together with river fp16 regrets this is the realistic single-GPU capacity win, on the order of 1.5× overall, not 2×. | Raw fp16 leaves are a bad idea. Reach at the river is ~1/2162 of the root weight before strategy products, and another 0.05 of strategy takes it under the fp16 normal minimum (~6e-5). Low-probability hands would become exact zeros and the regret update would follow. A per-leaf scale keeps the exponent. Do this only with a spot-check against fp32 leaf values on a small tree. |
| 5 | Port the CPU showdown's two-pointer / card-sum loop into `showdown_pass_kernel`. | Medium | Essentially no VRAM change. Large speed change. The current kernel is a full double loop per showdown, and there are `L_r * 2352` showdowns. | Low if the result is checked against `PCfrSolver::showdownUtility` on one board. The CPU version double-subtracts the exact same two-card holding; the GPU skip-once loop is the better card removal. Matching the CPU bit-for-bit is optional. Matching it up to that one combo is the goal. |
| 6 | Pack `GpuShowdownComb` (index, rank, two cards) into 8 bytes. | Small | Tens of MB. | Low. Not worth doing ahead of 1–4. |

Things that look like memory wins and are not, on this code:

- fp16 reach or raw fp16 utilities. Underflow, as above. `DiscountedCfrTrainableSF` only stores EVs in half and keeps regrets in float. The GPU does not store per-action EVs during training (EVs are computed by the CPU pass). SF saves nothing on the device.
- Dropping the second ping-pong buffer. Forward writes the next level while reading the current one; backward does the same for utilities. The pair is load-bearing. It is also the small scratch term.
- Caching less showdown metadata. Already cached per board.
- Turning isomorphism on in the constructor. It is already passed as `true` and it only affects the CPU walk.

---

## 5. Bugs, risks, and build issues

### fp16 mode reads float reach and utilities as half

`use_fp16` selects `forward_pass_kernel<uint16_t>` and the same for backward, showdown, and terminal. Those templates convert **every** `read_val` / `write_val`, including reach and utilities. The allocations and the root-weight upload are float:

```cpp
// src/solver/HipPCfrSolver.cpp, allocateDeviceMemory
size_t utilities_bytes = (size_t)max_level_size * total_node_data_size * sizeof(float);
size_t leaf_utilities_bytes = (size_t)total_leaf_nodes * total_node_data_size * sizeof(float);
size_t reach_probs_bytes = (size_t)max_level_size * total_node_data_size * sizeof(float);
```

```cpp
// src/solver/HipPCfrSolver.cpp, train()
hip_copy_to_device(d_reach_probs_in, root_reach_probs.data(), root_reach_probs.size() * sizeof(float));
```

A host float `1.0f` read back as the low 16 bits is not `1.0` in half. Level 0 reach is garbage, and every later level is half-precision written by that garbage. The UI checkbox turns this on (`use_halffloats = 2` → `use_fp16 true`). `scripts/run_solver_worker.py` retries with `set_use_fp16 1` when the process exits 255, which is the VRAM predictor giving up. Jobs that only finish on that retry are the ones this bug applies to. fp32 solves are unaffected. Leave the kernel as it is for the multi-GPU work. When you do want fp16 to mean "half regrets only", that is a one-kernel-template change and it should be checked on one GPU before it is copied to two. River-only half regrets, matching `DiscountedCfrTrainableHF`, are the variant that matches the CPU and avoids putting half-size reach into the chance exchange.

`read_val` also casts `uint16_t` storage through `__half`. The bit patterns match IEEE fp16, so that part is fine once the buffer really is half. Host conversion uses `half_float::half` from `include/tools/half-1-12-0.h`, which is the same layout the CPU HF trainer uses.

### Average-strategy update order

Described in [Regret update versus the CPU trainer](#regret-update-versus-the-cpu-trainer). The `theta = 0.9`, the alpha/beta scaling, and the missing reach weight are shared with `DiscountedCfrTrainable` on purpose. The mid-loop `compute_strategy_prob` is not. It will show up as a strategy difference against the CPU solver, including on one GPU. It will not, by itself, show up as a difference between one GPU and two GPUs if both run the same kernel.

### 32-bit sizes

`total_trainable_size`, `total_leaf_nodes`, and the `int` fields in `GpuState` (`child_start_idx`, `trainable_offset`, `parent_idx`). The trainable counter overflows at the byte counts in the medium-flop row of the table. `h_regrets.resize(total_trainable_size)` then either throws or, after wrap, allocates a tiny buffer and the device copy walks off it.

### Allocation and predictor failures

- `hip_allocate_device` prints and returns `nullptr`. Callers do not check. The next `hipMemcpy` fails opaquely.
- `printExpectedVramUsage` calls `exit(1)` if `hipMemGetInfo` fails and `exit(-1)` if the estimate exceeds 95% of free VRAM. That kills the Qt process. The estimate uses `locked_bytes = T * sizeof(uint8_t)` while `allocateDeviceMemory` uses `(T + 7) / 8`.
- `hipHostMalloc(&h_async_buffer, T * sizeof(float))` is unchecked, unused, and not freed (`freeDeviceMemory` does not know about it). On a large `T` this pinned allocation can fail the solve after VRAM is already reserved, or pin a large fraction of host RAM for the rest of the process.

### Build and toolchain

The install that actually runs this solver is `/opt/rocm`, and it already targets gfx1200.

- `run.sh` sets `LD_LIBRARY_PATH=/opt/rocm/lib` and `HSA_ENABLE_SDMA=0`, then executes `build/GPUPokerSolver`. That is the GUI launch that has been used on these cards.
- `scripts/run_solver_worker.py` and `scripts/generate_training_data.py` prepend `/opt/rocm/core-7.13/lib`. That `core-7.13` directory is the ROCm 7 component layout, not ROCm 5.7.
- The README installs PyTorch from the `rocm7.2` wheel index.
- `GPUPokerSolver.pro` compiles `HipKernels.hip` with `/opt/rocm/bin/hipcc -c --offload-arch=native`. On this box `native` is gfx1200, which is why the binary runs there. Leave that flag. Pinning `gfx1200` is optional and is not required to keep the current build working.

A `hipcc --version` that prints HIP 5.7 and clang 17 is a different binary on `PATH`. Check `which hipcc` against `/opt/rocm/bin/hipcc --version` before treating it as the compiler that built `build/GPUPokerSolver`. The earlier "upgrade off HIP 5.7 or the card will not run" conclusion does not apply to this tree as it is launched today.

The qmake rule compiles the `.hip` file with an explicit `-isystem /opt/rocm/include` and links `-L/opt/rocm/lib -lamdhip64`. `HipPCfrSolver.cpp` is a normal `SOURCES` entry, compiled by g++, and the `.pro` file does not add the ROCm include path to g++. The `.cpp` includes `<hip/hip_runtime.h>`. That has clearly succeeded on the machine that produced the current binary (system include path or `CPATH`). A clean build on a machine where g++ cannot see `/opt/rocm/include` would need that `-I` added. Not a reason to change the working `run.sh` launch.

`GpuTypes.h` marks `unpack_*` as `__device__` only, and `HipPCfrSolver.cpp` calls them on the host. g++ is ignoring the attribute, which is why the current binary links. Mark them `__host__ __device__` only when that header is next edited for another reason.

Graph capture return codes are ignored. A failed capture looks like a bad launch on iteration 0. Worth checking if a two-device graph is added. The one-GPU path already captures a graph successfully on this ROCm, so leave it.

### Other behavioral risks

- **Isomorphism is a CPU-only saving.** Planning VRAM as if suit isomorphism were already applied will overestimate what fits by roughly 1.5–2.5× on the turn/river bulk, depending on flop texture. The GPU pays for every suit.
- **Exploitability during the solve** calls into CPU CFR and best response and creates trainables. On a large tree this dominates the printed "s/iter" and can OOM the host. It is not part of the GPU iteration. The `hipDeviceSynchronize` every 5 iterations plus a 2 ms sleep also stalls the queue; it is harmless for correctness and noisy for timing.
- **Showdown performance and a small card-removal difference.** GPU showdown is `O(n^2)` with an explicit skip on shared cards. CPU showdown is the linear two-pointer plus card sums, which subtracts an identical two-card holding twice. Results differ on that one combo class. Not a dual-GPU issue.
- **Terminal kernel.** Card sums are local and recomputed by all 128 threads. Correct, wasteful, high register pressure. No race.
- **`action_count` is 8 bits, card id is 7 bits with 127 as "none".** Safe for a 52-card deck (card integers 0–51) and for normal bet counts.
- **Chance denominator assumes the chance node's card list is the full deck**, so `valid_children = deck - board`. That is how `GameTree` builds chance nodes (`this->deck.getCards()`). A short deck still works if the same deck is on the node. A chance node that was pre-filtered to the live cards only would subtract 2 a second time. The current builder does not do that.
- **Device 0 may be the display card.** The compositor can hold a few hundred MB to about 1.5 GB. The 95% check uses free memory at allocation time, which is right, but a dual-GPU shard that puts half the tree on the display card can still fail while the idle card had room. Log both PCI bus ids and free bytes, and put the larger shard on the card with more free memory.
- **Lock bit test is `offset + hand`, i.e. action 0's slot.** The writer sets every action's bit for a locked hand, so the test works. A future writer that sets a different bit will silently train a "locked" hand.
- **`runCfrIteration` is declared on `HipPCfrSolver` and has no definition.** Nothing calls it. Dead declaration, not a link error today.
- **Player-0 comments in the kernels say OOP.** In this tree player 0 is IP. Do not swap ranges while porting to two devices.
- **Save/load and node locking stay on the CPU trainables.** A dual-GPU solve that skips the final `syncStrategiesToCpuTree` will save an empty or stale strategy. The final sync is part of the feature, not an optional debug dump.
- **Preflop solver and the neural-net evaluator** do not share these buffers. Nothing in this note applies to them.

---

## 6. How to verify on the machine

The current `run.sh` binary is the baseline. This environment has no GPU, so none of the following was run here.

### Two workers, before any solver change

Launch the training worker twice, with `run.sh`'s environment (`/opt/rocm/lib` on `LD_LIBRARY_PATH`, `HSA_ENABLE_SDMA=0`) and a different `HIP_VISIBLE_DEVICES` on each. `rocm-smi` should show both cards busy on different jobs, and the `training_samples` rows should move to `solved` independently. If one process still sees both GPUs, `HIP_VISIBLE_DEVICES` was not set in that process. This test does not involve a reach or utility copy.

### Toolchain, only if a later one-solve split is built

1. `rocminfo` shows two agents, both `gfx1200`, about 16 GB each. `/opt/rocm/bin/hipcc --version` is the compiler the `.pro` file calls.
2. A one-line HIP program prints `hipGetDeviceCount() == 2`, both PCI bus ids, free and total memory, and both `hipDeviceCanAccessPeer` results. Peer access false means the host-staged path is the one under test. That is a pass. Keep `HSA_ENABLE_SDMA=0`.

### Correctness, one GPU against two

Use the same precision on both sides. fp32 (`set_use_fp16` left off, which is what the worker tries first) is the clean comparison. Same tree, same ranges, same iteration count, `print_interval` larger than the iteration count so the CPU exploitability pass does not run in the middle. Compare at the end.

Pick three spots, in this order:

1. **River spot.** Two-GPU mode should take the "root is a river, ignore device 1" branch. Root average strategy must match the one-GPU run to the bit (same kernels, no exchange). If it does not, the flag is changing the single-GPU path. Stop.
2. **Turn spot, small ranges, ~20 iterations.** This splits river cards. Trunk backward adds child utilities in a fixed card order. Subtree math does not depend on other cards. In fp32, with no atomics in these kernels, the cumulative strategy should match the one-GPU result to the bit, or at worst to a couple of ulps if a sum is reassociated. A relative error above about 1e-5 on the root strategy means the chance denominator, the child set, or the utility exchange is wrong. Check a locked hand on the turn and a hand on one river: the lock should be identical, and the river strategy for a card owned by device 1 should match the one-GPU strategy for that same deal id.
3. **Flop spot, still small enough to fit on one card.** Same comparison. This is the real shard (turn cards, rivers underneath). Again expect an exact or near-exact fp32 match. Also compare one non-root flop action and one turn action under a card that lives on device 1.

Practical comparison without new tooling: after both runs, dump strategy at the root via the existing solver dump and diff it. For a tighter check, log `max abs diff` of `h_cum_strategies` between the two runs (same `T`, same layout). Do not compare exploitability alone. The CPU best-response number can hide a wrong subtree if the root happens to look similar, and it is expensive.

Then run the same flop spot with a node locked from the UI before solving. The locked hand's strategy should be the locked value on both runs, and the other hands should still match.

fp16, after the template fix: compare against the fp32 run with a loose tolerance (relative 1e-2 on the average strategy is a reasonable first gate, plus exploitability within a small fraction of a percent of the pot). Do not expect bit-exact agreement between fp16 and fp32. Do expect bit-exact agreement between one GPU and two GPUs in fp16, for the same reason as fp32, if both devices use the same half rounding.

### Capacity

1. On one GPU, grow one dimension until allocation fails: first range width on a fixed small flop tree, then stack / bet sizes on a fixed narrow range. Record the predictor's line (`Total Game States`, `Total Trainable Slots`, per-buffer MB, `ESTIMATED TOTAL DEMAND`) and `hipMemGetInfo` free bytes. That is the one-GPU envelope on this box.
2. Enable two GPUs on the spot that just failed, and on a spot about 1.6× that leaf-plus-regret total. Each device's predictor line should show about half the leaf buffer and about half the regret pair of the one-GPU estimate. The trunk remains. A run that finishes the requested iterations without a later `hipMalloc` failure is a pass.
3. Watch host RAM (`free -h`) across the **start** of `train()`, not only VRAM. If the process is killed there, the CPU trainable materialization is the limit. Two GPUs will not help until rank-1 in the savings table is done.
4. Confirm the display card. If device 0's free memory is meaningfully lower, the larger shard belongs on the other device. Re-run the failing spot with that assignment before calling the tree too big.

### Timing, so the exchange is visible

On the flop spot that fits on one card, time 50 iterations with exploitability prints disabled. Report seconds/iteration for one GPU and for two, and the peer-access flag. A two-GPU iteration that is slower than one GPU by a large factor means the exchange is inside the inner level loop or a device is stalled on `hipDeviceSynchronize` of the wrong stream. It should not mean "PCIe cannot handle 400 MB".

### What not to use as evidence

- A single screenshot of the VRAM log. The behavior that matters is: same strategy on a shared spot, successful allocation on a spot that previously failed, host RAM still alive at the first sync.
- Exploitability versus the CPU solver as the dual-GPU test. Compare one GPU against two GPUs running the same kernels. A difference there is a bad split. A difference against the CPU trainer is the existing update-order gap and is not introduced by the second card.

---

## Summary

`HipPCfrSolver` is a level-synchronous CFR over an explicit copy of the tree for every turn and river card. Regrets and the cumulative average live in two flat arrays of length `T`. Reach and backed-up utilities are two ping-pong buffers the size of the widest BFS level. Leaf counterfactual values are a third array the size of every terminal and showdown times both ranges, in float, and that array plus the regret pair is what fills the card. The structure is already much tighter than `CudaPCfrSolver`. Shrinking the small buffers further does not change which spots solve.

On a 52-card flop the river multiplier is about 2352. A short 25bb tree with a few hundred combos is a few GB. The same formulas put a medium betting tree, or a wide range, on or past 16 GB, and the `int` trainable counter overflows at about 8 GB of fp16 regret-pair data. Host copies of those same strategies can be larger than VRAM.

Use both cards first as two processes (`HIP_VISIBLE_DEVICES` 0 and 1 on `scripts/run_solver_worker.py`, same `/opt/rocm` launch as `run.sh`). No solver change, no vector copy, twice the jobs, each job still one card.

A single pot larger than 16 GB is the turn-card (or river-card) split of the same kernels. There is no activation vector. Each iteration copies the reach vector down at the chance node and the utility vector back up. Regrets stay on the card that owns that street card. That is about 1.8× the bytes of one card, and the one-GPU path stays the default when the flag is off. `HSA_ENABLE_SDMA=0` stays set. Strategy math stays as it is until a later pass.
