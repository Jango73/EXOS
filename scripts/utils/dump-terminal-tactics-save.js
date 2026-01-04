'use strict';

const fs = require('fs');

const filePath = process.argv[2];
if (!filePath) {
    console.error('Missing save file path.');
    process.exit(1);
}

const buffer = fs.readFileSync(filePath);

const MAX_TEAMS = 5;
const MAX_PLACEMENT_QUEUE = 3;
const MAX_UNIT_QUEUE = 3;

const TERRAIN_TYPE_MASK = 0x3f;
const TERRAIN_NAMES = {
    0: 'PLAINS',
    1: 'MOUNTAIN',
    2: 'FOREST',
    3: 'WATER',
    4: 'PLASMA'
};

const BUILDING_NAMES = {
    1: 'CONSTRUCTION_YARD',
    2: 'BARRACKS',
    3: 'POWER_PLANT',
    4: 'FACTORY',
    5: 'TECH_CENTER',
    6: 'TURRET',
    7: 'WALL'
};

const UNIT_NAMES = {
    1: 'TROOPER',
    2: 'SOLDIER',
    3: 'ENGINEER',
    4: 'SCOUT',
    5: 'MOBILE_ARTILLERY',
    6: 'TANK',
    7: 'TRANSPORT',
    8: 'DRILLER'
};

const ATTITUDE_NAMES = {
    1: 'AGGRESSIVE',
    2: 'DEFENSIVE'
};

const MINDSET_NAMES = {
    0: 'IDLE',
    1: 'URGENCY',
    2: 'PANIC'
};

function readUInt32LE(buf, offset) {
    return buf.readUInt32LE(offset);
}

function readInt32LE(buf, offset) {
    return buf.readInt32LE(offset);
}

function readBigUInt64LE(buf, offset) {
    return buf.readBigUInt64LE(offset);
}

function createReader(buf, ptrSize) {
    let offset = 0;

    const readI32 = () => {
        const value = readInt32LE(buf, offset);
        offset += 4;
        return value;
    };

    const readU32 = () => {
        const value = readUInt32LE(buf, offset);
        offset += 4;
        return value;
    };

    const readWord = () => {
        if (ptrSize === 8) {
            const value = readBigUInt64LE(buf, offset);
            offset += 8;
            return value;
        }
        return readU32();
    };

    const align = (alignment) => {
        const mask = alignment - 1;
        if ((offset & mask) !== 0) {
            offset = (offset + mask) & ~mask;
        }
    };

    const typeInfo = {
        i32: { size: 4, align: 4, read: readI32 },
        u32: { size: 4, align: 4, read: readU32 },
        word: { size: ptrSize, align: ptrSize, read: readWord },
        ptr: { size: ptrSize, align: ptrSize, read: readWord }
    };

    function readField(type) {
        const info = typeInfo[type];
        align(info.align);
        return info.read();
    }

    function parseStruct(layout) {
        const start = offset;
        let maxAlign = 1;
        const result = {};

        for (const field of layout) {
            const info = field.type ? typeInfo[field.type] : null;
            if (info && info.align > maxAlign) {
                maxAlign = info.align;
            }

            if (field.count && field.count > 1) {
                const arr = [];
                for (let i = 0; i < field.count; i++) {
                    if (field.layout) {
                        arr.push(parseStruct(field.layout));
                    } else {
                        arr.push(readField(field.type));
                    }
                }
                result[field.name] = arr;
            } else if (field.layout) {
                result[field.name] = parseStruct(field.layout);
            } else {
                result[field.name] = readField(field.type);
            }
        }

        align(maxAlign);
        result._structSize = offset - start;
        return result;
    }

    function getOffset() {
        return offset;
    }

    function setOffset(value) {
        offset = value;
    }

    return {
        readI32,
        readU32,
        readWord,
        align,
        parseStruct,
        getOffset,
        setOffset
    };
}

const buildJobLayout = [
    { name: 'TypeId', type: 'i32' },
    { name: 'TimeRemaining', type: 'u32' }
];

const unitJobLayout = [
    { name: 'TypeId', type: 'i32' },
    { name: 'TimeRemaining', type: 'u32' }
];

const buildingLayout = [
    { name: 'Id', type: 'i32' },
    { name: 'TypeId', type: 'i32' },
    { name: 'X', type: 'i32' },
    { name: 'Y', type: 'i32' },
    { name: 'Hp', type: 'i32' },
    { name: 'Team', type: 'i32' },
    { name: 'Level', type: 'i32' },
    { name: 'BuildTimeRemaining', type: 'u32' },
    { name: 'UnderConstruction', type: 'word' },
    { name: 'BuildQueue', layout: buildJobLayout, count: MAX_PLACEMENT_QUEUE },
    { name: 'BuildQueueCount', type: 'i32' },
    { name: 'UnitQueue', layout: unitJobLayout, count: MAX_UNIT_QUEUE },
    { name: 'UnitQueueCount', type: 'i32' },
    { name: 'LastDamageTime', type: 'u32' },
    { name: 'LastAttackTime', type: 'u32' },
    { name: 'Next', type: 'ptr' }
];

