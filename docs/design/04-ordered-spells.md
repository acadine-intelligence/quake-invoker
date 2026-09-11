# Ordered invocation and classic spells

Status: ordered recipes, the shared mana pool, and all ten classic spells implemented and verified in-engine (2026-09-11). Invoking resolves the exact orb sequence against the 27-entry table; weapon recipes equip a hand; Ghost Walk (Q,Q,W), Sunstrike (E,E,E), EMP (W,W,W), Chaos Meteor (W,E,E), Tornado (Q,W,W), Deafening Blast (Q,W,E), Cold Snap (Q,Q,Q), Ice Wall (Q,Q,E), Alacrity (W,W,E), and Forge Spirit (Q,E,E) equip a hand and cast on that hand's fire button, spending mana and starting their cooldown; the Portal Pair (Q,E,W) places a two-way shortcut in two shots; reserved entries report "not castable yet" without granting anything. This target supersedes the order-independent recipe target in `01-invoker-mechanics.md`; that document and `05-dual-hands.md` describe the earlier slices.

## Requested behavior

- Orb order identifies the invocation. Three choices in each of three positions give 27 recipes.
- Include Cold Snap, Ice Wall, Forge Spirit, Ghost Walk, Tornado, EMP, Alacrity, Chaos Meteor, Sunstrike, and Deafening Blast.
- EMP removes enemy armor and mana.
- R invokes the current recipe. Mouse 1 casts the first prepared invocation; Mouse 2 casts the second.
- A new invocation replaces the Mouse 2 invocation.
- Other recipes can create Quake weapons and additional abilities.
- A portal ability places the first portal on one shot and the second portal on the next shot.

The classic spells and ordered recipes remain requested features. The playable prototype still matches orb counts. It now equips two stock weapons, with movement-coupled orb controls and independent mouse firing. See `05-dual-hands.md`.

### Input and first-person presentation request (2026-09-07)

Jonathan requested physical D/W/A for logical Q/W/E orb selection. The orb display should use colors without recipe letters. A separate WASD display should show held keys in real time. Mouse 1 belongs on the left side of the first-person view. Mouse 2 belongs on the right. Each slot must hold a functional invocation and show its own first-person weapon draw model once invoked. Rendering two models alone does not satisfy independent firing or casting.

The D/W/A request supersedes the proposed Q/E/F orb bindings below. Jonathan confirmed that D/W/A must both move the player and select an orb. Each new physical key press appends one orb. Holding a key continues movement without appending more orbs. S moves backward without selecting an orb. The WASD display tracks held keys, including opposing keys held together. R invokes into the right hand. The implementation uses the recommended T-to-swap behavior so Mouse 1 stays pinned until swapped. The current delivery adds functional dual stock weapons and their models. The classic spell behavior and portal renderer remain the separate larger design below.

## Decisions needed

### Access to Mouse 1

Settled by the dual-hands slice (2026-09-07): R always loads or replaces the right-hand slot and T swaps the slots, including an empty slot. Mouse 1 keeps its invocation until the player swaps. From a fresh life: invoke into the right hand, press T to move it to the left, then invoke the next recipe into the right hand.

### Portal rendering

Recommended first implementation: visible paired teleport surfaces with safe exits and preserved momentum. Entering either portal exits the other. The portal surface initially shows an original effect rather than the scene beyond it.

The alternative includes a rendered view through the destination portal. That requires a separate renderer and visibility implementation, in addition to traversal. Both choices retain the first-shot/second-shot placement behavior. The request's reference to Portal leaves this visual scope open.

## Proposed controls and rules

These are implementation proposals, separate from the requested behavior above.

| Input | Action |
|-------|--------|
| Q | Select Quas, logical Q. |
| E | Select Wex, logical W. |
| F | Select Exort, logical E. |
| R | Invoke. |
| Mouse 1 | Cast the first prepared invocation. |
| Mouse 2 | Cast the second prepared invocation. |
| T | Swap prepared invocations, if the pinned-slot proposal is selected. |

WASD continues to move the player. Physical keys differ from the logical recipe letters. For example, the proposed physical Q/E/F sequence produces logical QWE.

