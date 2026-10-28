#!/usr/bin/env python3
"""
saveblock_fields.py - every byte of the FFX save block g_ffxSaveData, as data.

The block is g_ffxSaveData 0x112CA90 .. 0x1133350, 0x68C0 = 26,816 bytes. The evidence for every
row is in reversing/SAVE_BLOCK_MAP.md. This file is the machine readable half, written so Project
Zanarkand can generate its _32 layout (and the co-op mod its hash buckets) from one list.

FIELDS is a tuple of (offset, size, name, type, class) rows that tile 0..0x68C0 exactly, with no
gap and no overlap. `python -I saveblock_fields.py check` proves it.

  offset  block offset, bytes
  size    bytes
  name    snake_case, unique, unused runs are unused_<hex offset>
  type    u8 s8 u16 s16 u32 s32 u64 f32 char, an array of one ("u8[80]"), or a record name from
          RECORDS with a count ("SAVE_EQUIP_ENTRY[200]")
  class   persistent  real state the game writes and reads, it means the same thing on any machine
          derived     recomputed by the engine from other state, each step or on a recompute. Safe
                      to overwrite, the engine will put its own value back. NOTES says when
          scratch     a working value that only means something inside a map, a scene or one
                      operation, or that nothing in retail ever reads
          unused      nothing reads or writes it except whole-block copies and clears. The proof
                      for each one is in SAVE_BLOCK_MAP.md section 4

RECORDS holds the inner layouts of the three repeated records, in the same 5-tuple shape with
record relative offsets. SCRIPT_WORK_RUNS is the script variable area +0x1EC..+0x21EC split into
the runs some shipped ATEL script declares (True) and the runs no script declares (False). HAZARDS
names the fields whose value depends on the machine, the wall clock or the local player, which a
desync check or a transplant has to treat specially.

Usage
  python -I saveblock_fields.py check     prove FIELDS and every RECORDS entry tile exactly
  python -I saveblock_fields.py table     print the rows
  python -I saveblock_fields.py c         print FIELDS as a C struct body in ffx_types.h style
  python -I saveblock_fields.py classes   bytes per class

Nothing here reads the game, a save or the IDB.
"""
import sys

BLOCK_VA = 0x112CA90
BLOCK_SIZE = 0x68C0

BASIC = {"u8": 1, "s8": 1, "char": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "f32": 4, "u64": 8}

CLASSES = ("persistent", "derived", "scratch", "unused")

# ---------------------------------------------------------------------------------------------
# The three repeated records
# ---------------------------------------------------------------------------------------------

SAVE_EQUIP_ENTRY = (
    (0x00, 2, "name_id", "u16", "persistent"),
    (0x02, 1, "in_use", "u8", "persistent"),
    (0x03, 1, "flags", "u8", "persistent"),
    (0x04, 1, "for_char", "u8", "persistent"),
    (0x05, 1, "kind", "u8", "persistent"),
    (0x06, 1, "owner", "u8", "persistent"),
    (0x07, 1, "unused_07", "u8", "unused"),
    (0x08, 1, "actor_5c1", "u8", "persistent"),
    (0x09, 1, "actor_5c7", "u8", "persistent"),
    (0x0A, 1, "actor_5d8_add", "u8", "persistent"),
    (0x0B, 1, "ability_slot_count", "u8", "persistent"),
    (0x0C, 2, "model_id", "u16", "persistent"),
    (0x0E, 8, "abilities", "u16[4]", "persistent"),
)

