// Test gamemode for the combat stream. Private server only, never the shared one.
// Every callback prints a "CB" line, and a timer prints what the server believes about
// each player, so run_live.sh can check the client's bytes against the server's own view.
#define SAMP_COMPAT
#include <open.mp>

new gSpawns[MAX_PLAYERS];
new gStep[MAX_PLAYERS];

main() {}

forward Report();
public Report()
{
    for (new i = 0; i < 4; i++) {
        if (!IsPlayerConnected(i)) continue;
        new keys, ud, lr, Float:hp, Float:ar, w, a;
        GetPlayerKeys(i, keys, ud, lr);
        GetPlayerHealth(i, hp);
        GetPlayerArmour(i, ar);
        GetPlayerWeaponData(i, 2, w, a);
        new Float:fx, Float:fy, Float:fz;
        GetPlayerCameraFrontVector(i, fx, fy, fz);
        printf("ST %d state=%d keys=%d ud=%d lr=%d hp=%.0f ar=%.0f weapon=%d action=%d anim=%d money=%d slot2=%d/%d cam=%d front=%.1f",
            i, GetPlayerState(i), keys, ud, lr, hp, ar, GetPlayerWeapon(i),
            GetPlayerSpecialAction(i), GetPlayerAnimationIndex(i), GetPlayerMoney(i), w, a,
            GetPlayerCameraMode(i), fx);
    }
    return 1;
}

public OnGameModeInit()
{
    AddPlayerClass(7, 1900.0, -2490.0, 13.6, 90.0, WEAPON_DEAGLE, 100, WEAPON_FIST, 0, WEAPON_FIST, 0);
    SetTimer("Report", 400, true);
    return 1;
}

public OnPlayerSpawn(playerid)
{
    gSpawns[playerid]++;
    printf("CB spawn %d count=%d", playerid, gSpawns[playerid]);
    GivePlayerMoney(playerid, 1234);
    GivePlayerWeapon(playerid, WEAPON_M4, 90);
    SetPlayerHealth(playerid, 75.0);
    SetPlayerArmour(playerid, 25.0);
    SetPlayerSkillLevel(playerid, WEAPONSKILL_PISTOL, 500);
    SetPlayerFightingStyle(playerid, FIGHT_STYLE_BOXING);
    ApplyAnimation(playerid, "PED", "IDLE_CHAT", 4.1, false, false, false, false, 0);
    SetPlayerSpecialAction(playerid, SPECIAL_ACTION_HANDSUP);
    SetPlayerWantedLevel(playerid, 3);
    SetPlayerInterior(playerid, 0);
    return 1;
}

