// Test gamemode for feat_vehicle.c, run on a private server only (see live_run.sh).
// /v<cmd> does one thing to the world and every callback is echoed as a "CB ..." message,
// so the client log shows both sides. live_check.py compares the two.
#define SAMP_COMPAT
#include <open.mp>

#define Echo(%0,%1) print(%1), SendClientMessage(%0, 0xCBCBCBFF, %1)
// The flat apron of the LS airport runway, so the same mode works in the real game.
#define SPAWN_X 1900.0
#define SPAWN_Y -2490.0
#define SPAWN_Z 13.6

new gCar, gTruck, gTrailer, gCop, gBike, gEmpty;
new gNextReport;
new gMany[40], gManyCount;

main() {}

public OnGameModeInit()
{
    AddPlayerClass(7, SPAWN_X, SPAWN_Y, SPAWN_Z, 90.0, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    gCar = CreateVehicle(411, SPAWN_X + 5.0, SPAWN_Y, SPAWN_Z, 90.0, 6, 7, -1);
    gTruck = CreateVehicle(403, SPAWN_X + 15.0, SPAWN_Y, SPAWN_Z, 0.0, 1, 1, -1);
    gTrailer = CreateVehicle(435, SPAWN_X + 25.0, SPAWN_Y, SPAWN_Z, 0.0, 1, 1, -1);
    gCop = CreateVehicle(596, SPAWN_X + 35.0, SPAWN_Y, SPAWN_Z, 180.0, 0, 1, -1, true);
    gBike = CreateVehicle(522, SPAWN_X + 45.0, SPAWN_Y, SPAWN_Z, 270.0, 3, 3, -1);
    gEmpty = CreateVehicle(560, SPAWN_X - 5.0, SPAWN_Y, SPAWN_Z, 30.0, 2, 2, -1);
    SetVehicleNumberPlate(gCar, "PS2 CAR");
    AddVehicleComponent(gEmpty, 1010);
    AddVehicleComponent(gEmpty, 1073);
    ChangeVehiclePaintjob(gEmpty, 1);
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    return 1;
}

public OnPlayerSpawn(playerid)
{
    new s[96];
    format(s, sizeof s, "CB ids car %d truck %d trailer %d cop %d bike %d empty %d", gCar, gTruck, gTrailer, gCop, gBike, gEmpty);
    Echo(playerid, s);
    return 1;
}

public OnPlayerEnterVehicle(playerid, vehicleid, ispassenger)
{
    new s[64];
    format(s, sizeof s, "CB enter %d %d", vehicleid, ispassenger);
    Echo(playerid, s);
    return 1;
}

public OnPlayerExitVehicle(playerid, vehicleid)
{
    new s[64];
    format(s, sizeof s, "CB exit %d", vehicleid);
    Echo(playerid, s);
    return 1;
}

public OnPlayerStateChange(playerid, PLAYER_STATE:newstate, PLAYER_STATE:oldstate)
{
    new s[64];
    format(s, sizeof s, "CB state %d from %d seat %d car %d", _:newstate, _:oldstate, GetPlayerVehicleSeat(playerid), GetPlayerVehicleID(playerid));
    Echo(playerid, s);
    return 1;
}

public OnVehicleDamageStatusUpdate(vehicleid, playerid)
{
    new VEHICLE_PANEL_STATUS:panels, VEHICLE_DOOR_STATUS:doors, VEHICLE_LIGHT_STATUS:lights, VEHICLE_TIRE_STATUS:tires, s[80];
    GetVehicleDamageStatus(vehicleid, panels, doors, lights, tires);
    format(s, sizeof s, "CB damage %d %x %x %x %x", vehicleid, _:panels, _:doors, _:lights, _:tires);
    Echo(playerid, s);
    return 1;
}

public OnVehicleDeath(vehicleid, killerid)
{
    new s[64];
    format(s, sizeof s, "CB death %d by %d", vehicleid, killerid);
    Echo(killerid, s);
    return 1;
}

public OnUnoccupiedVehicleUpdate(vehicleid, playerid, passenger_seat, Float:new_x, Float:new_y, Float:new_z, Float:vel_x, Float:vel_y, Float:vel_z)
{
    static last;
    new s[96], t = GetTickCount();
    if (t - last < 500) return 1;
    last = t;
    format(s, sizeof s, "CB unoccupied %d seat %d at %.2f %.2f %.2f", vehicleid, passenger_seat, new_x, new_y, new_z);
    Echo(playerid, s);
    return 1;
}

public OnTrailerUpdate(playerid, vehicleid)
{
    static last;
    new s[64], t = GetTickCount();
    if (t - last < 500) return 1;
    last = t;
    format(s, sizeof s, "CB trailer %d", vehicleid);
    Echo(playerid, s);
    return 1;
}

public OnPlayerUpdate(playerid)
{
    new v = GetPlayerVehicleID(playerid), t = GetTickCount();
    if (v && t > gNextReport) {
        new Float:x, Float:y, Float:z, Float:a, Float:h, Float:qw, Float:qx, Float:qy, Float:qz, s[144];
        gNextReport = t + 700;
        GetVehiclePos(v, x, y, z);
        GetVehicleZAngle(v, a);
        GetVehicleHealth(v, h);
        GetVehicleRotationQuat(v, qw, qx, qy, qz);
        format(s, sizeof s, "CB update car %d at %.2f %.2f %.2f angle %.2f health %.0f quat %.3f %.3f %.3f %.3f trailer %d",
               v, x, y, z, a, h, qw, qx, qy, qz, GetVehicleTrailer(v));
        Echo(playerid, s);
    }
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    new s[96];
    if (!strcmp(cmdtext, "/vvel")) {
        SetVehicleVelocity(GetPlayerVehicleID(playerid), 0.1, 0.2, 0.3);
    } else if (!strcmp(cmdtext, "/vspin")) {
        SetVehicleAngularVelocity(GetPlayerVehicleID(playerid), 0.01, 0.02, 0.03);
    } else if (!strcmp(cmdtext, "/vpos")) {
        SetVehiclePos(GetPlayerVehicleID(playerid), SPAWN_X + 2.0, SPAWN_Y + 50.0, SPAWN_Z);
    } else if (!strcmp(cmdtext, "/vangle")) {
        SetVehicleZAngle(gEmpty, 123.0);
    } else if (!strcmp(cmdtext, "/vhealth")) {
        SetVehicleHealth(GetPlayerVehicleID(playerid), 500.0);
    } else if (!strcmp(cmdtext, "/vrepair")) {
        RepairVehicle(GetPlayerVehicleID(playerid));
    } else if (!strcmp(cmdtext, "/vdamage")) {
        UpdateVehicleDamageStatus(gEmpty, VEHICLE_PANEL_STATUS:0x31, VEHICLE_DOOR_STATUS:0x04000302, VEHICLE_LIGHT_STATUS:0x05, VEHICLE_TIRE_STATUS:0x6);
    } else if (!strcmp(cmdtext, "/vparams")) {
        SetVehicleParamsEx(gEmpty, VEHICLE_PARAMS_ON, VEHICLE_PARAMS_OFF, VEHICLE_PARAMS_ON, VEHICLE_PARAMS_ON, VEHICLE_PARAMS_ON, VEHICLE_PARAMS_OFF, VEHICLE_PARAMS_ON);
        SetVehicleParamsCarDoors(gEmpty, 1, 0, 1, 0);
    } else if (!strcmp(cmdtext, "/vcolour")) {
        ChangeVehicleColor(gEmpty, 44, 55);
    } else if (!strcmp(cmdtext, "/vmod")) {
        AddVehicleComponent(gEmpty, 1087);
        RemoveVehicleComponent(gEmpty, 1010);
    } else if (!strcmp(cmdtext, "/vinterior")) {
        LinkVehicleToInterior(gEmpty, 3);
    } else if (!strcmp(cmdtext, "/vputtruck")) {
        PutPlayerInVehicle(playerid, gTruck, 0);
    } else if (!strcmp(cmdtext, "/vpassenger")) {
        PutPlayerInVehicle(playerid, gTruck, 1);
    } else if (!strcmp(cmdtext, "/vattach")) {
        AttachTrailerToVehicle(gTrailer, gTruck);
    } else if (!strcmp(cmdtext, "/vdetach")) {
        DetachTrailerFromVehicle(gTruck);
    } else if (!strcmp(cmdtext, "/vremove")) {
        RemovePlayerFromVehicle(playerid);
    } else if (!strcmp(cmdtext, "/vfar")) {
        SetPlayerPos(playerid, SPAWN_X + 1500.0, SPAWN_Y, SPAWN_Z);
    } else if (!strcmp(cmdtext, "/vback")) {
        SetPlayerPos(playerid, SPAWN_X, SPAWN_Y, SPAWN_Z);
    } else if (!strcmp(cmdtext, "/vcar")) {
        PutPlayerInVehicle(playerid, gCar, 0);
    } else if (!strcmp(cmdtext, "/vbike")) {
        PutPlayerInVehicle(playerid, gBike, 0);
    } else if (!strcmp(cmdtext, "/vcop")) {
        PutPlayerInVehicle(playerid, gCop, 0);
    } else if (!strcmp(cmdtext, "/vlock")) {
        SetVehicleParamsEx(gCar, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_ON, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET);
    } else if (!strcmp(cmdtext, "/vunlock")) {
        SetVehicleParamsEx(gCar, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_OFF, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET);
    } else if (!strcmp(cmdtext, "/vengineoff")) {
        SetVehicleParamsEx(GetPlayerVehicleID(playerid), VEHICLE_PARAMS_OFF, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET);
    } else if (!strcmp(cmdtext, "/vsiren")) {
        SetVehicleParamsEx(gCop, VEHICLE_PARAMS_ON, VEHICLE_PARAMS_ON, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET, VEHICLE_PARAMS_UNSET);
    } else if (!strcmp(cmdtext, "/vrespawn")) {
        SetVehicleToRespawn(gCar);
    } else if (!strcmp(cmdtext, "/vmany")) {
        // 40 cars of 20 models: more than the client will keep in the game
        for (new i = 0; i < 40; i++) {
            gMany[i] = CreateVehicle(400 + (i % 20), SPAWN_X + 60.0 + (i % 10) * 6.0, SPAWN_Y + 20.0 + (i / 10) * 8.0, SPAWN_Z, 0.0, 1, 1, -1);
        }
        gManyCount = 40;
    } else if (!strcmp(cmdtext, "/vclear")) {
        for (new i = 0; i < gManyCount; i++) {
            DestroyVehicle(gMany[i]);
        }
        gManyCount = 0;
    } else if (!strcmp(cmdtext, "/vfaceget")) {
        new Float:a;
        GetPlayerFacingAngle(playerid, a);
        format(s, sizeof s, "CB facing %.2f", a);
        Echo(playerid, s);
    } else if (!strcmp(cmdtext, "/vangleget")) {
        new Float:a;
        GetVehicleZAngle(gCar, a);
        format(s, sizeof s, "CB zangle %.2f", a);
        Echo(playerid, s);
    } else {
        return 0;
    }
    format(s, sizeof s, "CB did %s", cmdtext);
    Echo(playerid, s);
    return 1;
}