SAVE_CHAR_RECORD = (
    (0x00, 2, "template_name_offset", "u16", "persistent"),
    (0x02, 2, "template_word_02", "u16", "persistent"),
    (0x04, 4, "base_max_hp", "s32", "persistent"),
    (0x08, 4, "base_max_mp", "s32", "persistent"),
    (0x0C, 1, "base_strength", "u8", "persistent"),
    (0x0D, 1, "base_defense", "u8", "persistent"),
    (0x0E, 1, "base_magic", "u8", "persistent"),
    (0x0F, 1, "base_magic_defense", "u8", "persistent"),
    (0x10, 1, "base_agility", "u8", "persistent"),
    (0x11, 1, "base_luck", "u8", "persistent"),
    (0x12, 1, "base_evasion", "u8", "persistent"),
    (0x13, 1, "base_accuracy", "u8", "persistent"),
    (0x14, 4, "ap_total", "s32", "persistent"),
    (0x18, 4, "ap_banked", "s32", "persistent"),
    (0x1C, 4, "hp", "s32", "persistent"),
    (0x20, 4, "mp", "s32", "persistent"),
    (0x24, 4, "max_hp", "s32", "derived"),
    (0x28, 4, "max_mp", "s32", "derived"),
    (0x2C, 1, "party_flags", "u8", "persistent"),
    (0x2D, 1, "weapon_slot", "u8", "persistent"),
    (0x2E, 1, "armour_slot", "u8", "persistent"),
    (0x2F, 8, "effective_stats", "u8[8]", "derived"),
    (0x37, 1, "poison_percent", "u8", "persistent"),
    (0x38, 3, "overdrive_bytes", "u8[3]", "persistent"),
    (0x3B, 1, "sphere_levels", "u8", "persistent"),
    (0x3C, 1, "sphere_levels_spent", "u8", "persistent"),
    (0x3D, 1, "revive_countdown", "u8", "persistent"),
    (0x3E, 12, "ability_flags", "u16[6]", "persistent"),
    (0x4A, 6, "auto_ability_flags", "u16[3]", "derived"),
    (0x50, 16, "battle_counters", "s32[4]", "persistent"),
    (0x60, 40, "overdrive_mode_counters", "u16[20]", "persistent"),
    (0x88, 4, "overdrive_mode_mask", "u32", "persistent"),
    (0x8C, 8, "unused_8c", "u8[8]", "unused"),
)

SAVE_CHAR_NAME = (
    (0x00, 18, "text", "u8[18]", "persistent"),
    (0x12, 1, "language", "u8", "persistent"),
    (0x13, 1, "renamed", "u8", "persistent"),
)

RECORDS = {
    "SAVE_EQUIP_ENTRY": (22, SAVE_EQUIP_ENTRY),
    "SAVE_CHAR_RECORD": (0x94, SAVE_CHAR_RECORD),
    "SAVE_CHAR_NAME": (20, SAVE_CHAR_NAME),
}

# ---------------------------------------------------------------------------------------------
# The script variable area, +0x1EC .. +0x21EC
# ---------------------------------------------------------------------------------------------
# (start, end, declared). Declared means some shipped ATEL script declares a class 0 variable whose
# extent (element size times count) covers the byte. Every one of the 3,997 declared bytes is
# touched by a variable opcode, and no undeclared byte is. The only C access to an undeclared byte
# is the menu config word at +0x5EC, split out below. Census over the 397 event packages, every
# monster file, every battle script and the menu script (which declares no variables).

