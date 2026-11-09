// In-game test gamemode for the world stream: see ingame-tests.md. It spawns on Grove
// Street and runs one numbered step at a time. /next moves on, /step N jumps. Every step
// announces itself in chat. Runs on a private server, never the shared one.
#define SAMP_COMPAT
#include <open.mp>

#define Say(%0,%1) SendClientMessage(%0, 0xFFFF00FF, %1)
#define GX 2495.0
#define GY (-1680.0)
#define GZ 13.4

new gStep[MAX_PLAYERS];
new gObj[8], gMass[900], gMassCount;
new gPickup, gPickups[300], gPickupCount;
new gZone, gActor;
new Text3D:gLabel, Text3D:gFarLabel;

main() {}

public OnGameModeInit()
{
    AddPlayerClass(0, GX, GY, GZ, 0.0, WEAPON_DEAGLE, 200, WEAPON_FIST, 0, WEAPON_FIST, 0);
    return 1;
}

public OnPlayerRequestClass(playerid, classid) { return 1; }

public OnPlayerSpawn(playerid)
{
    Say(playerid, "World test ready. /next for step 1.");
    gStep[playerid] = 0;
    return 1;
}

Run(playerid, step)
{
    new s[96];
    gStep[playerid] = step;
    format(s, sizeof s, "Step %d", step);
    Say(playerid, s);
    SetPlayerPos(playerid, GX, GY, GZ);
    switch (step) {
    case 1: { // basic objects, a mapped id, a missing id
        gObj[0] = CreateObject(1337, GX + 3.0, GY + 5.0, GZ - 0.5, 0.0, 0.0, 0.0, 200.0);
        gObj[1] = CreateObject(3095, GX - 6.0, GY + 8.0, GZ, 0.0, 0.0, 45.0, 200.0);
        gObj[2] = CreateObject(18450, GX, GY + 40.0, GZ + 6.0, 0.0, 0.0, 90.0, 300.0);
        gObj[3] = CreateObject(19379, GX + 6.0, GY + 8.0, GZ, 0.0, 0.0, 0.0, 200.0);
    }
    case 2: { // set pos, rotation, move
        SetObjectPos(gObj[0], GX + 3.0, GY + 3.0, GZ - 0.5);
        SetObjectRot(gObj[1], 0.0, 0.0, 135.0);
        MoveObject(gObj[0], GX + 3.0, GY + 13.0, GZ - 0.5, 1.0, -1000.0, -1000.0, -1000.0);
    }
    case 3: { // move with a rotation target, then a stop
        MoveObject(gObj[1], GX - 6.0, GY + 18.0, GZ + 4.0, 1.5, 0.0, 0.0, 315.0);
        SetTimerEx("StopIt", 3000, false, "i", gObj[1]);
    }
    case 4: { // destroy
        for (new i = 0; i < 4; i++) DestroyObject(gObj[i]);
    }
    case 5: { // the object cap: 900 around the player
        for (new i = 0; i < 900; i++) {
            new Float:x = GX + float((i % 30) * 6) - 90.0;
            new Float:y = GY + float((i / 30) * 6) - 90.0;
            gMass[i] = CreateObject(1337, x, y, GZ - 0.5, 0.0, 0.0, 0.0, 300.0);
        }
        gMassCount = 900;
    }
    case 6: { // walk away from them
        SetPlayerPos(playerid, GX, GY - 400.0, GZ + 10.0);
    }
    case 7: {
        for (new i = 0; i < gMassCount; i++) DestroyObject(gMass[i]);
        gMassCount = 0;
    }
    case 8: { // attached to the player, then to a bone
        gObj[0] = CreateObject(1337, GX, GY, GZ, 0.0, 0.0, 0.0, 200.0);
        AttachObjectToPlayer(gObj[0], playerid, 0.0, 0.0, 1.5, 0.0, 0.0, 0.0);
        SetPlayerAttachedObject(playerid, 0, 18636, 2, 0.1, 0.0, 0.0);    // PC-only: skipped
        SetPlayerAttachedObject(playerid, 1, 1239, 2, 0.0, 0.0, 0.2);     // over the head
        SetPlayerAttachedObject(playerid, 2, 1240, 6, 0.0, 0.0, 0.0);     // right hand
    }
    case 9: {
        DestroyObject(gObj[0]);
        RemovePlayerAttachedObject(playerid, 1);
        RemovePlayerAttachedObject(playerid, 2);
    }
    case 10: { // pickups
        gPickup = CreatePickup(1240, 2, GX + 3.0, GY + 3.0, GZ, -1);
        for (new i = 0; i < 300; i++) {
            new Float:x = GX + float((i % 20) * 25) - 250.0;
            new Float:y = GY + float((i / 20) * 25) - 180.0;
            gPickups[i] = CreatePickup(1242, 2, x, y, GZ + 0.5, -1);
        }
        gPickupCount = 300;
    }
    case 11: {
        DestroyPickup(gPickup);
        for (new i = 0; i < gPickupCount; i++) DestroyPickup(gPickups[i]);
        gPickupCount = 0;
    }
    case 12: { // checkpoint
        SetPlayerCheckpoint(playerid, GX + 10.0, GY + 10.0, GZ, 3.0);
    }
    case 13: {
        DisablePlayerCheckpoint(playerid);
        SetPlayerRaceCheckpoint(playerid, CP_TYPE_GROUND_NORMAL, GX + 10.0, GY + 10.0, GZ, GX + 30.0, GY + 30.0, GZ, 5.0);
    }
    case 14: {
        SetPlayerRaceCheckpoint(playerid, CP_TYPE_GROUND_FINISH, GX + 30.0, GY + 30.0, GZ, GX, GY, GZ, 5.0);
    }
    case 15: { // an air one
        SetPlayerRaceCheckpoint(playerid, CP_TYPE_AIR_NORMAL, GX, GY + 40.0, GZ + 20.0, GX, GY + 80.0, GZ + 20.0, 8.0);
    }
    case 16: {
        DisablePlayerRaceCheckpoint(playerid);
    }
    case 17: { // map icons
        SetPlayerMapIcon(playerid, 1, GX + 50.0, GY + 50.0, GZ, 56, 0, MAPICON_LOCAL);
        SetPlayerMapIcon(playerid, 2, GX - 50.0, GY + 50.0, GZ, 0, 0xFF00FFFF, MAPICON_LOCAL);
        SetPlayerMapIcon(playerid, 3, GX + 2000.0, GY + 2000.0, GZ, 6, 0, MAPICON_GLOBAL);
        SetPlayerMapIcon(playerid, 4, GX - 80.0, GY, GZ, 22, 0, MAPICON_GLOBAL_CHECKPOINT);
    }
    case 18: {
        for (new i = 1; i <= 4; i++) RemovePlayerMapIcon(playerid, i);
    }
    case 19: { // gang zone
        gZone = GangZoneCreate(GX - 100.0, GY - 100.0, GX + 100.0, GY + 100.0);
        GangZoneShowForPlayer(playerid, gZone, 0xFF000080);
    }
    case 20: {
        GangZoneFlashForPlayer(playerid, gZone, 0x00FF0080);
    }
    case 21: {
        GangZoneStopFlashForPlayer(playerid, gZone);
        GangZoneHideForPlayer(playerid, gZone);
        GangZoneDestroy(gZone);
    }
    case 22: { // labels
        gLabel = Create3DTextLabel("{FF0000}Red {00FF00}green\n{FFFFFF}second line", 0xFFFFFFFF, GX + 3.0, GY + 5.0, GZ + 1.0, 40.0, 0, false);
        gFarLabel = Create3DTextLabel("Behind the wall", 0x00FFFFFF, 2495.0, -1707.0, 14.0, 60.0, 0, true);
    }
    case 23: {
        Delete3DTextLabel(gLabel);
        Delete3DTextLabel(gFarLabel);
    }
    case 24: { // time, clock, weather
        SetPlayerTime(playerid, 0, 0);
        SetPlayerWeather(playerid, 8);
    }
    case 25: {
        SetPlayerTime(playerid, 12, 0);
        TogglePlayerClock(playerid, true);
        SetPlayerWeather(playerid, 0);
    }
    case 26: {
        TogglePlayerClock(playerid, false);
    }
    case 27: { // gravity
        SetGravity(0.002);
    }
    case 28: {
        SetGravity(0.008);
    }
    case 29: { // sounds and explosions
        PlayerPlaySound(playerid, 1130, 0.0, 0.0, 0.0);
        PlayerPlaySound(playerid, 1058, GX, GY, GZ);
        CreateExplosionForPlayer(playerid, GX + 20.0, GY + 20.0, GZ, 2, 10.0);
    }
    case 30: { // CJ's house gone
        RemoveBuildingForPlayer(playerid, 17697, 2495.0, -1696.0, 17.0, 30.0);
        RemoveBuildingForPlayer(playerid, 17768, 2495.0, -1696.0, 17.0, 30.0);
    }
    case 31: { // interior
        SetPlayerInterior(playerid, 3);
        SetPlayerPos(playerid, 234.0, 1064.0, 1084.0);
    }
    case 32: {
        SetPlayerInterior(playerid, 0);
    }
    case 33: { // actors
        gActor = CreateActor(7, GX + 2.0, GY + 4.0, GZ, 180.0);
        SetActorInvulnerable(gActor, false);
        ApplyActorAnimation(gActor, "PED", "IDLE_CHAT", 4.1, true, false, false, false, 0);
    }
    case 34: {
        SetActorPos(gActor, GX + 4.0, GY + 4.0, GZ);
        SetActorFacingAngle(gActor, 90.0);
        ClearActorAnimations(gActor);
    }
    case 35: {
        SetActorHealth(gActor, 10.0);
    }
    case 36: {
        DestroyActor(gActor);
        Say(playerid, "All steps done.");
    }
    }
    return 1;
}