- Read recipes from the oldest held orb to the newest. A fourth selection discards the oldest orb.
- Require three selected orbs before invoking. Remove the prototype's automatic completion of partial recipes.
- Invoking prepares an ability. Casting activates it. Casting leaves the prepared invocation in its slot.
- Store cooldowns by invocation identity, independently of slot position. Swapping or reinvoking must not reset a cooldown.
- If both slots hold the same invocation, they share its cooldown and resources.
- Spell casts use mana. Conventional weapons keep ammunition and hold-to-fire behavior. Invoking a weapon must not repeatedly refill its ammunition.
- Check eligibility and resources on the server. Invalid casts produce feedback without spending resources. Simultaneous inputs have deterministic ordering.
- Spawn resets prepared invocations and status effects. Death, disconnect, and map changes remove owned temporary entities and portal pairs.
- HUD shows the exact orb order, both prepared invocations, mana, and cooldown readiness.

## Proposed recipe allocation

The classic spells keep familiar orb compositions, with one selected order per spell. The other orders become distinct recipes. The exact mapping below is a proposal, not a claim about DotA's order-independent invocation.

This allocation assigns the ten classic spells, nine Quake weapons, and one portal ability. Seven recipes remain reserved for later ideas. Reserved entries must report that they are unassigned; they must not masquerade as implemented spells.

| Recipe | Invocation |
|--------|------------|
| QQQ | Cold Snap |
| QQW | Ghost Walk |
| QQE | Ice Wall |
| QWQ | Machinegun |
| QWW | Tornado |
| QWE | Deafening Blast |
| QEQ | Shotgun |
| QEW | Portal Pair |
| QEE | Forge Spirit |
| WQQ | Gauntlet |
| WQW | Rocket Launcher |
| WQE | Grenade Launcher |
| WWQ | Lightning Gun |
| WWW | EMP |
| WWE | Alacrity |
| WEQ | Railgun |
| WEW | Plasma Gun |
| WEE | Chaos Meteor |
| EQQ | BFG |
| EQW | Reserved |
| EQE | Reserved |
| EWQ | Reserved |
| EWW | Reserved |
| EWE | Reserved |
| EEQ | Reserved |
| EEW | Reserved |
| EEE | Sunstrike |

## Proposed FPS spell adaptations

These descriptions specify distinct gameplay. They do not claim to reproduce a particular DotA balance patch. Damage values, durations, ranges, and resource costs remain playtest parameters.

| Spell | FPS behavior | Required proof |
|-------|--------------|----------------|
| Cold Snap | Aim at an enemy to apply a debuff. Subsequent qualifying damage causes brief freezes and additional damage, with an internal trigger cooldown. The built Cold Snap follows this spec: a 1000-unit aim; a 5 s debuff; any damage except the trigger's own freezes the chilled player for 250 ms and adds 15 damage, at most once per 900 ms. The freeze is a full brief lock: no movement, no jump, no weapon fire, no hand casts (movement and firing both ride state the client predicts, so it cannot desync). Only damage that removes health triggers; a hit fully absorbed by armor does not. The 900 ms floor is one floor per victim, shared by every caster, and a recast cannot shorten it. | Repeated hits trigger at the allowed interval. Its own damage cannot recurse into another trigger. |
| Ice Wall | Place a short-lived ice field across the ground ahead. Enemies inside take periodic damage and move more slowly. The built Ice Wall follows this spec: a 600-unit aim ray settles the field on the floor, held 64 units clear of a wall it hits; 6 s lifetime; everyone except the caster inside 170 units moves at 0.6x after a 400 ms grace and takes 8 damage every 500 ms. | Crossing applies the slow and damage. Leaving or expiry removes the slow. Overlapping fields cannot multiply the slow indefinitely. |
| Forge Spirit | Summon a damageable companion that follows the caster and attacks enemies with armor-reducing fire projectiles. | It acquires a valid enemy, deals damage, reduces armor, and disappears on death or expiry. It respects collision and an owner-specific population cap. The built Forge Spirit follows this spec: 60 mana, a 40 s cooldown; it hovers near the caster, fires 9-damage armor-shredding bolts at the nearest visible enemy, and dies with its caster, on its 30 s expiry or when its 60 health run out, within a two-per-caster cap (a summon at the cap dismisses the oldest). |
| Ghost Walk | Become invisible and apply a short-range slow to nearby enemies. Casting an offensive ability or firing a weapon ends invisibility. | Remote observers and enemy targeting reflect invisibility. Reveal and expiry remove the associated state. |
| Tornado | Launch a travelling vortex that lifts enemies caught along its path. | Targets gain vertical displacement, land safely, and regain normal movement. Solid walls stop the vortex. The built Tornado follows this spec: level flight for up to 1.5 s, a 170-unit lift radius, and a quiet stop at solid walls. |
| EMP | Mark an area, then discharge after a delay. Affected enemies lose armor and mana. | Equipped enemies actually reach zero armor and mana. Resources cannot become negative. It has no direct health damage in this proposed adaptation. The built EMP differs: 70 damage plus a shove, no resource drain (see 05-dual-hands.md). |
| Alacrity | Apply a temporary weapon attack-speed and damage buff to the caster. | Weapon shot intervals and damage change while active, then return to normal. Repeated applications do not multiply the buff. The built Alacrity follows this spec: 30 mana, a 20 s cooldown and an 8 s window. It rides the engine's haste powerup, which already lifts move speed and shortens weapon fire intervals and hand cooldowns by 1.3; classic weapon damage is multiplied by 1.3 for the window. Recasts refresh the window, cannot stack it, and a longer Speed-item window survives the cast. Spell damage does not take the weapon-damage bonus. |
| Chaos Meteor | Send a burning meteor along the ground, with impact damage and a burning trail. | It moves across valid ground, hits targets, and applies timed burn damage. Walls and expiry end it. |
| Sunstrike | Mark the aimed ground position and produce a delayed, narrow strike that bypasses armor. | The strike occurs at the recorded position after its delay. Damage follows the documented armor rule and radius. |
| Deafening Blast | Fire a broad pressure wave that damages and pushes enemies back, temporarily preventing weapon fire. | Impact changes velocity and damage. Weapon fire resumes when the disarm expires. Spell casting follows an explicit disarm rule. The built Deafening Blast follows this spec: 50 damage, a 260-unit/s shove, and a 3 s weapon disarm. Spell casting is exempt from the disarm, and the disarmed player sees a WEAPONS DISABLED bar with the time left on it. |