SCRIPT_WORK_RUNS = (
    (0x01EC, 0x0228, True), (0x0228, 0x026C, False), (0x026C, 0x0289, True),
    (0x0289, 0x028B, False), (0x028B, 0x0292, True), (0x0292, 0x0294, False),
    (0x0294, 0x02A7, True), (0x02A7, 0x02A8, False), (0x02A8, 0x02C7, True),
    (0x02C7, 0x02EC, False), (0x02EC, 0x02EE, True), (0x02EE, 0x02F0, False),
    (0x02F0, 0x02F2, True), (0x02F2, 0x02F4, False), (0x02F4, 0x02F7, True),
    (0x02F7, 0x02FA, False), (0x02FA, 0x02FD, True), (0x02FD, 0x02FE, False),
    (0x02FE, 0x02FF, True), (0x02FF, 0x0300, False), (0x0300, 0x0304, True),
    (0x0304, 0x0305, False), (0x0305, 0x031E, True), (0x031E, 0x032C, False),
    (0x032C, 0x0338, True), (0x0338, 0x033A, False), (0x033A, 0x033C, True),
    (0x033C, 0x033D, False), (0x033D, 0x0358, True), (0x0358, 0x036C, False),
    (0x036C, 0x0383, True), (0x0383, 0x03AC, False), (0x03AC, 0x03C6, True),
    (0x03C6, 0x03EC, False), (0x03EC, 0x03FA, True), (0x03FA, 0x03FC, False),
    (0x03FC, 0x0403, True), (0x0403, 0x042C, False), (0x042C, 0x0444, True),
    (0x0444, 0x044D, False), (0x044D, 0x0454, True), (0x0454, 0x046C, False),
    (0x046C, 0x0472, True), (0x0472, 0x0474, False), (0x0474, 0x047C, True),
    (0x047C, 0x047D, False), (0x047D, 0x0487, True), (0x0487, 0x0488, False),
    (0x0488, 0x04A5, True), (0x04A5, 0x072C, False), (0x072C, 0x09E8, True),
    (0x09E8, 0x09EC, False), (0x09EC, 0x09EE, True), (0x09EE, 0x0A24, False),
    (0x0A24, 0x0A74, True), (0x0A74, 0x0A7C, False), (0x0A7C, 0x0A84, True),
    (0x0A84, 0x0A9E, False), (0x0A9E, 0x0AA1, True), (0x0AA1, 0x0AA2, False),
    (0x0AA2, 0x0AAE, True), (0x0AAE, 0x0BEC, False), (0x0BEC, 0x0BEE, True),
    (0x0BEE, 0x0BF4, False), (0x0BF4, 0x0BF8, True), (0x0BF8, 0x0BFC, False),
    (0x0BFC, 0x0BFE, True), (0x0BFE, 0x0C00, False), (0x0C00, 0x0C0C, True),
    (0x0C0C, 0x0C1C, False), (0x0C1C, 0x0C25, True), (0x0C25, 0x0C28, False),
    (0x0C28, 0x0C2A, True), (0x0C2A, 0x0C30, False), (0x0C30, 0x0C34, True),
    (0x0C34, 0x0C35, False), (0x0C35, 0x0C37, True), (0x0C37, 0x0C38, False),
    (0x0C38, 0x0C3A, True), (0x0C3A, 0x0C3C, False), (0x0C3C, 0x0C4D, True),
    (0x0C4D, 0x0C51, False), (0x0C51, 0x0C5D, True), (0x0C5D, 0x0C60, False),
    (0x0C60, 0x0C66, True), (0x0C66, 0x0C68, False), (0x0C68, 0x0C92, True),
    (0x0C92, 0x0CD7, False), (0x0CD7, 0x0D66, True), (0x0D66, 0x11EC, False),
    (0x11EC, 0x1636, True), (0x1636, 0x163E, False), (0x163E, 0x1BEC, True),
    (0x1BEC, 0x21EC, False),
)

MENU_CONFIG_5EC = (0x5EC, 2, "menu_config_5ec", "u16", "persistent")

# C code that reads inside declared script variables. Not separate fields, the bytes belong to the
# scripts, but the recreation's C side needs them by name.
SCRIPT_WORK_C_READERS = {
    0x034A: "FFX_MesWin_DrawInsertedValue, a message inserts a script value",
    0x0BEC: "scenario word, FFX_SaveData_GetScenarioWord 0x86C470 and many more, also read out of "
            "the staging image at staging +0xC2C by the orphan at 0x647EB0",
    0x0C39: "the Celestial weapons found bits, 0x01 Tidus 0x02 Yuna 0x04 Wakka 0x08 Auron 0x10 Kimahri 0x20 Lulu 0x40 Rikku (MINIGAMES_PROGRESSION.md 4.2). FFX_MainStep (trophy 27 on 1..126 then 127) and core:077 IsPadPressedRemapped (event 49, bit 0x20)",
    0x0C7F: "the Jecht Sphere count after the first, 9 = all ten (MINIGAMES_PROGRESSION.md 5). maybe_FFX_Scene_Init, the inline trophy 20 check at 0x88E09B (0x88ECC0 is an uncalled copy)",
    0x0C89: "FFX_SaveData_GetFlagsC89",
    0x11EC: "Blitz init reads 0x78 bytes here",
    0x157E: "Blitz init reads 0x3C bytes here",
}


