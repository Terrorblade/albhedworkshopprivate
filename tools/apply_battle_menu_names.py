"""
Apply the battle menu names from reversing/BATTLE_MENU.md to the FFX.exe IDB.

The battle command menu is a page stack (g_ffxBtlMenuPages, 8 pages of 240 bytes) driven by
FFX_BtlMenu_Step once per sub-step. Each page has a kind (page +0x06) and five callbacks that
FFX_BtlMenu_InstallPageCallbacks picks by kind: draw +0x90, enter +0x94, open +0x98, step +0x9C and
exit +0xA0. The rows below name those callbacks per kind (Root, List, Summon, Equip, Switch, LeftWin,
RightWin, Mix, Gil, Target), the target page builders, the page pop, the per battle init and
shutdown, the help and CTB window helpers, the two target preview hooks in the battle core, and the
menu globals. BATTLE_MENU.md has the state table and the evidence for each row.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all. A name starting maybe_ is a guess
and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_battle_menu_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
COMMENTS = False
VERBOSE = False

# ---------------------------------------------------------------------------------------------
# Functions and code labels whose current name is a placeholder
# ---------------------------------------------------------------------------------------------

NAMES = {
    # kind 0, the root command window. Callbacks installed by FFX_BtlMenu_InstallPageCallbacks
    0x8A85A0: ("FFX_BtlMenu_Root_OpenFirst", "root page +0x98 open callback on the first open of a turn (installed when FFX_Btl_GetUsableCommandKinds > 0 and g_ffxBtlMenuOpenSoundPending is 1). StopUnprotected(2), starts blimitlay when the usable kinds are exactly 2 (overdrive only), then swaps +0x98 to FFX_BtlMenu_Root_Open and returns 1. BATTLE_MENU.md 3.4"),
    0x8A8640: ("FFX_BtlMenu_Root_Open", "root page +0x98 open callback. StopUnprotected(2), then by FFX_Btl_GetUsableCommandKinds: 0 starts bnormallay, 2 starts bnormalllay, 1 and 3 start nothing (jump table 0x8A8694). Returns 1, so state 3 lasts one step"),
    0x8A8A70: ("FFX_BtlMenu_Root_Step", "root page +0x9C step callback. Plays SE 2 once per open (g_ffxBtlMenuOpenSoundPending), then calls FFX_BtlMenu_PageProc_Root only when page +0xE0 elapsed >= 0.45 s (dbl_B43D50). Wall clock input lockout"),
    0x8A2A60: ("FFX_BtlMenu_Root_Exit", "root page +0xA0 exit callback, StopPlayingUnprotected(2), returns 1"),
    0x8A49B0: ("FFX_BtlMenu_Root_Draw", "root page +0x90 draw callback, the command window. Greys rows through FFX_BtlMenu_ClassifyCommand, runs the page +0x88 scroll callback at 0x8A669F, so the root page proc input gate is released by this DRAW. BATTLE_MENU.md 4"),
    # kinds 1 2 3 4 6 0xE 0x11, the plain lists (magic, skills, overdrive moves, items, specials, Use)
    0x8A86F0: ("FFX_BtlMenu_List_Open", "+0x98 open callback for kinds 1, 2, 3, 4, 6, 0xE, 0x11 (Blk Magic, Wht Magic, Skill, overdrive moves, Item, Special, Use). Starts bsublay, returns 1"),
    0x8A8B20: ("FFX_BtlMenu_List_Step", "+0x9C step for the plain list kinds. FFX_BtlMenu_PageProc_B when page +0xE0 >= 0.30 s (flt_B3EB44), returns 0"),
    0x8A2A90: ("FFX_BtlMenu_List_Exit", "+0xA0 exit for the plain list kinds, StopPlayingUnprotected(2)"),
    0x8A7520: ("FFX_BtlMenu_List_Draw", "+0x90 draw for the plain list kinds, the body is FFX_BtlMenu_DrawListBody"),
    # kind 5 Summon / Grand Summon
    0x8A8710: ("FFX_BtlMenu_Summon_Open", "+0x98 open for kind 5 (Summon 0x3117, Grand Summon 0x3118). Starts bsublay"),
    0x8A8B50: ("FFX_BtlMenu_Summon_Step", "+0x9C step for kind 5. At page +0xE0 >= 0.45 s sets g_ffxBtlMenuListProcMode 2, calls FFX_BtlMenu_PageProc_B, sets it back to 0"),
    0x8A2AA0: ("FFX_BtlMenu_Summon_Exit", "+0xA0 exit for kind 5, StopPlayingUnprotected(2)"),
    0x8A75F0: ("FFX_BtlMenu_Summon_Draw", "+0x90 draw for kind 5, the aeon list"),
    # kinds 7 0xF Weapon / Armor
    0x8A8730: ("FFX_BtlMenu_Equip_Open", "+0x98 open for kinds 7 and 0xF (Weapon 0x3004, Armor 0x3005). Starts bsublay"),
    0x8A8B90: ("FFX_BtlMenu_Equip_Step", "+0x9C step for kinds 7 and 0xF. Below 0.45 s returns 0, else g_ffxBtlMenuListProcMode 3 around FFX_BtlMenu_PageProc_B"),
    0x8A2AB0: ("FFX_BtlMenu_Equip_Exit", "+0xA0 exit for kinds 7 and 0xF, StopPlayingUnprotected(2)"),
    0x8A7DC0: ("FFX_BtlMenu_Equip_Draw", "+0x90 draw for kinds 7 and 0xF, the equipment list"),
    # kind 0xA Switch
    0x8A84D0: ("FFX_BtlMenu_Switch_Enter", "kind 0xA (Switch 0x3002) +0x94 enter callback, the only enter callback in use. Builds the bench list (FFX_BtlMenu_GetPageList kind 10), cursor 0, StopUnprotected(2), starts bclose"),
    0x8A8600: ("FFX_BtlMenu_Switch_Open", "kind 0xA +0x98 open, returns 1"),
    0x8A87A0: ("FFX_BtlMenu_Switch_Step", "kind 0xA +0x9C step, FFX_BtlMenu_PageProc_Switch at page +0xE0 >= 0.30 s"),
    0x8A2A30: ("FFX_BtlMenu_Switch_Exit", "kind 0xA +0xA0 exit, returns 1 and stops nothing"),
    0x8A2DA0: ("FFX_BtlMenu_Switch_Draw", "kind 0xA +0x90 draw, the bench list"),
    0x8A2160: ("FFX_BtlMenu_PageProc_Switch", "Switch page input. Up/Down (word10 or the L1/L2 marker repeat) cycles with wrap, help from FFX_BtlMenu_SetHelpForUnitList, cancel 0x40 pops, confirm 0x20 checks FFX_BtlMenu_ClassifyCommand, bumps its own state and pushes the chosen unit. Auto battle forges cancel"),
    # kind 0xC left window, kind 0xD right window
    0x8A8620: ("FFX_BtlMenu_LeftWin_Open", "kind 0xC +0x98 open. The left window: the actor command list at +40 of its 1144 byte record, rows flagged 0x1000 at command row +28 (the overdrive parents and the scripted battle commands). Starts bleftwlay"),
    0x8A8A40: ("FFX_BtlMenu_LeftWin_Step", "kind 0xC +0x9C step, FFX_BtlMenu_PageProc_C at page +0xE0 >= 0.45 s"),
    0x8A2A50: ("FFX_BtlMenu_LeftWin_Exit", "kind 0xC +0xA0 exit, StopPlayingUnprotected(2)"),
    0x8A3680: ("FFX_BtlMenu_LeftWin_Draw", "kind 0xC +0x90 draw, the left window"),
    0x8A86D0: ("FFX_BtlMenu_RightWin_Open", "kind 0xD +0x98 open. The right window: the list at +56, rows flagged 0x800 (Escape, Weapon, Armor, and for aeons Shield, Boost, Dismiss). Starts brightwlay"),
    0x8A8AF0: ("FFX_BtlMenu_RightWin_Step", "kind 0xD +0x9C step, FFX_BtlMenu_PageProc_D at page +0xE0 >= 0.45 s"),
    0x8A2A80: ("FFX_BtlMenu_RightWin_Exit", "kind 0xD +0xA0 exit, StopPlayingUnprotected(2)"),
    0x8A6A60: ("FFX_BtlMenu_RightWin_Draw", "kind 0xD +0x90 draw, the right window"),
    # kind 0x14 Mix
    0x8A86B0: ("FFX_BtlMenu_Mix_Open", "kind 0x14 (Mix 0x311E) +0x98 open. Starts bsublay"),
    0x8A8AC0: ("FFX_BtlMenu_Mix_Step", "kind 0x14 +0x9C step, FFX_BtlMenu_PageProc_Mix at page +0xE0 >= 0.30 s"),
    0x8A2A70: ("FFX_BtlMenu_Mix_Exit", "kind 0x14 +0xA0 exit, StopPlayingUnprotected(2)"),
    0x8A6870: ("FFX_BtlMenu_Mix_Draw", "kind 0x14 +0x90 draw, the two item Mix list"),
    0x89D610: ("FFX_BtlMenu_PageProc_Mix", "Mix page input. Cancel 0x40 drops the last pick or pops, confirm 0x20 picks a row (FFX_SaveData_SetItemInUse on the first pick) and pushes after the second, L1 refused. Refuses all input while page +0x88 is not FFX_BtlMenu_ScrollIdle"),
    # kinds 0x15 0x16 the gil amount page
    0x8A8610: ("FFX_BtlMenu_Gil_Open", "kinds 0x15 / 0x16 (Spare Change 0x3024, Bribe 0x302A, Pay 0x3123) +0x98 open, returns 1"),
    0x8A87D0: ("FFX_BtlMenu_Gil_Step", "kinds 0x15 / 0x16 +0x9C step and input, with NO open lockout. Word10 Left/Right picks a digit, Up/Down adds or subtracts a power of ten, clamped to g_ffxBtlMenuGilMax. Confirm 0x20 pushes list entry 0 and stores g_ffxBtlMenuStagedGil, cancel 0x40 pops. Auto battle pays Yojimbo 1 gil minimum"),
    0x8A2A40: ("FFX_BtlMenu_Gil_Exit", "kinds 0x15 / 0x16 +0xA0 exit, StopPlayingUnprotected(2)"),
    0x8A32B0: ("FFX_BtlMenu_Gil_Draw", "kinds 0x15 / 0x16 +0x90 draw, the gil amount readout"),
    0x8A2030: ("FFX_BtlMenu_Gil_Init", "(cap) zeroes the gil page state, held gil from FFX_SaveData_GetGil, max = min(cap, held) or held when cap < 0, digit count from sprintf. Spare Change passes 100000, the others -1"),
    # the target page
    0x8A8780: ("FFX_BtlMenu_Target_Open", "target page +0x98 open. Starts bclose (the command window closing), returns 1"),
    0x8A8BE0: ("FFX_BtlMenu_Target_Step", "target page +0x9C step, FFX_BtlMenu_PageProc_Target every step, NO open lockout"),
    0x8A2AC0: ("FFX_BtlMenu_Target_Exit", "target page +0xA0 exit, returns 1 and stops nothing"),
    0x8A84C0: ("FFX_BtlMenu_Target_Draw", "target page +0x90 draw, FFX_BtlMenu_DrawTargetCursor"),
    0x89C870: ("FFX_BtlMenu_DrawTargetCursor", "hand cursor over every unit in g_ffxBtlMenuTargetMask. Skipped under auto battle"),
    0x89D390: ("FFX_BtlMenu_PageProc_Target", "target page input. Word10 moves the cursor by camera side (sub_780CE0), a group button swaps ally and enemy candidates, then g_ffxBtlMenuTargetMask = candidate mask and FFX_Btl_OnMenuTargetChanged EVERY step (simulation writes). Cancel 0x40: FFX_Btl_OnMenuTargetCancelled and pop. Confirm 0x20: FFX_BtlMenu_PushPage(actor, 0x3115), which commits"),
    0x89A860: ("FFX_BtlMenu_SetupTargetPage", "PushPage result 254: page +0x02 = 1 (target page), FFX_BtlMenu_BuildTargetCandidates, ++selections done at +0xA6, cursor and mask into +0x42 / +0xD8, FFX_BtlMenu_InstallTargetCallbacks"),
    0x8A1090: ("FFX_BtlMenu_InstallTargetCallbacks", "target page callbacks: +0x94 none, +0x98 Target_Open, +0x9C Target_Step, +0xA0 Target_Exit, +0x90 Target_Draw"),
    0x89ABD0: ("FFX_BtlMenu_BuildTargetCandidates", "(command, sub) clears the 320 byte target context at 0x133D0D0, stores the ids, builds the candidate masks (0x8985B0, 0x898490, 0x898330, 0x8983F0, 0x898640). Mix forces one candidate, the intersection mask"),
    # page stack plumbing
    0x89A630: ("FFX_BtlMenu_PopPage", "depth > 0: top page state 5, owner rewritten, chosen id 255, multi select mode cleared when the page opener was Mix 0x311E or Doublecast 0x3029, depth - 1, uncovered page state 2 (re-enter, so its open lockout restarts), its selection index stepped back. Callers then ++ the popped page state, 5 -> 6"),
    0x89B5A0: ("FFX_BtlMenu_GetPageList", "(actor, kind, &count) the id list a page shows, from sub_785410, count trimmed of trailing 255 (kinds 6, 17, 20 keep the full count, 7 and 15 at least 1)"),
    0x89B660: ("FFX_BtlMenu_InitForBattle", "from FFX_Btl_BeginBattle. g_ffxBtlMenuActive = 1, clears the page stack, target context and cursor memory, open mask 0, owner 255, party window flag 1, minigame timer state, reads dumm79 and dum201 from battle_script.bin, StopAll(2)"),
    0x89AD90: ("FFX_BtlMenu_ShutdownForBattle", "from FFX_Btl_Shutdown. Minigame phase 1, stops SE 0x8000004E, g_ffxBtlMenuActive = 0, MesWin object bank 0"),
    0x89BC60: ("FFX_BtlMenu_SetPartyWindowOn", "(on) g_ffxBtlMenuPartyWindowOn, from Btl 112 and 113 (SetMenuFlagOn / Off). Gates FFX_BtlMenu_DrawPartyWindow in FFX_BtlMenu_DrawRoot"),
    0x896450: ("FFX_BtlMenu_DrawPartyWindow", "the three front line rows (word_133F7AC, list 18): name and numbers, highlighted when in g_ffxBtlMenuTargetMask. Drawn only when g_ffxBtlMenuPartyWindowOn is 1"),
    0x89B590: ("FFX_BtlMenu_GetEnabled", "returns g_ffxBtlMenuEnabled, which is always 1. Only caller FFX_Btl_IsPadDrivenMagicPlaying"),
    0x89BC50: ("FFX_BtlMenu_SetEnabled_ORPHAN", "the only writer of g_ffxBtlMenuEnabled (0x89BC56). No code or data reference reaches it, so the flag keeps its .data value 1"),
    # per step helpers called from FFX_BtlMenu_Step
    0x8912E0: ("FFX_BtlMenu_StepHelp", "per step while the menu is open. Select (pressed 0x100) toggles save block option bit 0x400 (help window shown), then picks the help line for the row or target under the cursor and sets g_ffxBtlMenuHelpShown"),
    0x891140: ("FFX_BtlMenu_RefreshTargetUnits", "every sub-step, before the minigames. Rebuilds the ally unit list from list 9 (live allies) and word_133F0F0 from list 11 (live enemies)"),
    0x8979D0: ("FFX_BtlMenu_InitTargetUnits", "the same two lists, at FFX_BtlMenu_Open and FFX_BtlMenu_InitForBattle"),
    0x890F30: ("FFX_BtlMenu_StepCursorSpin", "adds pi/30 to g_ffxBtlMenuCursorSpinAngle each step the menu is open, wraps at pi. Presentation"),
    0x8A0CD0: ("FFX_BtlMenu_ArmOpenSound", "g_ffxBtlMenuOpenSoundPending = 1, from FFX_BtlMenu_Open"),
    0x8A0CE0: ("FFX_BtlMenu_ResetCtbWindow", "from FFX_BtlMenu_Open. Clears the CTB order window rows and seeds them from FFX_Btl_BuildCtbPreview"),
    0x89ECE0: ("FFX_BtlMenu_StepCtbWindow", "(command id, target mask) from FFX_BtlMenu_BuildStagedCommand every step. Rebuilds the CTB order window rows from FFX_Btl_BuildCtbPreview, scrolls on the R1 / R2 markers. Presentation built on the step"),
    0x8A0590: ("FFX_BtlMenu_DrawCtbWindow", "draws the CTB order window rows FFX_BtlMenu_StepCtbWindow built"),
    0x898DF0: ("FFX_BtlMenu_ScrollUpOnDraw", "page +0x88 scroll callback set by a page proc when the cursor leaves the top of the window. Run by the DRAW (0x89FF0F, 0x8A669F): +0x3C += 16, then +0x88 = FFX_BtlMenu_ScrollIdle"),
    0x898E20: ("FFX_BtlMenu_ScrollDownOnDraw", "page +0x88 scroll callback for the bottom edge, run by the draw: +0x3C -= 16, then back to FFX_BtlMenu_ScrollIdle"),
    0x898E40: ("FFX_BtlMenu_ScrollIdle", "the idle page +0x88 value. FFX_BtlMenu_PageProc_Root, _B and _Mix return at once while +0x88 is anything else, so input after a scroll waits for the next drawn step"),
    0x8B7C60: ("FFX_BtlMenu_GetScrollPixels", "returns 16, the pixel step of the two scroll callbacks"),
    0x89F370: ("FFX_BtlMenu_DrawListBody", "the shared list body for the list, summon, equip, left, right and Mix draws. Calls page +0x88 at 0x89FF0F"),
    0x8997C0: ("FFX_BtlMenu_SetHelpForUnitList", "help line for a unit row (Switch bench, Summon and Grand Summon lists)"),
    0x899970: ("FFX_BtlMenu_SetHelpForCommand", "help line for a command id row, from its ability record"),
    0x899EC0: ("FFX_BtlMenu_SetHelpForString", "help line from a kernel string id (the gil pages use 0x3014)"),
    0x89A8B0: ("FFX_BtlMenu_SetHelpForTarget", "help line for the unit under the target cursor"),
    0x890EB0: ("FFX_BtlMenu_HideHelp", "g_ffxBtlMenuHelpShown = 0, from the Triangle commit in FFX_BtlMenu_PageProc_C and _D"),
    0x8936D0: ("FFX_BtlMenu_DrawHelp", "draws g_ffxBtlMenuHelpText when save option bit 0x400 is set (jz at 0x8936EE skips when clear) and auto battle is off"),
    0x891580: ("maybe_FFX_BtlMenu_StepTargetInfo", "on a target page, for an enemy unit (20..27) under the cursor, builds the sliding info rows (0x899F20) and sets byte_133C93D. Name inferred from the reads"),
    0x895140: ("maybe_FFX_BtlMenu_DrawTargetInfo", "draws the rows maybe_FFX_BtlMenu_StepTargetInfo built, when byte_133C93D and save option bit 0x400 are set"),
    # battle core hooks the target page calls
    0x792DE0: ("FFX_Btl_OnMenuTargetChanged", "(actor, mask) from FFX_BtlMenu_PageProc_Target every step. g_ffxBtlCurTargetMask = mask, and when it differs from g_ffxBtlShownTargetMask and the command queue is empty: turns units (sub_7969F0), FFX_Btl_AimNeckAtSingleTarget, FFX_BtlEv_FireEvent(0, actor, 25, ..) at 0x792E2B. SIMULATION writes driven by the local cursor"),
    0x792A40: ("FFX_Btl_OnMenuTargetCancelled", "(actor) from the target page cancel. With an empty command queue: neck reset and FFX_BtlEv_FireEvent(0, actor, 132, ..). Always g_ffxBtlShownTargetMask = -1"),
    0x7B1E80: ("FFX_Btl_AimNeckAtSingleTarget", "(actor, mask) unless g_ffxBtlDbgNeckOff: exactly one other unit in the mask -> actor +0x438 = it, +0x439 = 5, else word +0x438 = 0x5FF. Also called by FFX_Btl_ResolveTargets"),
    # unreferenced code (no code or data reference, scanned)
    0x8A8550: ("FFX_BtlMenu_Root_OpenFirst_ORPHAN", "a copy of FFX_BtlMenu_Root_OpenFirst without the +0x98 swap. No reference"),
    0x8A8750: ("FFX_BtlMenu_Root_StepNow_ORPHAN", "sets page state 4 and +0x8C, then runs FFX_BtlMenu_Root_Step. No reference"),
    0x89ECA0: ("FFX_BtlMenu_InstallStub_ORPHAN", "StopUnprotected(2), +0x90 = sub_8A2C40, +0x8C = FFX_BtlMenu_Switch_Exit. No reference"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xC58EF8: ("g_ffxBtlMenuEnabled", "dword in initialised .data, value 1. FFX_BtlMenu_Step needs != 0 (0x89AE8A), FFX_BtlMenu_DrawRoot needs == 1. Its only writer FFX_BtlMenu_SetEnabled_ORPHAN is unreachable, so it is a constant"),
    0x133C948: ("g_ffxBtlMenuActive", "1 from FFX_BtlMenu_InitForBattle to FFX_BtlMenu_ShutdownForBattle. FFX_BtlMenu_DrawRoot returns at once when 0"),
    0xC58EF4: ("g_ffxBtlMenuPartyWindowOn", "byte, 1 at battle start, Btl 112 / 113 set it. FFX_BtlMenu_DrawRoot draws FFX_BtlMenu_DrawPartyWindow when it is 1"),
    0x133C94C: ("g_ffxBtlMenuMultiSelectMode", "0 normal, 0x100 Mix (0x311E, two items, one entry), 0x10000 Doublecast (0x3029, two spells, two target pages). Set by FFX_BtlMenu_SetPageKindForCommand, cleared by FFX_BtlMenu_Open and FFX_BtlMenu_PopPage"),
    0x133C93C: ("g_ffxBtlMenuHelpShown", "byte, the help line has content. FFX_BtlMenu_StepHelp sets it, the commits clear it"),
    0x1340452: ("g_ffxBtlMenuOpenSoundPending", "byte, 1 at FFX_BtlMenu_Open, FFX_BtlMenu_Root_Step plays SE 2 and clears it. Also picks FFX_BtlMenu_Root_OpenFirst"),
    0x133D6E8: ("g_ffxBtlMenuListProcMode", "2 while FFX_BtlMenu_Summon_Step runs FFX_BtlMenu_PageProc_B, 3 for FFX_BtlMenu_Equip_Step, else 0"),
    0x1340464: ("g_ffxBtlMenuGilAmount", "the gil amount on the gil page, copied to g_ffxBtlMenuStagedGil on confirm"),
    0x1340460: ("g_ffxBtlMenuGilMax", "the cap, min(cap, held gil)"),
    0x134045C: ("g_ffxBtlMenuGilHeld", "FFX_SaveData_GetGil at page init"),
    0x1340458: ("g_ffxBtlMenuGilDigit", "low word, the selected digit"),
    0x1340468: ("g_ffxBtlMenuGilLeft", "held minus amount, for the readout"),
    0x1340440: ("g_ffxBtlMenuGilCommitted", "the amount at confirm, 0 on cancel"),
    0x133F538: ("g_ffxBtlMenuHelpText", "128 byte help line buffer the FFX_BtlMenu_SetHelpFor* setters fill"),
    0x133F106: ("g_ffxBtlMenuPendingMpCost", "the first Doublecast spell's raw MP cost, passed to FFX_Btl_GetCommandMpCost by FFX_BtlMenu_ClassifyCommand for the second pick"),
    0x133FA7C: ("g_ffxBtlMenuCursorSpinAngle", "float, FFX_BtlMenu_StepCursorSpin"),
}

PLACEHOLDER = re.compile(
    r"^(sub|nullsub|loc|j_sub|unk|dword|word|byte|qword|off|flt|dbl|stru|funcs)_[0-9A-Fa-f]+$"
    r"|^[A-Za-z]+__f[0-9A-Fa-f]{6}$"
    r"|^FFX_AtelSys_[A-Za-z]+_\d+_(start|poll|resf|resi)$")


def _cur_name(ea):
    import idc
    return idc.get_name(ea) or ""


def _plan(table, kind):
    import idaapi
    import ida_name
    plan, skips, kept = [], [], []
    for ea, (new, why) in sorted(table.items()):
        cur = _cur_name(ea)
        if cur == new:
            kept.append("0x%X %s" % (ea, new))
            continue
        m = re.match(r"^was ([^ ]+)\.", why)
        if kind == "RENAMES" or (kind == "GLOBALS" and m):
            if not m:
                skips.append("0x%X %s: comment does not start 'was OLD.'" % (ea, new))
                continue
            if cur != m.group(1):
                skips.append("0x%X is %s, expected %s" % (ea, cur or "(no name)", m.group(1)))
                continue
        else:
            if cur and not PLACEHOLDER.match(cur):
                skips.append("0x%X is %s, not a placeholder" % (ea, cur))
                continue
        other = ida_name.get_name_ea(idaapi.BADADDR, new)
        if other != idaapi.BADADDR and other != ea:
            skips.append("0x%X name %s already used at 0x%X" % (ea, new, other))
            continue
        plan.append((ea, cur, new, why))
    return plan, skips, kept


def _add_comment(ea, text):
    import idc
    import ida_funcs
    if not text:
        return False
    f = ida_funcs.get_func(ea)
    if f and f.start_ea == ea:
        old = idc.get_func_cmt(ea, 1) or ""
        if text in old or len(old) + len(text) + 1 > 1000:
            return False
        return bool(idc.set_func_cmt(ea, (old + "\n" + text) if old else text, 1))
    old = idc.get_cmt(ea, 1) or ""
    if text in old or len(old) + len(text) + 1 > 1000:
        return False
    return bool(idc.set_cmt(ea, (old + " " + text) if old else text, 1))


def main(dry_run=DRY_RUN, comments=COMMENTS, verbose=VERBOSE):
    import idc
    import ida_name
    total = 0
    failed = 0
    for kind, table in (("NAMES", NAMES), ("RENAMES", RENAMES), ("GLOBALS", GLOBALS)):
        plan, skips, kept = _plan(table, kind)
        for line in kept:
            if verbose:
                print("ok    %s %s" % (kind, line))
        for line in skips:
            print("skip  %s %s" % (kind, line))
        for ea, cur, new, why in plan:
            print("name  %s 0x%X %s -> %s%s" % (kind, ea, cur or "(no name)", new, ("    " + why) if verbose else ""))
        print("%s: %d to rename, %d skipped, %d already named, %d rows" % (kind, len(plan), len(skips), len(kept), len(table)))
        total += len(plan)
        if dry_run:
            continue
        for ea, cur, new, why in plan:
            if not idc.set_name(ea, new, ida_name.SN_NOWARN | ida_name.SN_NOCHECK):
                print("FAILED 0x%X %s -> %s" % (ea, cur, new))
                failed += 1
            elif comments:
                _add_comment(ea, why)
    if dry_run:
        print("DRY_RUN, nothing renamed (%d would be)" % total)
        return True
    print("renamed %d, failed %d. The database is NOT saved, save it when you are happy" % (total - failed, failed))
    return failed == 0


def headless():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True, help="a COPY of the database")
    ap.add_argument("--apply", action="store_true", help="rehearse the renames, the database is not saved")
    ap.add_argument("--comments", action="store_true", help="also append each row's comment")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    import idapro
    if idapro.open_database(a.idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % a.idb)
    try:
        main(dry_run=not a.apply, comments=a.comments, verbose=a.verbose)
    finally:
        idapro.close_database(save=False)


if __name__ == "__main__":
    if "idaapi" in sys.modules or "ida_kernwin" in sys.modules:
        main()
    else:
        headless()