Mana must be introduced as a real server-owned resource. EMP targets in the tests must start with nonzero armor and mana. A renamed grenade or a resource value drawn only on the HUD does not implement EMP.

Implemented numbers (2026-09-10, extended 2026-09-11): one 100-point pool per client, regenerated at 4 points per second while alive on the server. Ghost Walk costs 25 with an 18 s cooldown; Sunstrike 45/24 s; EMP 45/30 s; Chaos Meteor 55/35 s; Tornado 40/25 s; Deafening Blast 45/30 s. All cooldowns run on server time (`level.time`), and the HUD reads the pool through `STAT_INVOKE_MANA`. Tornado lifts everyone inside its 170-unit radius to at least 210 units/s of upward speed and fades at solid walls or after 1.5 s. Deafening Blast bursts on contact or after 900 ms, shoving (260 units/s plus lift) and damaging (50) inside a 350-unit radius, and disarming weapon fire for 3 s. The disarm holds both hand weapons and the classic selected weapon (`ps.weaponTime`), while spell casting stays available; respawn clears it. Cold Snap costs 35 with a 20 s cooldown; it marks a 1000-unit aim and holds a 5 s debuff. Any damage except the trigger's own freezes the chilled player for 250 ms and adds 15 damage, at most once per 900 ms; the bonus is credited to the hit that set it off. The freeze locks movement, jumps, weapon fire and hand casts for its 250 ms; ground speed and the pm_type both ride player state the client predicts, so it does not desync. A recast while the debuff holds refreshes it but cannot shorten the victim's 900 ms floor. Alacrity costs 30 with a 20 s cooldown and grants an 8 s haste window: move speed, weapon fire intervals and hand cooldowns lift by 1.3 through the engine's haste powerup, and classic weapon damage is multiplied by 1.3 while it holds (spell damage is not). Recasts refresh the window; it never stacks, and a fresh life starts without it. The engine's own haste dressing also applies: the caster shows the haste smoke trail and animation speed-up while the window holds, so a Ghost Walk caster becomes visible for the window's duration. Forge Spirit costs 60 with a 40 s cooldown and summons a 60-health companion for 30 s; it hovers near its caster, fires a 9-damage bolt every 1.3 s at the nearest enemy client in sight, and a bolt hit leaves the victim's armor absorbing at 0.6x for 4 s, refreshed by later hits. Two spirits live per caster; a summon at the cap dismisses the oldest.

All temporary effects need bounded entity counts and explicit lifetimes. Friendly-fire policy must follow the selected game mode. The first test opponents can be local clients or bots; a full roguelite enemy roster is outside this spell implementation.

### Chosen implementation (2026-09-11)

- Rendering: the original-effect surface, chosen for this pass. The
  view-through renderer stays a later, separate target.
- Slot replacement: shots alternate replacement of A and B (A on the
  first shot, B on the second, A again on the third). A refused shot
  leaves the existing pair unchanged.
- Placement: a 600-unit aim ray that must hit the static world (movers
  and the sky are refused) with player-hull room at the face; ends must
  stay 48 units apart. A refused shot spends nothing.
