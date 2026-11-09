// Test gamemode for the RPC decoders. Runs on a private server, never the shared
// one. /t1 to /t9 make the real server emit RPCs with known values, and every
// callback echoes what it received as a "CB ..." message, so the encoders get
// checked by the server's own parsers. See live_expected.txt and live_steps.inc.
#define SAMP_COMPAT
#include <open.mp>

#define Echo(%0,%1) SendClientMessage(%0, 0xCBCBCBFF, %1)

new gCar, gTruck, gTrailer, gObject, gObject2, gActor, gPickup, gZone;
new Text3D:gLabel;
new Text:gText;
new Menu:gMenu;
new PlayerText:gPlayerText[MAX_PLAYERS];
new gPlayerObject[MAX_PLAYERS];
new gPlayerPickup[MAX_PLAYERS];

main() {}

public OnGameModeInit()
{
    AddPlayerClass(7, 100.0, 200.0, 10.0, 90.0, WEAPON_DEAGLE, 100, WEAPON_FIST, 0, WEAPON_FIST, 0);
    gCar = CreateVehicle(411, 105.0, 200.0, 10.0, 45.0, 6, 7, -1);
    gTruck = CreateVehicle(403, 120.0, 200.0, 10.0, 0.0, 1, 1, -1);
    gTrailer = CreateVehicle(435, 130.0, 200.0, 10.0, 0.0, 1, 1, -1);
    gObject = CreateObject(1337, 103.0, 206.0, 10.0, 1.0, 2.0, 3.0, 300.0);
    SetObjectMaterial(gObject, 1, 19341, "egg_txd", "egg_tex", 0xFF00FF00);
    SetObjectMaterialText(gObject, "Object text", 2, OBJECT_MATERIAL_SIZE_256x128, "Arial", 24, true, 0xFFFFFFFF, 0xFF000000, OBJECT_MATERIAL_TEXT_ALIGN_CENTER);
    gObject2 = CreateObject(1338, 104.0, 206.0, 10.0, 0.0, 0.0, 0.0, 300.0);
    AttachObjectToVehicle(gObject2, gCar, 0.5, 0.25, 0.125, 10.0, 20.0, 30.0);
    gActor = CreateActor(7, 102.0, 205.0, 10.0, 30.0);
    SetActorInvulnerable(gActor, false);
    gPickup = CreatePickup(1240, 2, 101.0, 203.0, 10.0, -1);
    gZone = GangZoneCreate(1.0, 2.0, 30.0, 40.0);
    gLabel = Create3DTextLabel("Global label text", 0x11223344, 100.0, 201.0, 11.0, 40.0, 0, true);
    gText = TextDrawCreate(320.0, 240.0, "global ~r~text");
    TextDrawLetterSize(gText, 0.5, 1.5);
    TextDrawTextSize(gText, 640.0, 480.0);
    TextDrawAlignment(gText, TEXT_DRAW_ALIGN_CENTER);
    TextDrawColor(gText, 0x11223344);
    TextDrawUseBox(gText, true);
    TextDrawBoxColor(gText, 0x55667788);
    TextDrawSetShadow(gText, 2);
    TextDrawSetOutline(gText, 1);
    TextDrawBackgroundColor(gText, 0x99AABBCC);
    TextDrawFont(gText, TEXT_DRAW_FONT_MODEL_PREVIEW);
    TextDrawSetProportional(gText, true);
    TextDrawSetSelectable(gText, true);
    TextDrawSetPreviewModel(gText, 411);
    TextDrawSetPreviewRot(gText, 10.0, 20.0, 30.0, 1.25);
    TextDrawSetPreviewVehCol(gText, 3, 5);
    gMenu = CreateMenu("Shop", 2, 200.0, 100.0, 150.0, 75.0);
    SetMenuColumnHeader(gMenu, 0, "Item");
    AddMenuItem(gMenu, 0, "Deagle");
    AddMenuItem(gMenu, 0, "Rifle");
    SetMenuColumnHeader(gMenu, 1, "Price");
    AddMenuItem(gMenu, 1, "$500");
    return 1;
}

