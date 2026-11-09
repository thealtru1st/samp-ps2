// Test gamemode for payload/feat_dialog*.c. Private server only. It behaves like a
// public login server: OnPlayerRequestSpawn refuses until the password dialog has been
// answered. /s1 and up make the server send one kind of thing each, and every callback
// is echoed as a "CB ..." chat line, so the real server's parsers judge our answers.
// wip/dialog/live_dialog.c drives it on the host; ingame-tests.md drives it by hand.
#define SAMP_COMPAT
#include <open.mp>

#define Echo(%0,%1) printf(%1), SendClientMessage(%0, 0xCBCBCBFF, %1)
#define PASSWORD "hunter2"

new bool:gLogged[MAX_PLAYERS];
new Text:gGlobal, Text:gBox, Text:gSprite, Text:gClickA, Text:gClickB;
new PlayerText:gClickP[MAX_PLAYERS];
new Menu:gMenu;

main() {}

ShowLogin(playerid, const message[])
{
    new body[160];
    format(body, sizeof body, "{FFFFFF}Welcome to {FFCC00}Dialog Test{FFFFFF}\n%s\nEnter your password:", message);
    ShowPlayerDialog(playerid, 1, DIALOG_STYLE_PASSWORD, "{FFCC00}Login", body, "Login", "Quit");
}

public OnGameModeInit()
{
    AddPlayerClass(7, 2495.0, -1687.0, 13.5, 0.0, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    gGlobal = TextDrawCreate(320.0, 20.0, "Global ~r~red ~g~green~n~~b~~h~line two");
    TextDrawLetterSize(gGlobal, 0.4, 1.4);
    TextDrawAlignment(gGlobal, TEXT_DRAW_ALIGN_CENTER);
    TextDrawTextSize(gGlobal, 100.0, 400.0);
    TextDrawFont(gGlobal, TEXT_DRAW_FONT_1);
    TextDrawSetShadow(gGlobal, 1);

    gBox = TextDrawCreate(20.0, 120.0, "Boxed text");
    TextDrawLetterSize(gBox, 0.3, 1.0);
    TextDrawTextSize(gBox, 160.0, 20.0);
    TextDrawUseBox(gBox, true);
    TextDrawBoxColor(gBox, 0x00008080);
    TextDrawFont(gBox, TEXT_DRAW_FONT_2);

    gSprite = TextDrawCreate(20.0, 160.0, "LD_SPAC:white");
    TextDrawFont(gSprite, TEXT_DRAW_FONT_SPRITE_DRAW);
    TextDrawTextSize(gSprite, 80.0, 30.0);
    TextDrawColor(gSprite, 0xFF8000FF);

    gClickA = TextDrawCreate(320.0, 200.0, "Click A");
    TextDrawLetterSize(gClickA, 0.5, 1.6);
    TextDrawAlignment(gClickA, TEXT_DRAW_ALIGN_CENTER);
    TextDrawTextSize(gClickA, 20.0, 120.0);
    TextDrawSetSelectable(gClickA, true);

    gClickB = TextDrawCreate(320.0, 260.0, "Click B");
    TextDrawLetterSize(gClickB, 0.5, 1.6);
    TextDrawAlignment(gClickB, TEXT_DRAW_ALIGN_CENTER);
    TextDrawTextSize(gClickB, 20.0, 120.0);
    TextDrawSetSelectable(gClickB, true);

    gMenu = CreateMenu("Weapon shop", 2, 200.0, 120.0, 150.0, 80.0);
    SetMenuColumnHeader(gMenu, 0, "Item");
    SetMenuColumnHeader(gMenu, 1, "Cost");
    AddMenuItem(gMenu, 0, "Pistol");
    AddMenuItem(gMenu, 0, "Locked row");
    AddMenuItem(gMenu, 0, "Shotgun");
    AddMenuItem(gMenu, 1, "$100");
    AddMenuItem(gMenu, 1, "$0");
    AddMenuItem(gMenu, 1, "$300");
    DisableMenuRow(gMenu, 1);
    return 1;
}

public OnPlayerConnect(playerid)
{
    gLogged[playerid] = false;
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    if (!gLogged[playerid])
        ShowLogin(playerid, "Please log in.");
    return 1;
}

public OnPlayerRequestSpawn(playerid)
{
    Echo(playerid, "CB reqspawn %d", _:gLogged[playerid]);
    return gLogged[playerid];
}

public OnPlayerSpawn(playerid)
{
    Echo(playerid, "CB spawned");
    return 1;
}

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    Echo(playerid, "CB dialog %d %d %d [%s]", dialogid, response, listitem, inputtext);
    if (dialogid == 1) {
        if (response && !strcmp(inputtext, PASSWORD)) {
            gLogged[playerid] = true;
            Echo(playerid, "CB logged in");
        } else {
            ShowLogin(playerid, "{FF3030}Wrong password.");
        }
    }
    return 1;
}

public OnPlayerClickTextDraw(playerid, Text:clickedid)
{
    Echo(playerid, "CB click %d", _:clickedid == _:gClickA ? 1 : _:clickedid == _:gClickB ? 2 : _:clickedid == 65535 ? 65535 : -1);
    return 1;
}

public OnPlayerClickPlayerTextDraw(playerid, PlayerText:playertextid)
{
    Echo(playerid, "CB pclick %d", _:playertextid == _:gClickP[playerid] ? 1 : -1);
    return 1;
}

public OnPlayerSelectedMenuRow(playerid, row)
{
    Echo(playerid, "CB menu row %d", row);
    return 1;
}

