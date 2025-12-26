# Terminal Tactics AI Rebuild Plan

## 1. Goals
- Replace the current AI with a budget-driven system that makes one decision per AI tick.
- Keep decisions simple, ordered by explicit priority, and easy to reason about.
- Ensure AI can win reliably by expanding economy, teching, defending, and attacking with coordinated groups.

## 2. Constraints
- No external libraries or standard C runtime usage in kernel-side code.
- One action per AI tick; if an action is not possible, skip to the next condition.
- Never queue a building that consumes energy if available energy is zero.
- Always respect tech prerequisites and construction limits.

## 3. Data Model (per AI team)
- Attitude: Aggressive or Defensive.
- Mindset: Idle, Pressure, Panic.
- Budget pools: Economy, Infrastructure, Defense, Offense.
- Known enemy strength (observed only, with decay).
- Production state: available buildings, queue sizes, energy margin.
- Regiment state: list of regiments with role, target, composition, and strength.
- Cached map metrics: map size, map max dimension, target counts for scouts, drillers, and regiment sizes.

## 4. Perception and Enemy Strength
- Track enemy units and buildings inside the team field of view only.
- Compute a weighted strength value from observed enemy types.
- Decay old observations over time so the AI does not rely on stale data.
- Use known strength to shift budget weights for defense and offense.

## 5. Budget Model
- Budgets are updated every AI tick from plasma income and a controlled share of stored plasma.
- Define baseline weight sets per attitude and modify them by mindset:
  - Aggressive: offense high, defense low, infrastructure medium, economy medium.
  - Defensive: defense high, infrastructure high, offense low, economy medium.
  - Pressure: boost defense and immediate unit production, reduce offense.
  - Panic: boost economy and power, reduce offense and non-critical infrastructure.
- Budgets only allow actions when their pool can afford the action cost.

## 6. Priority Rules (global)
- Power Plant decisions are evaluated before any other building decision.
- Economy survival decisions (drillers and escort) are evaluated before tech expansion.
- Infrastructure decisions are ordered by tech prerequisites.
- Defense decisions use known enemy strength and proximity.
- Offense decisions use regiment readiness and surplus budgets.

## 7. Condition and Action Table
- Implement AI logic as an ordered list of conditions with a single action per match:
  - If condition is true and action is possible, execute and return.
  - If condition is true but action is not possible, skip to the next condition.
- Conditions read only from a prepared AI context snapshot to avoid recomputation.

## 8. Regiment System
- Regiment types: base guard, driller escort, attack group.
- Scouts are managed independently and always run exploration orders; they are not part of regiments.
- Each regiment has a target strength and composition, computed from tech level and attitude.
- Attack regiments assemble at rally points, then move as a group to enemy targets.
- Base guard regiments patrol within a defined radius around key buildings.
- Escort regiments follow drillers and scale with known enemy strength.

## 9. Unit Production Logic
- Production buildings choose units based on budget pool, regiment needs, and available tech.
- Defensive attitude produces fewer attack units and more escorts and guard units.
- Aggressive attitude favors attack units but maintains minimal defense and escort levels.

## 10. Building Production Logic
- Infrastructure pool prioritizes: power plants, barracks, factory, tech center, then upgrades.
- Defense pool prioritizes: turrets and walls with spacing rules and access gates.
- Economy pool prioritizes: drillers, and placement safety for plasma access.

## 11. Map Placement Rules
- Buildings are placed using a prioritized search to keep clear movement paths.
- Respect terrain constraints and avoid water and mountains for placement and spawn.

## 12. AI Tick Pipeline
- Stagger AI updates: process half the AI teams each tick, alternating halves each tick.
- Effective refresh rate per team becomes ~1000 ms when the base tick is ~500 ms.
- Build an AI context snapshot: resources, energy, map metrics, unit counts, known enemy strength.
- Update mindset based on threat and affordability checks.
- Refresh budget pools using attitude, mindset, and economy status.
- Evaluate the condition table in order and execute a single action.

## 13. Debugging and Validation
- Add a concise AI decision trace (condition name and action) for debugging only.
- Validate that each action respects power, affordability, and placement constraints.
- Test AI behavior on small maps and large maps with all attitudes.

## 14. Robustness Rules
- Add hysteresis for budget and mindset changes to avoid rapid oscillation.
- Add cooldowns on repeated actions (same building or unit) to prevent spam.
- Reserve plasma for mandatory near-term requirements (power and critical buildings).
- Enforce production throttles if queues are full or placement repeatedly fails.
- Track action failures and temporarily lower that action priority after repeated failures.
- Ensure AI does not issue new orders to units already executing valid orders (idle only).

## 15. Migration Plan
- Replace the existing AI loop with the new condition-action system.
- Introduce the new AI context and budget structs.
- Incrementally wire conditions and actions, starting with power and economy.
- Add regiment handling and offense logic last.