// One server action per press of R3 (KEY_SUBMISSION), for wip/combat/ingame-tests.md.
Step(playerid)
{
    new n = ++gStep[playerid];
    new Float:x, Float:y, Float:z;
    GetPlayerPos(playerid, x, y, z);
    printf("CB step %d", n);
    switch (n) {
        case 1: { GivePlayerWeapon(playerid, WEAPON_SHOTGUN, 30); Say(playerid, "1 shotgun, 30 shells, armed"); }
        case 2: { SetPlayerAmmo(playerid, WEAPON_SHOTGUN, 5); Say(playerid, "2 shotgun ammo 5"); }
        case 3: { SetPlayerArmedWeapon(playerid, WEAPON_FIST); Say(playerid, "3 armed with fists"); }
        case 4: { ResetPlayerWeapons(playerid); Say(playerid, "4 all weapons gone"); }
        case 5: { SetPlayerHealth(playerid, 30.0); Say(playerid, "5 health 30"); }
        case 6: { SetPlayerArmour(playerid, 80.0); Say(playerid, "6 armour 80"); }
        case 7: { GivePlayerMoney(playerid, 5000); Say(playerid, "7 money +5000"); }
        case 8: { ResetPlayerMoney(playerid); Say(playerid, "8 money 0"); }
        case 9: { SetPlayerWantedLevel(playerid, 4); Say(playerid, "9 four wanted stars, no police"); }
        case 10: { SetPlayerWantedLevel(playerid, 0); Say(playerid, "10 no stars"); }
        case 11: { SetPlayerFacingAngle(playerid, 180.0); Say(playerid, "11 facing south"); }
        case 12: { SetPlayerPosFindZ(playerid, x + 30.0, y, z + 50.0); Say(playerid, "12 moved 30m east, on the ground"); }
        case 13: { SetPlayerVelocity(playerid, 0.0, 0.0, 0.4); Say(playerid, "13 thrown upwards"); }
        case 14: { TogglePlayerControllable(playerid, false); Say(playerid, "14 frozen: try to move"); }
        case 15: { TogglePlayerControllable(playerid, true); Say(playerid, "15 free"); }
        case 16: { SetPlayerCameraPos(playerid, x + 5.0, y + 5.0, z + 5.0); SetPlayerCameraLookAt(playerid, x, y, z, CAMERA_CUT); Say(playerid, "16 fixed camera on you"); }
        case 17: { InterpolateCameraPos(playerid, x + 20.0, y, z + 5.0, x, y + 20.0, z + 5.0, 3000, CAMERA_MOVE); Say(playerid, "17 camera glides for 3s"); }
        case 18: { SetCameraBehindPlayer(playerid); Say(playerid, "18 camera back"); }
        case 19: { SetPlayerInterior(playerid, 6); SetPlayerPos(playerid, -26.69, -55.71, 1003.55); Say(playerid, "19 interior 3"); }
        case 20: { SetPlayerInterior(playerid, 0); SetPlayerPos(playerid, 1900.0, -2490.0, 13.6); Say(playerid, "20 back outside"); }
        case 21: { SetPlayerTime(playerid, 2, 0); SetPlayerWeather(playerid, 8); Say(playerid, "21 2am, rain"); }
        case 22: { SetPlayerTime(playerid, 12, 0); SetPlayerWeather(playerid, 1); Say(playerid, "22 noon, clear"); }
        case 23: { ApplyAnimation(playerid, "DANCING", "DNCE_M_A", 4.1, true, false, false, false, 0); Say(playerid, "23 dancing, loops"); }
        case 24: { ClearAnimations(playerid); Say(playerid, "24 stopped"); }
        case 25: { ApplyAnimation(playerid, "NOSUCHLIB", "X", 4.1, false, false, false, false, 0); ApplyAnimation(playerid, "PED", "NOSUCHANIM", 4.1, false, false, false, false, 0); Say(playerid, "25 two bad animations: nothing happens"); }
        case 26: { SetPlayerSpecialAction(playerid, SPECIAL_ACTION_HANDSUP); Say(playerid, "26 hands up"); }
        case 27: { SetPlayerSpecialAction(playerid, SPECIAL_ACTION_NONE); Say(playerid, "27 hands down"); }
        case 28: { SetPlayerSpecialAction(playerid, SPECIAL_ACTION_USEJETPACK); Say(playerid, "28 jetpack"); }
        case 29: { SetPlayerSpecialAction(playerid, SPECIAL_ACTION_NONE); SetPlayerFightingStyle(playerid, FIGHT_STYLE_KUNGFU); Say(playerid, "29 kung fu: punch"); }
        case 30: { SetPlayerSkin(playerid, 280); Say(playerid, "30 police skin"); }
        case 31: { SetPlayerSkin(playerid, 7); SetPlayerHealth(playerid, 0.0); Say(playerid, "31 killed by the server"); }
        case 32: { ForceClassSelection(playerid); SetPlayerHealth(playerid, 0.0); Say(playerid, "32 killed, class selection next"); }
        case 33: { TogglePlayerSpectating(playerid, true); if (IsPlayerConnected(1)) PlayerSpectatePlayer(playerid, 1); Say(playerid, "33 spectating player 1"); }
        case 34: { TogglePlayerSpectating(playerid, false); Say(playerid, "34 spectate off"); gStep[playerid] = 0; }
    }
}