const unitLayout = [
    { name: 'Id', type: 'i32' },
    { name: 'TypeId', type: 'i32' },
    { name: 'X', type: 'i32' },
    { name: 'Y', type: 'i32' },
    { name: 'Hp', type: 'i32' },
    { name: 'Team', type: 'i32' },
    { name: 'State', type: 'i32' },
    { name: 'EscortUnitId', type: 'i32' },
    { name: 'EscortUnitTeam', type: 'i32' },
    { name: 'StateTargetX', type: 'i32' },
    { name: 'StateTargetY', type: 'i32' },
    { name: 'IsMoving', type: 'word' },
    { name: 'TargetX', type: 'i32' },
    { name: 'TargetY', type: 'i32' },
    { name: 'IsSelected', type: 'word' },
    { name: 'LastAttackTime', type: 'u32' },
    { name: 'LastDamageTime', type: 'u32' },
    { name: 'LastHarvestTime', type: 'u32' },
    { name: 'LastStateUpdateTime', type: 'u32' },
    { name: 'MoveProgress', type: 'u32' },
    { name: 'LastMoveX', type: 'i32' },
    { name: 'LastMoveY', type: 'i32' },
    { name: 'LastMoveTime', type: 'u32' },
    { name: 'StuckDetourActive', type: 'word' },
    { name: 'StuckDetourCount', type: 'u32' },
    { name: 'StuckOriginalTargetX', type: 'i32' },
    { name: 'StuckOriginalTargetY', type: 'i32' },
    { name: 'StuckDetourTargetX', type: 'i32' },
    { name: 'StuckDetourTargetY', type: 'i32' },
    { name: 'IsGridlocked', type: 'word' },
    { name: 'GridlockLastUpdateTime', type: 'u32' },
    { name: 'PathHead', type: 'ptr' },
    { name: 'PathTail', type: 'ptr' },
    { name: 'PathTargetX', type: 'i32' },
    { name: 'PathTargetY', type: 'i32' },
    { name: 'Next', type: 'ptr' }
];

function parseSave(ptrSize) {
    const reader = createReader(buffer, ptrSize);
    const magic = reader.readU32();
    const version = reader.readU32();

    if (magic !== 0x54544143) {
        return { ok: false, reason: 'invalid magic' };
    }

    const mapWidth = reader.readI32();
    const mapHeight = reader.readI32();
    const difficulty = reader.readI32();
    const viewportX = reader.readI32();
    const viewportY = reader.readI32();
    const teamCount = reader.readI32();

    const resources = [];
    for (let i = 0; i < MAX_TEAMS; i++) {
        resources.push({
            Plasma: reader.readI32(),
            Energy: reader.readI32(),
            MaxEnergy: reader.readI32()
        });
    }

    const aiAttitudes = [];
    const aiMindsets = [];
    for (let i = 0; i < MAX_TEAMS; i++) aiAttitudes.push(reader.readI32());
    for (let i = 0; i < MAX_TEAMS; i++) aiMindsets.push(reader.readI32());

    const gameTime = reader.readU32();
    const lastUpdate = reader.readU32();
    const gameSpeed = reader.readI32();
    const isPaused = reader.readI32();
    const menuPage = reader.readI32();
    const showGrid = reader.readI32();
    const showCoordinates = reader.readI32();
    const isPlacing = reader.readI32();
    const pendingType = reader.readI32();
    const placementX = reader.readI32();
    const placementY = reader.readI32();

    const terrain = new Uint8Array(mapWidth * mapHeight);
    for (let y = 0; y < mapHeight; y++) {
        for (let x = 0; x < mapWidth; x++) {
            terrain[y * mapWidth + x] = buffer[reader.getOffset()];
            reader.setOffset(reader.getOffset() + 1);
        }
    }

    const plasma = new Int32Array(mapWidth * mapHeight);
    for (let y = 0; y < mapHeight; y++) {
        for (let x = 0; x < mapWidth; x++) {
            plasma[y * mapWidth + x] = reader.readI32();
        }
    }

    const buildingCount = reader.readU32();
    const buildings = [];
    for (let i = 0; i < buildingCount; i++) {
        buildings.push(reader.parseStruct(buildingLayout));
    }

    const unitCount = reader.readU32();
    const units = [];
    for (let i = 0; i < unitCount; i++) {
        units.push(reader.parseStruct(unitLayout));
    }

    const endOffset = reader.getOffset();
    return {
        ok: true,
        ptrSize,
        endOffset,
        version,
        mapWidth,
        mapHeight,
        difficulty,
        viewportX,
        viewportY,
        teamCount,
        resources,
        aiAttitudes,
        aiMindsets,
        gameTime,
        lastUpdate,
        gameSpeed,
        isPaused,
        menuPage,
        showGrid,
        showCoordinates,
        isPlacing,
        pendingType,
        placementX,
        placementY,
        terrain,
        plasma,
        buildings,
        units
    };
}