def _script_work_fields():
    rows = []
    for lo, hi, declared in SCRIPT_WORK_RUNS:
        if declared:
            rows.append((lo, hi - lo, "script_vars_%04x" % lo, "u8[%d]" % (hi - lo), "persistent"))
            continue
        m = MENU_CONFIG_5EC
        if lo <= m[0] < hi:
            if m[0] > lo:
                rows.append((lo, m[0] - lo, "unused_%04x" % lo, "u8[%d]" % (m[0] - lo), "unused"))
            rows.append(m)
            e = m[0] + m[1]
            rows.append((e, hi - e, "unused_%04x" % e, "u8[%d]" % (hi - e), "unused"))
        else:
            rows.append((lo, hi - lo, "unused_%04x" % lo, "u8[%d]" % (hi - lo), "unused"))
    return tuple(rows)


# ---------------------------------------------------------------------------------------------
# The block
# ---------------------------------------------------------------------------------------------

HEADER = (
    (0x0000, 2, "map_id", "u16", "persistent"),
    (0x0002, 2, "prev_map_id", "u16", "persistent"),
    (0x0004, 2, "scene_id", "u16", "persistent"),
    (0x0006, 2, "prev_scene_id", "u16", "persistent"),
    (0x0008, 2, "scene_sub_id", "u16", "persistent"),
    (0x000A, 2, "prev_scene_sub_id", "u16", "persistent"),
    (0x000C, 1, "entry_point", "u8", "persistent"),
    (0x000D, 1, "prev_entry_point", "u8", "persistent"),
    (0x000E, 2, "map_name_id", "u16", "derived"),
    (0x0010, 1, "encounter_ground_attr", "u8", "derived"),
    (0x0011, 1, "warp_fade_kind", "u8", "persistent"),
    (0x0012, 1, "fade_out_frames", "u8", "persistent"),
    (0x0013, 1, "field_flags", "u8", "derived"),
    (0x0014, 4, "unused_0014", "u8[4]", "unused"),
    (0x0018, 8, "header_bits", "u64", "persistent"),
    (0x0020, 1, "party_snapshot_valid", "u8", "persistent"),
    (0x0021, 3, "party_snapshot", "u8[3]", "persistent"),
    (0x0024, 4, "party_snapshot_mask", "u32", "persistent"),
    (0x0028, 1, "indoor", "u8", "derived"),
    (0x0029, 1, "map_out_flag", "u8", "derived"),
    (0x002A, 1, "loaded_from_file", "u8", "scratch"),
    (0x002B, 1, "script_byte_2b", "u8", "scratch"),
    (0x002C, 32, "love_param", "u32[8]", "persistent"),
    (0x004C, 80, "event_flag_bits", "u8[80]", "persistent"),
    (0x009C, 12, "save_player_pos", "f32[3]", "persistent"),
    (0x00A8, 2, "checkpoint_bgm", "u16", "persistent"),
    (0x00AA, 2, "checkpoint_movie_word", "u16", "persistent"),
    (0x00AC, 8, "unused_00ac", "u8[8]", "unused"),
    (0x00B4, 2, "world_map_marker_x", "u16", "persistent"),
    (0x00B6, 2, "world_map_marker_y", "u16", "persistent"),
    (0x00B8, 2, "checkpoint_entry", "u16", "persistent"),
    (0x00BA, 2, "checkpoint_map", "u16", "persistent"),
    (0x00BC, 4, "playtime", "u32", "persistent"),
    (0x00C0, 16, "location_history", "u16[8]", "persistent"),
    (0x00D0, 1, "battle_return_mode", "u8", "scratch"),
    (0x00D1, 1, "rikku_alt_outfit", "u8", "persistent"),
    (0x00D2, 1, "shadow_flag", "u8", "persistent"),
    (0x00D3, 1, "unused_00d3", "u8", "unused"),
    (0x00D4, 2, "map_name_override", "u16", "persistent"),
    (0x00D6, 1, "map_name_override_active", "u8", "persistent"),
    (0x00D7, 1, "script_flag_d7", "u8", "persistent"),
    (0x00D8, 4, "premovie_number", "u32", "scratch"),
    (0x00DC, 4, "premovie_mode", "u32", "scratch"),
    (0x00E0, 4, "premovie_state", "u32", "scratch"),
    (0x00E4, 4, "field_boot_flags", "u32", "scratch"),
    (0x00E8, 2, "field_word_e8", "u16", "scratch"),
    (0x00EA, 2, "script_word_ea", "u16", "persistent"),
    (0x00EC, 32, "name_entry_text", "char[32]", "persistent"),
    (0x010C, 224, "unused_010c", "u8[224]", "unused"),
)