public OnPlayerRequestClass(playerid, classid) { return 1; }
public OnPlayerRequestSpawn(playerid) { return 1; }

Batch1(playerid)
{
    SetPlayerName(playerid, "RPCRenamed");
    SetPlayerHealth(playerid, 88.5);
    SetPlayerArmour(playerid, 44.5);
    TogglePlayerControllable(playerid, true);
    PlayerPlaySound(playerid, 1052, 1.0, 2.0, 3.0);
    SetPlayerWorldBounds(playerid, 2000.0, -2000.0, 3000.0, -3000.0);
    GivePlayerMoney(playerid, 1234);
    SetPlayerFacingAngle(playerid, 123.5);
    ResetPlayerMoney(playerid);
    ResetPlayerWeapons(playerid);
    GivePlayerWeapon(playerid, WEAPON_DEAGLE, 50);
    SetPlayerAmmo(playerid, WEAPON_DEAGLE, 77);
    SetPlayerArmedWeapon(playerid, WEAPON_DEAGLE);
    SetPlayerTime(playerid, 7, 30);
    TogglePlayerClock(playerid, true);
    SetPlayerShopName(playerid, "FDPIZA");
    SetPlayerSkillLevel(playerid, WEAPONSKILL_PISTOL, 500);
    SetPlayerDrunkLevel(playerid, 2500);
    SetPlayerPosFindZ(playerid, 100.0, 200.0, 10.0);
    SetPlayerPos(playerid, 100.0, 200.0, 10.0);
}

Batch2(playerid)
{
    SetPlayerCheckpoint(playerid, 1.0, 2.0, 3.0, 4.5);
    DisablePlayerCheckpoint(playerid);
    SetPlayerRaceCheckpoint(playerid, CP_TYPE_GROUND_FINISH, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.5);
    DisablePlayerRaceCheckpoint(playerid);
    PlayAudioStreamForPlayer(playerid, "http://example.invalid/a.mp3", 1.0, 2.0, 3.0, 50.0, true);
    StopAudioStreamForPlayer(playerid);
    RemoveBuildingForPlayer(playerid, 1302, 1.0, 2.0, 3.0, 5.5);
    SetPlayerMapIcon(playerid, 3, 1.0, 2.0, 3.0, 31, 0xAABBCCDD, MAPICON_GLOBAL);
    RemovePlayerMapIcon(playerid, 3);
    SetPlayerWeather(playerid, 8);
    SetWorldTime(14);
    SetPlayerInterior(playerid, 3);
    SetPlayerInterior(playerid, 0);
    SetPlayerWantedLevel(playerid, 4);
    CreateExplosionForPlayer(playerid, 1.0, 2.0, 3.0, 6, 9.5);
    PlayCrimeReportForPlayer(playerid, playerid, 16);
    GameTextForPlayer(playerid, "~r~hello", 3000, 3);
    TogglePlayerWidescreen(playerid, true);
    TogglePlayerWidescreen(playerid, false);
    SetPlayerGravity(playerid, 0.0125);
    SetPlayerTeam(playerid, 5);
    SetPlayerColor(playerid, 0x11223344);
    SetPlayerSkin(playerid, 20);
    SetPlayerFightingStyle(playerid, FIGHT_STYLE_KNEEHEAD);
    SetPlayerVelocity(playerid, 0.1, 0.2, 0.3);
    SetPlayerSpecialAction(playerid, SPECIAL_ACTION_DANCE1);
    ApplyAnimation(playerid, "PED", "WALK_old", 4.25, true, false, true, true, 500, SYNC_NONE);
    ClearAnimations(playerid);
    SetPlayerChatBubble(playerid, "bubble", 0xFF00FF00, 30.5, 5000);
    SendDeathMessageToPlayer(playerid, INVALID_PLAYER_ID, playerid, 51);
    SendPlayerMessageToPlayer(playerid, playerid, "player chat");
    EnableStuntBonusForPlayer(playerid, false);
    SetPlayerCameraPos(playerid, 1.0, 2.0, 3.0);
    SetPlayerCameraLookAt(playerid, 4.0, 5.0, 6.0, CAMERA_MOVE);
    InterpolateCameraPos(playerid, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 1500, CAMERA_CUT);
    SetCameraBehindPlayer(playerid);
    for (new i = 0; i < 2; i++)
        if (i != playerid)
            ShowPlayerNameTagForPlayer(playerid, i, false);
    DisableRemoteVehicleCollisions(playerid, true);
    EnablePlayerCameraTarget(playerid, true);
    SetSpawnInfo(playerid, 3, 22, 100.0, 200.0, 10.0, 45.5, WEAPON_DEAGLE, 10, WEAPON_MP5, 20, WEAPON_M4, 30);
    SendClientCheck(playerid, 72, 0x400000, 2, 4);
}