public OnPlayerExitedMenu(playerid)
{
    Echo(playerid, "CB menu exit");
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    new body[1024];

    if (!strcmp(cmdtext, "/s1")) {
        ShowPlayerDialog(playerid, 10, DIALOG_STYLE_LIST, "Teleports", "Grove\n{FF0000}Red row\nAirport\nPier", "Go", "Back");
    } else if (!strcmp(cmdtext, "/s2")) {
        ShowPlayerDialog(playerid, 11, DIALOG_STYLE_TABLIST, "Shop", "Deagle\t$5000\t100\nSawnoff\t$5000\t100\nPistol\t$1000\t50", "Buy", "");
    } else if (!strcmp(cmdtext, "/s3")) {
        ShowPlayerDialog(playerid, 12, DIALOG_STYLE_TABLIST_HEADERS, "Stats", "Name\tScore\tPing\nAlice\t10\t30\n{00FF00}Bob\t20\t40\n", "Pick", "Close");
    } else if (!strcmp(cmdtext, "/s4")) {
        ShowPlayerDialog(playerid, 13, DIALOG_STYLE_MSGBOX, "{00FFFF}Rules", "{FFFFFF}1. Be {FF0000}nice{FFFFFF}.\n2. A very long line that must wrap inside the panel because no server keeps its rule text short when it could write a whole paragraph about it instead.\n\tTabbed", "Accept", "");
    } else if (!strcmp(cmdtext, "/s5")) {
        ShowPlayerDialog(playerid, 14, DIALOG_STYLE_INPUT, "Name", "Pick a name:", "Save", "Back");
    } else if (!strcmp(cmdtext, "/s6")) {
        ShowPlayerDialog(playerid, 15, DIALOG_STYLE_MSGBOX, "Hide me", "This goes away by itself.", "OK", "");
        SetTimerEx("HideIt", 1500, false, "i", playerid);
    } else if (!strcmp(cmdtext, "/s7")) {
        TextDrawSetString(gGlobal, "Global ~r~red ~g~green~n~~b~~h~line two"); // /s8 outlives a run
        TextDrawShowForPlayer(playerid, gGlobal);
        TextDrawShowForPlayer(playerid, gBox);
        TextDrawShowForPlayer(playerid, gSprite);
    } else if (!strcmp(cmdtext, "/s8")) {
        TextDrawSetString(gGlobal, "Changed ~y~string");
    } else if (!strcmp(cmdtext, "/s9")) {
        TextDrawHideForPlayer(playerid, gGlobal);
        TextDrawHideForPlayer(playerid, gBox);
        TextDrawHideForPlayer(playerid, gSprite);
    } else if (!strcmp(cmdtext, "/s10")) {
        gClickP[playerid] = CreatePlayerTextDraw(playerid, 320.0, 320.0, "Click P");
        PlayerTextDrawLetterSize(playerid, gClickP[playerid], 0.5, 1.6);
        PlayerTextDrawAlignment(playerid, gClickP[playerid], TEXT_DRAW_ALIGN_CENTER);
        PlayerTextDrawTextSize(playerid, gClickP[playerid], 20.0, 120.0);
        PlayerTextDrawSetSelectable(playerid, gClickP[playerid], true);
        PlayerTextDrawShow(playerid, gClickP[playerid]);
        TextDrawShowForPlayer(playerid, gClickA);
        TextDrawShowForPlayer(playerid, gClickB);
        SelectTextDraw(playerid, 0xFFFF00FF);
    } else if (!strcmp(cmdtext, "/s11")) {
        CancelSelectTextDraw(playerid);
    } else if (!strcmp(cmdtext, "/s12")) {
        TextDrawHideForPlayer(playerid, gClickA);
        TextDrawHideForPlayer(playerid, gClickB);
        PlayerTextDrawDestroy(playerid, gClickP[playerid]);
    } else if (!strcmp(cmdtext, "/s13")) {
        ShowMenuForPlayer(gMenu, playerid);
    } else if (!strcmp(cmdtext, "/s14")) {
        GameTextForPlayer(playerid, "~r~Style ~w~three~n~second line", 3000, 3);
    } else if (!strcmp(cmdtext, "/s15")) {
        for (new s = 0; s <= 6; s++) {
            format(body, sizeof body, "style %d", s);
            GameTextForPlayer(playerid, body, 6000, s);
        }
    } else if (!strcmp(cmdtext, "/s16")) {
        SetPlayerName(playerid, "Renamed_Tester");
        SetPlayerColor(playerid, 0x20B2AAFF);
        SetPlayerScore(playerid, 1234);
    } else if (!strcmp(cmdtext, "/s17")) {
        for (new i = 0; i < MAX_PLAYERS; i++)
            if (IsPlayerConnected(i) && i != playerid) {
                SetPlayerChatBubble(i, "Hello from a bubble", 0xFF00FFFF, 50.0, 8000);
                SetPlayerColor(i, 0xFF0000FF);
            }
    } else if (!strcmp(cmdtext, "/s18")) {
        // Long text with colour tokens: what would smash the game's stack if it got there.
        format(body, sizeof body, "~r~");
        for (new i = 0; i < 90; i++)
            strcat(body, "long word ", sizeof body);
        new Text:td = TextDrawCreate(20.0, 30.0, body);
        TextDrawLetterSize(td, 0.3, 1.0);
        TextDrawTextSize(td, 620.0, 20.0);
        TextDrawShowForPlayer(playerid, td);
    } else {
        return 0;
    }
    return 1;
}

forward HideIt(playerid);
public HideIt(playerid)
{
    HidePlayerDialog(playerid);
    Echo(playerid, "CB hidden");
}
