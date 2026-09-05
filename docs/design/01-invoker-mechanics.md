# 01 Invoker mechanics (draft)

Status: draft for discussion. Nothing here is implemented.

## Orbs

Three orb types, three slots. Pressing an orb key pushes that orb into the slots and drops the oldest. Order does not matter for the invocation, only the multiset (same as DotA Invoker).

Working names, subject to change:

| Orb | Theme | Passive while held (per orb count) |
|-----|-------|-----------------------------------|
| Q | Frost / control | health regeneration |
| W | Storm / mobility | movement speed |
| E | Fire / damage | damage bonus |

## Invocation

Press Invoke. The engine reads the current three-orb multiset and equips the matching weapon into one of two invoked-weapon slots (newest replaces oldest). Ten combinations exist with three orbs of three types:

| Combination | Weapon / ability idea | Quake analogue |
|-------------|-----------------------|----------------|
| QQQ | Frost wall (blocking, slows) | none |
| WWW | Blink dash | none |
| EEE | Sunstrike (delayed AoE) | BFG-lite |
| QQW | Ice shards (projectile that lays a wall) | plasma |
| QQE | Frost rockets (slow on hit) | rocket launcher |
| WWQ | Tornado (knock up, self launch) | none |
| WWE | Chaos lightning (bouncing) | lightning gun |
| EEQ | Meteor roll (rolling area burn) | none |
| EEW | Alacrity (fire rate buff) | haste |
| QWE | Deafening blast (knockback shot) | shotgun / railgun-ish |

The Quake column is a design aid: which existing weapon code paths in `code/game/g_weapon.c` and `code/cgame/cg_weapons.c` each invocation can start from.

## Implementation sketch

- `code/game/bg_public.h`: add orb state and invoked weapon slots to `playerState_t` stats (fits in `persistant`/`stats` arrays or new fields; check network delta cost).
- `code/game/g_active.c`: read orb and invoke button state from `usercmd_t`.
- `code/game/g_weapon.c`: map invocation id to a fire function.
- `code/cgame/cg_draw.c`: draw orb slots and invoked weapons in the HUD.
- `code/ui/`: keybinds for Q, W, E, Invoke.

Keep every change server-authoritative and predicted in `bg_pmove.c` where movement is affected (blink, tornado).