Say(playerid, const text[])
{
    SendClientMessage(playerid, 0xFFFF00FF, text);
    printf("CB said %s", text);
}

public OnPlayerKeyStateChange(playerid, KEY:newkeys, KEY:oldkeys)
{
    printf("CB keys %d new=%d old=%d", playerid, _:newkeys, _:oldkeys);
    if ((newkeys & KEY_SUBMISSION) && !(oldkeys & KEY_SUBMISSION))
        Step(playerid);
    return 1;
}

public OnPlayerTakeDamage(playerid, issuerid, Float:amount, WEAPON:weaponid, bodypart)
{
    printf("CB take %d issuer=%d amount=%.2f weapon=%d part=%d", playerid, issuerid, amount, _:weaponid, bodypart);
    return 1;
}

public OnPlayerGiveDamage(playerid, damagedid, Float:amount, WEAPON:weaponid, bodypart)
{
    printf("CB give %d damaged=%d amount=%.2f weapon=%d part=%d", playerid, damagedid, amount, _:weaponid, bodypart);
    return 1;
}

public OnPlayerDeath(playerid, killerid, WEAPON:reason)
{
    printf("CB death %d killer=%d reason=%d", playerid, killerid, _:reason);
    return 1;
}

public OnPlayerWeaponShot(playerid, WEAPON:weaponid, BULLET_HIT_TYPE:hittype, hitid, Float:fX, Float:fY, Float:fZ)
{
    printf("CB shot %d weapon=%d type=%d hit=%d", playerid, _:weaponid, _:hittype, hitid);
    return 1;
}

public OnPlayerInteriorChange(playerid, newinteriorid, oldinteriorid)
{
    printf("CB interior %d new=%d", playerid, newinteriorid);
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    return 1;
}

// R3 opens the browser in the merged client, so the steps also run from chat: /cs N
public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/cs", true, 3)) {
        gStep[playerid] = strval(cmdtext[4]) - 1;
        Step(playerid);
        return 1;
    }
    if (!strcmp(cmdtext, "/tk", true)) { ResetPlayerWeapons(playerid); GivePlayerWeapon(playerid, WEAPON_KNIFE, 1); GivePlayerWeapon(playerid, WEAPON_COLT45, 65535); return 1; }
    if (!strcmp(cmdtext, "/gg", true)) { SetPlayerPos(playerid, -1321.0, 2526.0, 87.5); SetPlayerWorldBounds(playerid, -1274.2817, -1358.5095, 2575.6509, 2472.3486); return 1; }
    if (!strcmp(cmdtext, "/bounds", true)) { SetPlayerWorldBounds(playerid, 1910.0, 1880.0, -2470.0, -2510.0); return 1; }
    if (!strcmp(cmdtext, "/nobounds", true)) { SetPlayerWorldBounds(playerid, 20000.0, -20000.0, 20000.0, -20000.0); return 1; }
    if (!strcmp(cmdtext, "/rocket", true)) { GivePlayerWeapon(playerid, WEAPON_ROCKETLAUNCHER, 10); return 1; }
    if (!strcmp(cmdtext, "/m4", true)) { GivePlayerWeapon(playerid, WEAPON_M4, 200); return 1; }
    if (!strcmp(cmdtext, "/hp", true)) { SetPlayerHealth(playerid, 100.0); SetPlayerArmour(playerid, 0.0); return 1; }
    if (!strcmp(cmdtext, "/low", true)) { SetPlayerHealth(playerid, 20.0); SetPlayerArmour(playerid, 0.0); return 1; }
    if (!strcmp(cmdtext, "/kbots", true)) { for (new i = 1; i < 4; i++) { if (IsPlayerConnected(i)) SetPlayerHealth(i, 15.0); } return 1; }
    return 0;
}