forward StopIt(object);
public StopIt(object) { StopObject(object); return 1; }

public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/next", true))
        return Run(playerid, gStep[playerid] + 1);
    if (!strcmp(cmdtext, "/step", true, 5))
        return Run(playerid, strval(cmdtext[6]));
    return 0;
}

public OnPlayerEnterCheckpoint(playerid) { Say(playerid, "ENTER checkpoint"); return 1; }
public OnPlayerLeaveCheckpoint(playerid) { Say(playerid, "LEAVE checkpoint"); return 1; }
public OnPlayerEnterRaceCheckpoint(playerid) { Say(playerid, "ENTER race checkpoint"); return 1; }
public OnPlayerLeaveRaceCheckpoint(playerid) { Say(playerid, "LEAVE race checkpoint"); return 1; }

public OnPlayerPickUpPickup(playerid, pickupid)
{
    new s[48];
    format(s, sizeof s, "PICKUP %d (first %d)", pickupid, gPickup);
    Say(playerid, s);
    return 1;
}

public OnPlayerGiveDamageActor(playerid, damaged_actorid, Float:amount, WEAPON:weaponid, bodypart)
{
    new s[64];
    format(s, sizeof s, "ACTOR HIT %.1f weapon %d", amount, _:weaponid);
    Say(playerid, s);
    return 1;
}

public OnPlayerInteriorChange(playerid, newinteriorid, oldinteriorid)
{
    new s[48];
    format(s, sizeof s, "INTERIOR %d", newinteriorid);
    Say(playerid, s);
    return 1;
}