SPHERE_GRID = (
    (0x21EC, 2560, "grid_nodes", "u8[2560]", "persistent"),
    (0x2BEC, 1280, "grid_links", "u8[1280]", "persistent"),
    (0x30EC, 14, "grid_current_node", "u16[7]", "persistent"),
    (0x30FA, 10, "unused_30fa", "u8[10]", "unused"),
    (0x3104, 1, "grid_id", "u8", "persistent"),
    (0x3105, 1, "grid_zoom", "u8", "persistent"),
    (0x3106, 1030, "unused_3106", "u8[1030]", "unused"),
)

GUIDE_MAP = (
    (0x350C, 512, "guide_map_582", "u8[512]", "persistent"),
    (0x370C, 512, "guide_map_584", "u8[512]", "persistent"),
    (0x390C, 512, "guide_map_590", "u8[512]", "persistent"),
    (0x3B0C, 512, "guide_map_591", "u8[512]", "persistent"),
)

PARTY_AND_CONF = (
    (0x3D0C, 4, "option_bits", "u32", "persistent"),
    (0x3D10, 4, "al_bhed_primer_mask", "u32", "persistent"),
    (0x3D14, 4, "battle_count", "u32", "persistent"),
    (0x3D18, 16, "unused_3d18", "u8[16]", "unused"),
    (0x3D28, 32, "innate_ability_bits", "u16[16]", "persistent"),
    (0x3D48, 4, "gil", "u32", "persistent"),
    (0x3D4C, 4, "rng_seed_base", "u32", "persistent"),
    (0x3D50, 8, "unused_3d50", "u8[8]", "unused"),
    (0x3D58, 3, "party", "u8[3]", "persistent"),
    (0x3D5B, 17, "aeon_order", "u8[17]", "persistent"),
    (0x3D6C, 32, "shared_ability_bits", "u16[16]", "persistent"),
    (0x3D8C, 16, "start_item_ids", "u16[8]", "persistent"),
    (0x3D9C, 8, "start_item_counts", "u8[8]", "persistent"),
    (0x3DA4, 4, "yojimbo_motivation", "u32", "persistent"),
    (0x3DA8, 4, "yojimbo_word", "u32", "persistent"),
    (0x3DAC, 4, "tidus_overdrive_learn_count", "u32", "persistent"),
    (0x3DB0, 4, "rikku_steal_count", "u32", "persistent"),
    (0x3DB4, 4, "gil_spent_302a", "u32", "persistent"),
    (0x3DB8, 20, "unused_3db8", "u8[20]", "unused"),
    (0x3DCC, 128, "world_flags", "u8[128]", "persistent"),
    (0x3E4C, 128, "unused_3e4c", "u8[128]", "unused"),
)

INVENTORY_AND_ROSTER = (
    (0x3ECC, 512, "item_ids", "u16[256]", "persistent"),
    (0x40CC, 256, "item_counts", "u8[256]", "persistent"),
    (0x41CC, 32, "item_gained_mask", "u16[16]", "persistent"),
    (0x41EC, 32, "item_lost_mask", "u16[16]", "persistent"),
    (0x420C, 512, "capture_counts", "u8[512]", "persistent"),
    (0x440C, 64, "monster_seen_mask", "u8[64]", "persistent"),
    (0x444C, 64, "monster_defeated_mask", "u8[64]", "persistent"),
    (0x448C, 16, "key_item_bits", "u8[16]", "persistent"),
    (0x449C, 4400, "equipment", "SAVE_EQUIP_ENTRY[200]", "persistent"),
    (0x55CC, 2664, "char_records", "SAVE_CHAR_RECORD[18]", "persistent"),
    (0x6034, 792, "char_ability_bits", "u8[792]", "persistent"),
    (0x634C, 360, "char_names", "SAVE_CHAR_NAME[18]", "persistent"),
    (0x64B4, 4, "crc", "u32", "scratch"),
    (0x64B8, 1032, "unused_64b8", "u8[1032]", "unused"),
)