Batch3(playerid)
{
    ShowPlayerDialog(playerid, 77, DIALOG_STYLE_LIST, "Dialog title", "/help - this very same dialog.\n/kill - suicide.\n{FF0000}Red $3000", "Buy", "Cancel");
    gPlayerText[playerid] = CreatePlayerTextDraw(playerid, 50.0, 60.0, "player text");
    PlayerTextDrawLetterSize(playerid, gPlayerText[playerid], 0.25, 0.75);
    PlayerTextDrawSetSelectable(playerid, gPlayerText[playerid], true);
    PlayerTextDrawShow(playerid, gPlayerText[playerid]);
    PlayerTextDrawSetString(playerid, gPlayerText[playerid], "changed");
    TextDrawShowForPlayer(playerid, gText);
    SelectTextDraw(playerid, 0xFF0000FF);
    GangZoneShowForPlayer(playerid, gZone, 0x11223344);
    GangZoneFlashForPlayer(playerid, gZone, 0x55667788);
    GangZoneStopFlashForPlayer(playerid, gZone);
    GangZoneHideForPlayer(playerid, gZone);
    ShowMenuForPlayer(gMenu, playerid);
    new PlayerText3D:l = CreatePlayer3DTextLabel(playerid, "Player label", 0x55667788, 1.0, 2.0, 3.0, 25.0);
    DeletePlayer3DTextLabel(playerid, l);
}

Batch4(playerid)
{
    new o = CreatePlayerObject(playerid, 1339, 105.0, 206.0, 10.0, 4.0, 5.0, 6.0, 200.0);
    gPlayerObject[playerid] = o;
    SetPlayerObjectPos(playerid, o, 106.0, 207.0, 11.0);
    SetPlayerObjectRot(playerid, o, 7.0, 8.0, 9.0);
    SetPlayerObjectMaterial(playerid, o, 3, 18646, "matcolours", "white", 0x80112233);
    SetPlayerObjectMaterialText(playerid, o, "Sign text", 4, OBJECT_MATERIAL_SIZE_128x128, "Courier", 18, false, 0xFF112233, 0xFF445566, OBJECT_MATERIAL_TEXT_ALIGN_RIGHT);
    MovePlayerObject(playerid, o, 110.0, 210.0, 12.0, 2.5, 1.0, 2.0, 3.0);
    StopPlayerObject(playerid, o);
    AttachPlayerObjectToPlayer(playerid, o, playerid, 0.5, 0.25, 0.125, 1.0, 2.0, 3.0);
    AttachCameraToPlayerObject(playerid, o);
    SetCameraBehindPlayer(playerid);
    SetPlayerAttachedObject(playerid, 2, 1242, 2, 0.1, 0.2, 0.3, 10.0, 20.0, 30.0, 1.5, 2.5, 3.5, 0x11223344, 0x55667788);
    EditPlayerObject(playerid, o);
    EditAttachedObject(playerid, 2);
    SelectObject(playerid);
    CancelEdit(playerid);
    gPlayerPickup[playerid] = CreatePlayerPickup(playerid, 1274, 2, 108.0, 209.0, 10.0);
}