function safeParse(ptrSize) {
    try {
        return parseSave(ptrSize);
    } catch (err) {
        return { ok: false, error: err && err.message ? err.message : String(err) };
    }
}

const attempt32 = safeParse(4);
const attempt64 = safeParse(8);

let parsed = null;

if (attempt32.ok && attempt32.endOffset === buffer.length) {
    parsed = attempt32;
} else if (attempt64.ok && attempt64.endOffset === buffer.length) {
    parsed = attempt64;
} else if (attempt32.ok) {
    parsed = attempt32;
} else if (attempt64.ok) {
    parsed = attempt64;
}

if (!parsed || !parsed.ok) {
    console.error('Failed to parse save file.');
    process.exit(1);
}

function terrainAt(x, y) {
    if (x < 0 || y < 0 || x >= parsed.mapWidth || y >= parsed.mapHeight) return null;
    const bits = parsed.terrain[y * parsed.mapWidth + x];
    return bits & TERRAIN_TYPE_MASK;
}

function plasmaAt(x, y) {
    if (x < 0 || y < 0 || x >= parsed.mapWidth || y >= parsed.mapHeight) return null;
    return parsed.plasma[y * parsed.mapWidth + x];
}

function wrapCoord(value, size) {
    if (size <= 0) return value;
    let result = value % size;
    if (result < 0) result += size;
    return result;
}

function countPlasmaInFootprint(x, y, width, height) {
    let count = 0;
    for (let dy = 0; dy < height; dy++) {
        for (let dx = 0; dx < width; dx++) {
            const px = wrapCoord(x + dx, parsed.mapWidth);
            const py = wrapCoord(y + dy, parsed.mapHeight);
            const plasma = plasmaAt(px, py);
            if (plasma !== null && plasma > 0) {
                count++;
            }
        }
    }
    return count;
}

function hasBlockedTerrainInFootprint(x, y, width, height) {
    for (let dy = 0; dy < height; dy++) {
        for (let dx = 0; dx < width; dx++) {
            const px = wrapCoord(x + dx, parsed.mapWidth);
            const py = wrapCoord(y + dy, parsed.mapHeight);
            const terrain = terrainAt(px, py);
            if (terrain === 1 || terrain === 3) {
                return true;
            }
        }
    }
    return false;
}

console.log(`File: ${filePath}`);
console.log(`PointerSize: ${parsed.ptrSize}`);
console.log(`Version: ${parsed.version}`);
console.log(`Map: ${parsed.mapWidth} x ${parsed.mapHeight}`);
console.log(`Difficulty: ${parsed.difficulty}`);
console.log(`Teams: ${parsed.teamCount}`);
console.log(`GameTime: ${parsed.gameTime}`);
console.log(`LastUpdate: ${parsed.lastUpdate}`);
console.log(`GameSpeed: ${parsed.gameSpeed}`);
console.log(`Paused: ${parsed.isPaused !== 0}`);
console.log(`Viewport: ${parsed.viewportX},${parsed.viewportY}`);
console.log(`MenuPage: ${parsed.menuPage}`);
console.log(`ShowGrid: ${parsed.showGrid !== 0}`);
console.log(`ShowCoordinates: ${parsed.showCoordinates !== 0}`);
console.log(`Placing: ${parsed.isPlacing !== 0} PendingType=${parsed.pendingType} Pos=${parsed.placementX},${parsed.placementY}`);

console.log('');
console.log('Teams:');
for (let i = 0; i < parsed.teamCount; i++) {
    const res = parsed.resources[i];
    const attitude = ATTITUDE_NAMES[parsed.aiAttitudes[i]] || `UNKNOWN(${parsed.aiAttitudes[i]})`;
    const mindset = MINDSET_NAMES[parsed.aiMindsets[i]] || `UNKNOWN(${parsed.aiMindsets[i]})`;
    console.log(`- Team ${i}: Plasma=${res.Plasma} Energy=${res.Energy}/${res.MaxEnergy} Attitude=${attitude} Mindset=${mindset}`);
}

const unitsByTeam = new Map();
for (const unit of parsed.units) {
    if (!unitsByTeam.has(unit.Team)) unitsByTeam.set(unit.Team, []);
    unitsByTeam.get(unit.Team).push(unit);
}