FIELDS = HEADER + _script_work_fields() + SPHERE_GRID + GUIDE_MAP + PARTY_AND_CONF + INVENTORY_AND_ROSTER

# ---------------------------------------------------------------------------------------------
# Notes and hazards, keyed by field name
# ---------------------------------------------------------------------------------------------

NOTES = {
    "map_name_id": "set from the ground dictionary on map entry, SetMapNameId / SetMapNameIdFromGroundDic",
    "encounter_ground_attr": "rewritten every field step from the controlled actor's ground triangle",
    "field_flags": "bit 1 encounters allowed, StepFrame rewrites it every step",
    "indoor": "FFX_Field_UpdateIndoorFlag 0x871E50 compares camera height with ctx+520, then SetIndoor copies it here",
    "map_out_flag": "bit 0, FFX_Field_UpdateIndoorFlag, per map",
    "loaded_from_file": "1 in every file image (Serialize marks the copy), consumed once by SetSceneAndSub",
    "script_byte_2b": "core:250 writes it in 9 places, only core:249 (dead) and a debug page read it",
    "save_player_pos": "written only into the staging image at save time, read back from the staging image on load. The live copy is never read",
    "checkpoint_bgm": "0xFFFF in the staging image during a manual save",
    "checkpoint_movie_word": "word_C322A8 + 1, the background movie id plus a flag [I]. 0 in manual saves",
    "world_map_marker_x": "core:366, drawn by the save screen as the scene30 sprite at x/2",
    "battle_return_mode": "written by FFX_Btl_ReturnToField and Reset, no reader",
    "shadow_flag": "only Reset writes it, FFX_Ch_ShadowsEnabled reads it",
    "premovie_number": "premovie bookkeeping, cleared on map unload",
    "field_boot_flags": "cleared by maybe_FFX_Map_Unload and sub_872BB0",
    "field_word_e8": "cleared by maybe_FFX_Map_Unload and sub_872BB0",
    "name_entry_text": "free text from the name entry screen, compared by core:466",
    "grid_nodes": "1280 x {panel kind, activation mask}",
    "grid_links": "one byte per grid line, a per-character mask",
    "guide_map_582": "64 x 64 explored cell bitmap, set each step around the local player",
    "option_bits": "settings, also the sphere grid type in bits 14..15 and the text variant in bit 16",
    "innate_ability_bits": "kernel table 31 copy, words 0..5 ids 0x3000..0x305F, words 6..15 ids 0x3060 and up",
    "rng_seed_base": "kernel conf value, read once by FFX_InitNewSaveData to seed the RNG streams",
    "shared_ability_bits": "ids 0x3060 and up, bytes 28..31 are not addressable by a valid id",
    "item_gained_mask": "set only, by FFX_SaveData_MarkItemDelta from AddItem. Nothing reads it",
    "item_lost_mask": "set only, by FFX_SaveData_MarkItemDelta from AddItem. Nothing reads it",
    "capture_counts": "monster species 0..138 are valid indexes",
    "char_ability_bits": "18 x 44, bytes 40..43 of each row are not addressable by a valid command id",
    "crc": "only written into the file image, 0 in a block loaded through the PC Load Game",
}

HAZARDS = {
    "playtime": "wall clock. FFX_Save_StepPlayTimeFromWallClock 0x86CA20 adds FFX_Time_AppElapsedSeconds on the vsync thread",
    "save_player_pos": "float, local player position, only meaningful in a file image",
    "checkpoint_bgm": "differs between a manual save and an autosave of the same moment",
    "checkpoint_movie_word": "differs between a manual save and an autosave of the same moment",
    "loaded_from_file": "1 after a file load, 0 after a new game",
    "indoor": "camera dependent, and the camera is per machine",
    "map_out_flag": "camera dependent, same writer as indoor",
    "option_bits": "per player settings, including the text variant bit 0x10000",
    "char_names": "byte 18 of each record is FFX_GetLanguage at the time of the rename, per machine",
    "guide_map_582": "set from the local player position each step, so it diverges in co-op",
    "guide_map_584": "set from the local player position each step",
    "guide_map_590": "set from the local player position each step",
    "guide_map_591": "set from the local player position each step",
    "crc": "nonzero only after a raw install of file bytes",
    "encounter_ground_attr": "follows the controlled actor, per step",
}