Batch5(playerid)
{
    LinkVehicleToInterior(gCar, 2);
    LinkVehicleToInterior(gCar, 0);
    SetVehicleHealth(gCar, 750.5);
    SetVehiclePos(gCar, 106.0, 201.0, 10.5);
    SetVehicleZAngle(gCar, 77.5);
    SetVehicleNumberPlate(gCar, "PS2SAMP");
    SetVehicleParamsEx(gCar, 1, 0, 1, 0, 1, 0, 1);
    UpdateVehicleDamageStatus(gCar, VEHICLE_PANEL_STATUS:0x11, VEHICLE_DOOR_STATUS:0x22, VEHICLE_LIGHT_STATUS:0x3, VEHICLE_TYRE_STATUS:0x4);
    AddVehicleComponent(gCar, 1010);
    RemoveVehicleComponent(gCar, 1010);
    ChangeVehicleColor(gCar, 3, 4);
    ChangeVehiclePaintjob(gCar, 1);
}

Batch6(playerid)
{
    ApplyActorAnimation(gActor, "PED", "WALK_old", 4.25, true, false, true, true, 500);
    ClearActorAnimations(gActor);
    SetActorFacingAngle(gActor, 66.5);
    SetActorPos(gActor, 102.5, 205.5, 10.5);
    SetActorHealth(gActor, 55.5);
}

Batch7(playerid)
{
    TogglePlayerSpectating(playerid, true);
    PlayerSpectateVehicle(playerid, gCar, SPECTATE_MODE_FIXED);
}

Batch8(playerid)
{
    TogglePlayerSpectating(playerid, false);
    RemovePlayerAttachedObject(playerid, 2);
    DestroyPlayerObject(playerid, gPlayerObject[playerid]);
    DestroyPlayerPickup(playerid, gPlayerPickup[playerid]);
    DestroyPickup(gPickup);
    HideMenuForPlayer(gMenu, playerid);
    PlayerTextDrawHide(playerid, gPlayerText[playerid]);
    CancelSelectTextDraw(playerid);
    Delete3DTextLabel(gLabel);
    DestroyActor(gActor);
    DestroyVehicle(gTrailer);
    ForceClassSelection(playerid);
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/t1")) { Batch1(playerid); return 1; }
    if (!strcmp(cmdtext, "/t2")) { Batch2(playerid); return 1; }
    if (!strcmp(cmdtext, "/t3")) { Batch3(playerid); return 1; }
    if (!strcmp(cmdtext, "/t4")) { Batch4(playerid); return 1; }
    if (!strcmp(cmdtext, "/t5")) { Batch5(playerid); return 1; }
    if (!strcmp(cmdtext, "/t6")) { Batch6(playerid); return 1; }
    if (!strcmp(cmdtext, "/t7")) { Batch7(playerid); return 1; }
    if (!strcmp(cmdtext, "/t8")) { Batch8(playerid); return 1; }
    if (!strcmp(cmdtext, "/t9")) { GameModeExit(); return 1; }
    if (!strcmp(cmdtext, "/menu")) { ShowMenuForPlayer(gMenu, playerid); return 1; }
    if (!strcmp(cmdtext, "/selectobj")) { SelectObject(playerid); return 1; }
    if (!strcmp(cmdtext, "/editobj")) { EditPlayerObject(playerid, gPlayerObject[playerid]); return 1; }
    if (!strcmp(cmdtext, "/editatt")) { EditAttachedObject(playerid, 2); return 1; }
    if (!strcmp(cmdtext, "/specplayer"))
    {
        for (new i = 0; i < 2; i++)
            if (i != playerid)
                PlayerSpectatePlayer(playerid, i, SPECTATE_MODE_SIDE);
        return 1;
    }
    if (!strcmp(cmdtext, "/putin")) { PutPlayerInVehicle(playerid, gCar, 0); return 1; }
    if (!strcmp(cmdtext, "/vel")) { SetVehicleVelocity(gCar, 0.5, 0.25, 0.125); return 1; }
    if (!strcmp(cmdtext, "/trailer")) { AttachTrailerToVehicle(gTrailer, gCar); return 1; }
    if (!strcmp(cmdtext, "/detach")) { DetachTrailerFromVehicle(gCar); return 1; }
    if (!strcmp(cmdtext, "/out")) { RemovePlayerFromVehicle(playerid); return 1; }
    if (!strcmp(cmdtext, "/name")) { new n[32]; GetPlayerName(playerid, n, sizeof n); Echo(playerid, "CB name %s", n); return 1; }
    if (!strcmp(cmdtext, "/money")) { Echo(playerid, "CB money %d drunk %d", GetPlayerMoney(playerid), GetPlayerDrunkLevel(playerid)); return 1; }
    if (!strcmp(cmdtext, "/wep"))
    {
        new WEAPON:w, a;
        GetPlayerWeaponData(playerid, WEAPON_SLOT:2, w, a);
        Echo(playerid, "CB weapon slot2 %d %d", _:w, a);
        return 1;
    }
    if (!strcmp(cmdtext, "/target")) { Echo(playerid, "CB target vehicle %d", GetPlayerCameraTargetVehicle(playerid)); return 1; }
    if (!strcmp(cmdtext, "/where"))
    {
        new Float:x, Float:y, Float:z;
        GetPlayerPos(playerid, x, y, z);
        Echo(playerid, "CB pos %.1f %.1f %.1f state %d", x, y, z, _:GetPlayerState(playerid));
        return 1;
    }
    Echo(playerid, "CB unknown %s", cmdtext);
    return 1;
}

