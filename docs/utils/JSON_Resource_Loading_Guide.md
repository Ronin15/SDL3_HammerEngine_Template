# JSON Resource Loading Guide

## Overview

The project now uses the unified `res/data/resources.json` catalog for runtime resource definitions. This file contains more than a minimal demo set: equipment, consumables, crafting materials, ores, gems, currencies, merchant goods, and gameplay resources all live in the same catalog.

Use `ResourceTemplateManager` for loading and fast handle lookup, then use handles at runtime.

## Recommended Flow

```cpp
auto& templates = ResourceTemplateManager::Instance();
templates.init();

auto wood = templates.getHandleById("wood");
auto ironOre = templates.getHandleById("iron_ore");
auto goldCoins = templates.getHandleById("gold_coins");
```

Cache the resulting handles in gameplay code instead of repeating string lookups.

## Important Schema Fields

Common fields still include:

- `id`
- `name`
- `category`
- `type`
- `value`
- `maxStackSize`
- `consumable`
- `properties`

## Branch-Specific Implications

### `maxStackSize` matters during gameplay

On this branch, stack sizes are gameplay-significant rather than cosmetic:

- harvested wood/stone/ore/gems can fill inventory quickly
- trading and gift flows depend on current stack limits
- harvesting falls back to dropped items when inventory insertion fails
- tests now exercise inventory capacity behavior more directly

When adding new resources, choose `maxStackSize` deliberately and assume it affects both UI and interaction logic.

### Merchant / economy content is now first-class

`resources.json` is no longer just a few sample items. It includes merchant-facing equipment, consumables, raw materials, and currencies used by the trading/social systems.

## Related Data

Resource loading now interacts closely with NPC class data in `res/data/classes.json`:

- `isMerchant`
- `startingItems`
- `emotionalResilience`

Merchant inventories and social/trade behavior are only coherent when both resource definitions and class definitions are kept in sync.

## Runtime Guidance

- load templates at startup, not mid-frame
- convert IDs to handles during initialization
- use handles in inventories, harvesting, crafting, and trade flows
- validate stack sizes and item value assumptions when changing gameplay balance
