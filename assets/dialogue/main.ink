// The Keeper's Amulet: dialogue.
// Compile with inklecate (or Inky: File > Export to JSON) to main.ink.json.
//
// VARs whose names match the game's store variables are synced with the
// engine: pushed in when a conversation starts, read back after each line.
// Inventory is has_<item> (1 = held). Keep progression state in synced
// VARs, not in visit counts: each conversation starts with fresh Ink state.

VAR has_mushroom = 0
VAR has_axe = 0
VAR tobin_has_axe = 1
VAR saw_glint = 0
VAR asked_crypt = 0
VAR asked_key = 0
VAR key_in_well = 1
VAR gate_open = 0

INCLUDE tobin.ink

-> END