# ---------------------------------------------------------------------------------------------
# Checks and output
# ---------------------------------------------------------------------------------------------

def type_size(t):
    base, n = t, 1
    if "[" in t:
        base, rest = t.split("[", 1)
        n = int(rest.rstrip("]"), 0)
    if base in BASIC:
        return BASIC[base] * n
    if base in RECORDS:
        return RECORDS[base][0] * n
    raise ValueError("unknown type %s" % t)


def _check_rows(rows, total, what):
    errs = []
    pos = 0
    names = set()
    for off, size, name, typ, cls in rows:
        if off != pos:
            errs.append("%s: row %s at 0x%X, expected 0x%X" % (what, name, off, pos))
        if type_size(typ) != size:
            errs.append("%s: row %s type %s is %d bytes, size says %d" % (what, name, typ, type_size(typ), size))
        if cls not in CLASSES:
            errs.append("%s: row %s class %s" % (what, name, cls))
        if name in names:
            errs.append("%s: duplicate name %s" % (what, name))
        names.add(name)
        pos = off + size
    if pos != total:
        errs.append("%s: ends at 0x%X, expected 0x%X" % (what, pos, total))
    return errs


def check():
    errs = _check_rows(FIELDS, BLOCK_SIZE, "FIELDS")
    for rname, (rsize, rows) in RECORDS.items():
        errs += _check_rows(rows, rsize, rname)
    names = set(r[2] for r in FIELDS)
    for k in list(NOTES) + list(HAZARDS):
        if k not in names:
            errs.append("note or hazard for unknown field %s" % k)
    pos = 0x1EC
    for lo, hi, _ in SCRIPT_WORK_RUNS:
        if lo != pos:
            errs.append("SCRIPT_WORK_RUNS gap at 0x%X" % pos)
        pos = hi
    if pos != 0x21EC:
        errs.append("SCRIPT_WORK_RUNS ends at 0x%X" % pos)
    return errs


def class_bytes():
    out = dict((c, 0) for c in CLASSES)
    for off, size, name, typ, cls in FIELDS:
        rec = typ.split("[")[0]
        if rec in RECORDS:
            rsize, rows = RECORDS[rec]
            n = size // rsize
            for _, s, _, _, c in rows:
                out[c] += s * n
        else:
            out[cls] += size
    return out


def emit_c():
    lines = []
    for off, size, name, typ, cls in FIELDS:
        base, arr = typ, ""
        if "[" in typ:
            base, rest = typ.split("[", 1)
            arr = "[" + rest
        ctype = {"u8": "uint8_t", "s8": "int8_t", "char": "char", "u16": "uint16_t", "s16": "int16_t",
                 "u32": "uint32_t", "s32": "int32_t", "u64": "uint64_t", "f32": "float"}.get(base, base)
        member = "_unk_%x" % off if cls == "unused" else "m_" + name
        lines.append("  /* 0x%04X */ %s %s%s;  // %s" % (off, ctype, member, arr, cls))
    return "\n".join(lines)


def main(argv):
    mode = argv[1] if len(argv) > 1 else "check"
    if mode == "check":
        errs = check()
        for e in errs:
            print(e)
        print("%d fields, %d record layouts, %s" % (len(FIELDS), len(RECORDS), "OK" if not errs else "%d errors" % len(errs)))
        return 0 if not errs else 1
    if mode == "table":
        for off, size, name, typ, cls in FIELDS:
            print("+0x%04X %5d  %-11s %-24s %s" % (off, size, cls, typ, name))
        return 0
    if mode == "c":
        print(emit_c())
        return 0
    if mode == "classes":
        cb = class_bytes()
        for c in CLASSES:
            print("%-11s %6d" % (c, cb[c]))
        print("%-11s %6d" % ("total", sum(cb.values())))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