const buildingsByTeam = new Map();
for (const building of parsed.buildings) {
    if (!buildingsByTeam.has(building.Team)) buildingsByTeam.set(building.Team, []);
    buildingsByTeam.get(building.Team).push(building);
}

console.log('');
console.log(`Buildings: ${parsed.buildings.length}`);
for (let team = 0; team < parsed.teamCount; team++) {
    const list = buildingsByTeam.get(team) || [];
    console.log(`- Team ${team}: ${list.length}`);
}

console.log('');
console.log(`Units: ${parsed.units.length}`);
for (let team = 0; team < parsed.teamCount; team++) {
    const list = unitsByTeam.get(team) || [];
    console.log(`- Team ${team}: ${list.length}`);
}

console.log('');
console.log('Building list:');
for (const b of parsed.buildings) {
    const name = BUILDING_NAMES[b.TypeId] || `UNKNOWN(${b.TypeId})`;
    const underConstruction = (typeof b.UnderConstruction === 'bigint')
        ? b.UnderConstruction !== 0n
        : b.UnderConstruction !== 0;
    console.log(`- Id=${b.Id} Team=${b.Team} Type=${name} Pos=${b.X},${b.Y} Hp=${b.Hp} UC=${underConstruction} BT=${b.BuildTimeRemaining} BQ=${b.BuildQueueCount} UQ=${b.UnitQueueCount}`);
}

console.log('');
console.log('Unit list:');
for (const u of parsed.units) {
    const name = UNIT_NAMES[u.TypeId] || `UNKNOWN(${u.TypeId})`;
    const isMoving = (typeof u.IsMoving === 'bigint') ? u.IsMoving !== 0n : u.IsMoving !== 0;
    const isSelected = (typeof u.IsSelected === 'bigint') ? u.IsSelected !== 0n : u.IsSelected !== 0;
    const stuck = (typeof u.StuckDetourActive === 'bigint') ? u.StuckDetourActive !== 0n : u.StuckDetourActive !== 0;
    const gridlocked = (typeof u.IsGridlocked === 'bigint') ? u.IsGridlocked !== 0n : u.IsGridlocked !== 0;
    const targetTerrain = terrainAt(u.TargetX, u.TargetY);
    const targetPlasma = plasmaAt(u.TargetX, u.TargetY);
    const stateTargetTerrain = terrainAt(u.StateTargetX, u.StateTargetY);
    const stateTargetPlasma = plasmaAt(u.StateTargetX, u.StateTargetY);
    const targetTerrainName = targetTerrain === null ? 'OUT' : (TERRAIN_NAMES[targetTerrain] || `TYPE(${targetTerrain})`);
    const stateTerrainName = stateTargetTerrain === null ? 'OUT' : (TERRAIN_NAMES[stateTargetTerrain] || `TYPE(${stateTargetTerrain})`);
    const targetPlasmaText = targetPlasma === null ? 'n/a' : `${targetPlasma}`;
    const statePlasmaText = stateTargetPlasma === null ? 'n/a' : `${stateTargetPlasma}`;
    console.log(`- Id=${u.Id} Team=${u.Team} Type=${name} Pos=${u.X},${u.Y} Hp=${u.Hp} State=${u.State} Move=${isMoving} Target=${u.TargetX},${u.TargetY} TT=${targetTerrainName} TP=${targetPlasmaText} ST=${u.StateTargetX},${u.StateTargetY} STT=${stateTerrainName} STP=${statePlasmaText} Escort=${u.EscortUnitId}/${u.EscortUnitTeam} Selected=${isSelected} Stuck=${stuck} Gridlock=${gridlocked} StuckCount=${u.StuckDetourCount}`);
}

const drillers = parsed.units.filter((u) => u.TypeId === 8);
if (drillers.length > 0) {
    console.log('');
    console.log('Drillers:');
    for (const u of drillers) {
        const targetTerrain = terrainAt(u.TargetX, u.TargetY);
        const targetPlasma = plasmaAt(u.TargetX, u.TargetY);
        const targetPlasmaCount = countPlasmaInFootprint(u.TargetX, u.TargetY, 5, 2);
        const targetBlocked = hasBlockedTerrainInFootprint(u.TargetX, u.TargetY, 5, 2);
        const terrainName = targetTerrain === null ? 'OUT' : (TERRAIN_NAMES[targetTerrain] || `TYPE(${targetTerrain})`);
        console.log(`- Id=${u.Id} Team=${u.Team} Pos=${u.X},${u.Y} Target=${u.TargetX},${u.TargetY} Terrain=${terrainName} Plasma=${targetPlasma} TargetPlasmaCount=${targetPlasmaCount} TargetBlocked=${targetBlocked}`);
    }
}

console.log('');
console.log(`Parsed bytes: ${parsed.endOffset} / ${buffer.length}`);