public OnPlayerText(playerid, text[]) { Echo(playerid, "CB text %s", text); return 0; }
public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[]) { Echo(playerid, "CB dialog %d %d %d %s", dialogid, response, listitem, inputtext); return 1; }
public OnPlayerClickMap(playerid, Float:fX, Float:fY, Float:fZ) { Echo(playerid, "CB clickmap %.1f %.1f %.1f", fX, fY, fZ); return 1; }
public OnPlayerClickPlayer(playerid, clickedplayerid, CLICK_SOURCE:source) { Echo(playerid, "CB clickplayer %d %d", clickedplayerid, _:source); return 1; }
public OnPlayerClickTextDraw(playerid, Text:clickedid) { Echo(playerid, "CB clicktd %d", _:clickedid); return 1; }
public OnPlayerClickPlayerTextDraw(playerid, PlayerText:playertextid) { Echo(playerid, "CB clickptd %d", _:playertextid); return 1; }
public OnPlayerEnterVehicle(playerid, vehicleid, ispassenger) { Echo(playerid, "CB enter %d %d", vehicleid, ispassenger); return 1; }
public OnPlayerExitVehicle(playerid, vehicleid) { Echo(playerid, "CB exit %d", vehicleid); return 1; }
public OnPlayerStateChange(playerid, PLAYER_STATE:newstate, PLAYER_STATE:oldstate) { Echo(playerid, "CB state %d %d", _:newstate, _:oldstate); return 1; }
public OnPlayerDeath(playerid, killerid, WEAPON:reason) { Echo(playerid, "CB death %d %d", killerid, _:reason); return 1; }
public OnPlayerTakeDamage(playerid, issuerid, Float:amount, WEAPON:weaponid, bodypart) { Echo(playerid, "CB take %d %.1f %d %d", issuerid, amount, _:weaponid, bodypart); return 1; }
public OnPlayerGiveDamage(playerid, damagedid, Float:amount, WEAPON:weaponid, bodypart) { Echo(playerid, "CB give %d %.1f %d %d", damagedid, amount, _:weaponid, bodypart); return 1; }
public OnPlayerGiveDamageActor(playerid, damaged_actorid, Float:amount, WEAPON:weaponid, bodypart) { Echo(playerid, "CB actordmg %d %.1f %d %d", damaged_actorid, amount, _:weaponid, bodypart); return 1; }
public OnPlayerInteriorChange(playerid, newinteriorid, oldinteriorid) { Echo(playerid, "CB interior %d %d", newinteriorid, oldinteriorid); return 1; }
public OnPlayerPickUpPickup(playerid, pickupid) { Echo(playerid, "CB pickup %d", pickupid); return 1; }
public OnPlayerPickUpPlayerPickup(playerid, pickupid) { Echo(playerid, "CB ppickup %d", pickupid); return 1; }
public OnPlayerSelectedMenuRow(playerid, row) { Echo(playerid, "CB menurow %d", row); return 1; }
public OnPlayerExitedMenu(playerid) { Echo(playerid, "CB menuquit"); return 1; }
public OnVehicleDeath(vehicleid, killerid) { Echo(killerid, "CB vehicledeath %d", vehicleid); SendClientMessageToAll(0xCBCBCBFF, "CB vehicledeath all"); return 1; }
public OnVehicleDamageStatusUpdate(vehicleid, playerid) { new VEHICLE_PANEL_STATUS:p, VEHICLE_DOOR_STATUS:d, VEHICLE_LIGHT_STATUS:l, VEHICLE_TYRE_STATUS:t; GetVehicleDamageStatus(vehicleid, p, d, l, t); Echo(playerid, "CB vdamage %d %d %d %d %d", vehicleid, _:p, _:d, _:l, _:t); return 1; }
public OnVehicleRespray(playerid, vehicleid, color1, color2) { Echo(playerid, "CB respray %d %d %d", vehicleid, color1, color2); return 1; }
public OnVehicleMod(playerid, vehicleid, componentid) { Echo(playerid, "CB mod %d %d", vehicleid, componentid); return 1; }
public OnVehiclePaintjob(playerid, vehicleid, paintjobid) { Echo(playerid, "CB paintjob %d %d", vehicleid, paintjobid); return 1; }
public OnPlayerSelectObject(playerid, SELECT_OBJECT:type, objectid, modelid, Float:fX, Float:fY, Float:fZ) { Echo(playerid, "CB selectobj %d %d %d %.1f %.1f %.1f", _:type, objectid, modelid, fX, fY, fZ); return 1; }
public OnPlayerEditObject(playerid, playerobject, objectid, EDIT_RESPONSE:response, Float:fX, Float:fY, Float:fZ, Float:fRotX, Float:fRotY, Float:fRotZ) { Echo(playerid, "CB editobj %d %d %d %.1f %.1f %.1f %.1f %.1f %.1f", playerobject, objectid, _:response, fX, fY, fZ, fRotX, fRotY, fRotZ); return 1; }
public OnPlayerEditAttachedObject(playerid, EDIT_RESPONSE:response, index, modelid, boneid, Float:fOffsetX, Float:fOffsetY, Float:fOffsetZ, Float:fRotX, Float:fRotY, Float:fRotZ, Float:fScaleX, Float:fScaleY, Float:fScaleZ) { Echo(playerid, "CB editatt %d %d %d %d %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f", _:response, index, modelid, boneid, fOffsetX, fOffsetY, fOffsetZ, fRotX, fRotY, fRotZ, fScaleX, fScaleY, fScaleZ); return 1; }
public OnPlayerWeaponShot(playerid, WEAPON:weaponid, BULLET_HIT_TYPE:hittype, hitid, Float:fX, Float:fY, Float:fZ) { Echo(playerid, "CB shot %d %d %d %.1f %.1f %.1f", _:weaponid, _:hittype, hitid, fX, fY, fZ); return 1; }
public OnUnoccupiedVehicleUpdate(vehicleid, playerid, passenger_seat, Float:new_x, Float:new_y, Float:new_z, Float:vel_x, Float:vel_y, Float:vel_z) { Echo(playerid, "CB unocc %d %d %.1f %.1f %.1f", vehicleid, passenger_seat, new_x, new_y, new_z); return 1; }
public OnTrailerUpdate(playerid, vehicleid) { Echo(playerid, "CB trailer %d", vehicleid); return 1; }
public OnClientCheckResponse(playerid, actionid, memaddr, retndata) { Echo(playerid, "CB check %d %d %d", actionid, memaddr, retndata); return 1; }
