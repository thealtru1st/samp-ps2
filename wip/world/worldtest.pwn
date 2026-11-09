// Test gamemode for the world stream. Runs on a private server, never the shared one.
// Everything a real server would do to the world happens on spawn, and each callback that
// proves the client's answer reached the server echoes as a "CB ..." message.
#define SAMP_COMPAT
#include <open.mp>

#define Echo(%0,%1) SendClientMessage(%0, 0xCBCBCBFF, %1)

new gPickupNear, gActor;
new gMover;

main() {}

public OnGameModeInit()
{
    AddPlayerClass(7, 100.0, 200.0, 10.0, 90.0, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    // Far more than the game could hold, spread over a wide area.
    for (new i = 0; i < 900; i++) {
        new Float:x = float((i % 30) * 40) - 500.0;
        new Float:y = float((i / 30) * 40) - 400.0;
        CreateObject(1337, x, y, 10.0, 0.0, 0.0, 0.0, 300.0);
    }
    CreateObject(18450, 101.0, 204.0, 10.0, 0.0, 0.0, 45.0, 300.0); // PS2 numbers this 12719
    CreateObject(19379, 102.0, 204.0, 10.0, 0.0, 0.0, 0.0, 300.0);  // SA-MP's own, not on PS2
    gMover = CreateObject(1338, 104.0, 204.0, 10.0, 0.0, 0.0, 0.0, 300.0);
    for (new i = 0; i < 300; i++) {
        new Float:x = float((i % 20) * 25) - 200.0;
        new Float:y = float((i / 20) * 25) - 100.0;
        CreatePickup(1240, 2, x, y, 10.0, -1);
    }
    gPickupNear = CreatePickup(1242, 2, 105.0, 200.0, 10.0, -1);
    gActor = CreateActor(7, 102.0, 205.0, 10.0, 30.0);
    SetActorInvulnerable(gActor, false);
    Create3DTextLabel("{FF0000}Red{FFFFFF} label\nsecond line", 0xFFFFFFFF, 100.0, 201.0, 11.0, 40.0, 0, false);
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    return 1;
}

public OnPlayerSpawn(playerid)
{
    SetPlayerCheckpoint(playerid, 130.0, 200.0, 10.0, 3.0);
    SetPlayerRaceCheckpoint(playerid, CP_TYPE_GROUND_NORMAL, 160.0, 200.0, 10.0, 170.0, 200.0, 10.0, 4.0);
    SetPlayerMapIcon(playerid, 5, 120.0, 220.0, 10.0, 56, 0, MAPICON_GLOBAL);
    SetPlayerMapIcon(playerid, 6, 122.0, 220.0, 10.0, 0, 0xFF0000FF, MAPICON_LOCAL);
    new zone = GangZoneCreate(50.0, 150.0, 250.0, 300.0);
    GangZoneShowForPlayer(playerid, zone, 0xFF000080);
    GangZoneFlashForPlayer(playerid, zone, 0x00FF0080);
    RemoveBuildingForPlayer(playerid, 18450, 2000.0, 2000.0, 10.0, 50.0);
    RemoveBuildingForPlayer(playerid, -1, 2100.0, 2000.0, 10.0, 25.0);
    SetPlayerAttachedObject(playerid, 0, 1337, 2, 0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0, 0);
    MoveObject(gMover, 104.0, 214.0, 10.0, 5.0, -1000.0, -1000.0, -1000.0);
    SetPlayerTime(playerid, 21, 30);
    SetPlayerWeather(playerid, 8);
    SetGravity(0.012);
    TogglePlayerClock(playerid, false);
    SetPlayerWorldBounds(playerid, 1000.0, -1000.0, 1000.0, -1000.0);
    PlayerPlaySound(playerid, 1058, 0.0, 0.0, 0.0);
    CreateExplosionForPlayer(playerid, 90.0, 190.0, 10.0, 2, 10.0);
    PlayAudioStreamForPlayer(playerid, "http://example.invalid/none.mp3");
    return 1;
}

public OnPlayerEnterCheckpoint(playerid) { Echo(playerid, "CB enter_checkpoint"); return 1; }
public OnPlayerLeaveCheckpoint(playerid) { Echo(playerid, "CB leave_checkpoint"); return 1; }
public OnPlayerEnterRaceCheckpoint(playerid) { Echo(playerid, "CB enter_race"); return 1; }
public OnPlayerLeaveRaceCheckpoint(playerid) { Echo(playerid, "CB leave_race"); return 1; }

public OnPlayerPickUpPickup(playerid, pickupid)
{
    new s[48];
    format(s, sizeof s, "CB pickup %d near=%d", pickupid, pickupid == gPickupNear);
    Echo(playerid, s);
    return 1;
}

public OnPlayerGiveDamageActor(playerid, damaged_actorid, Float:amount, WEAPON:weaponid, bodypart)
{
    new s[64];
    format(s, sizeof s, "CB actor_damage %d %.1f %d %d", damaged_actorid == gActor, amount, _:weaponid, bodypart);
    Echo(playerid, s);
    return 1;
}

public OnPlayerInteriorChange(playerid, newinteriorid, oldinteriorid)
{
    new s[48];
    format(s, sizeof s, "CB interior %d", newinteriorid);
    Echo(playerid, s);
    return 1;
}