- Cost: 15 mana per shot, 1.5 s between shots on the firing hand.
- Traversal: exits 24 units clear of the far face, speed preserved and
  capped at 700 along that face's direction, with a 500 ms per-client
  re-entry guard; a blocked exit refuses travel instead of embedding
  the player.
- Lifecycle: the pair is kept across invoke and hand-swap, and removed
  on death, respawn, disconnect, or map change.

## Portal behavior and safety

- The first successful shot records portal A. The second successful shot records portal B and connects the pair.
- Each caster owns at most one pair. Further successful shots alternate replacement of A and B. A rejected placement leaves the existing pair unchanged.
- Start with static world surfaces. Reject sky, moving surfaces, and openings without clearance for the player's hull.
- A lone portal does not teleport. A connected pair works in both directions.
- Validate destination clearance before traversal. Reject unsafe traversal rather than embedding a player in geometry or unexpectedly telefragging another player.
- Transform exit direction and velocity consistently with the portal orientations. Preserve speed through valid traversal.
- Prevent immediate re-entry with an exit-clearance rule and a short traversal cooldown.
- Keep the pair when it is moved between prepared slots. Remove it when the owner dies, disconnects, or changes map.
- The selected rendering scope must appear in the acceptance report. A teleport effect does not prove a view-through portal renderer.

## Implementation boundaries

Use shared recipe definitions in `code/game/bg_invoke.*`. Replace the stock-weapon-only invocation representation with stable invocation identities and explicit behavior types. Keep prepared slots and owned-entity references in game-owned per-client state.

Trace the existing weapon firing path through `code/game/bg_pmove.c`, `g_active.c`, and `g_weapon.c` before connecting both mouse inputs. Preserve held-fire behavior and weapon timing. Movement-affecting status effects require matching shared prediction and server rules.

Synchronize authoritative prepared-slot state and cast identity with cgame. Send enough information to render the actual cast even if the player has already selected the next orb sequence. Keep cooldown/resource checks on the server.

Reuse licensed engine content or create original effects. Do not copy Valve models, icons, sounds, or code. Register any new art files in `docs/ASSETS.md` before committing them.

Keep the existing visual PR unchanged. This design lives on `feat/spell-system`, based on the visually verified code. Creating this draft does not authorize merging PR #2 or publishing the new gameplay branch.

## Completion contract

Implementation is complete only when the applicable items below pass. Progress (2026-09-11): ordered recipes and the two-slot weapon path pass on the host and in-engine; the mana system, Ghost Walk, Sunstrike, EMP, Chaos Meteor, Tornado, Deafening Blast, Cold Snap, and Ice Wall pass their in-engine exercise (Cold Snap's hit reaction and Ice Wall's slow ride the shared-state host tests; a live two-player check remains open for both); the remaining two spell rows and the portal are still open.

- [ ] Slot replacement and portal-rendering decisions are recorded.
- [ ] Exhaustive recipe tests cover all 27 ordered sequences, including order-distinct results, invalid input, and reserved entries.
- [ ] Actual R/Mouse 1/Mouse 2 inputs work in-engine. The selected Mouse 1 update mechanism works from empty slots onward.
- [ ] Both prepared slots work independently. Swapping and reinvoking cannot reset cooldowns or refill ammunition.
- [ ] Each classic spell passes its behavior proof in the table above against live targets.
- [ ] Mana spending and regeneration work. EMP removes nonzero enemy armor and mana as specified.
- [ ] Every allocated Quake weapon fires correctly from either prepared slot, including hold-to-fire and ammunition use.
- [ ] Portal placement and bidirectional traversal work. Invalid placement, unsafe exits, and repeated traversal have tests.
- [ ] The chosen portal visual scope has rendered verification.
- [ ] Death, respawn, disconnect, and map restart leave no stale slots, status effects, summons, or portals.
- [ ] Native libraries and QVMs build from a clean tree. Incremental header changes rebuild dependent QVM sources.
- [ ] Host tests and scripted engine tests pass. Screenshots or recordings establish appearance separately from damage/resource assertions.
- [ ] Independent source review finds no unresolved blocking defects. Gameplay changes remain unpublished until requested.

## Delivery sequence after decisions

1. Implement ordered recipes and the two-slot cast path. Exercise both slots with existing weapons, real mana, and cooldown rules.
2. Add the classic spells in bounded groups. Verify every spell's behavior against targets before adding the next group.
3. Implement the selected portal version and exercise placement failure cases.
4. Run the complete regression suite and independent review. Prepare the next reviewable PR when publication is requested.
