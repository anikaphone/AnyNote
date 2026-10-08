#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#define IDR_MAIN_MENU           101
#define IDR_ACCELERATOR         102
#define IDI_APP_ICON            103
#define IDD_INSERT_CODE         104
#define IDD_SET_PASSWORD        105
#define IDD_ENTER_PASSWORD      106
#define IDD_NEW_VAULT           107
#define IDD_MANAGE_VAULTS       108
#define IDD_RENAME_VAULT        109

// File Commands
#define ID_FILE_NEW_NOTE        1001
#define ID_FILE_NEW_SUB_NOTE    1002
#define ID_FILE_DELETE_NOTE     1003
#define ID_FILE_RENAME_NOTE     1004
#define ID_FILE_NEW_NOTEBOOK    1005
#define ID_FILE_OPEN_NOTEBOOK   1006
#define ID_FILE_SAVE            1007
#define ID_FILE_EXIT            1008
#define ID_FILE_MANAGE_VAULTS   1009
#define ID_TREE_EXPAND_ALL      1020
#define ID_TREE_COLLAPSE_ALL    1021
#define ID_TREE_SEARCH          1022

// Edit Commands
#define ID_EDIT_UNDO            1101
#define ID_EDIT_REDO            1102
#define ID_EDIT_CUT             1103
#define ID_EDIT_COPY            1104
#define ID_EDIT_PASTE           1105
#define ID_EDIT_SELECTALL       1106
#define ID_EDIT_FIND            1110
#define ID_EDIT_FIND_NEXT       1111
#define ID_EDIT_FIND_PREV       1112
#define ID_SEARCH_GLOBAL        1113
#define ID_EDIT_REPLACE         1114
#define ID_EDIT_MARK            1115

// Format Commands
#define ID_FORMAT_HEADING_0     1210
#define ID_FORMAT_HEADING_1     1211
#define ID_FORMAT_HEADING_2     1212
#define ID_FORMAT_HEADING_3     1213
#define ID_FORMAT_HEADING_4     1214
#define ID_FORMAT_BOLD          1201
#define ID_FORMAT_ITALIC        1202
#define ID_FORMAT_UNDERLINE     1203
#define ID_FORMAT_STRIKE        1204
#define ID_FORMAT_CODE_BLOCK    1205
#define ID_FORMAT_BULLET_LIST   1206
#define ID_FORMAT_NUMBER_LIST   1207

// Insert Commands
#define ID_INSERT_IMAGE         1301
#define ID_INSERT_DATETIME      1302
#define ID_INSERT_TABLE         1303

// Table Operations Commands
#define ID_TABLE_INSERT_ROW_ABOVE  1310
#define ID_TABLE_INSERT_ROW_BELOW  1311
#define ID_TABLE_INSERT_COL_LEFT   1312
#define ID_TABLE_INSERT_COL_RIGHT  1313
#define ID_TABLE_DELETE_ROW        1314
#define ID_TABLE_DELETE_COL        1315
#define ID_TABLE_DELETE_TABLE      1316

// Code Block Commands
#define ID_CODEBLOCK_COPY          1320
#define ID_CODE_LANG_BASE          1330
#define ID_CODE_LANG_MAX           1340

// Security Commands
#define ID_SECURITY_ENCRYPT     1401

// Vault Dynamic Commands (1410 ~ 1450)
#define ID_VAULT_SWITCH_BASE    1410
#define ID_VAULT_SWITCH_MAX     1450

// Help Commands
#define ID_HELP_ABOUT           1501

// UI Child IDs
#define IDC_MAIN_TOOLBAR        2001
#define IDC_MAIN_STATUSBAR      2002
#define IDC_MAIN_TREEVIEW       2003
#define IDC_MAIN_RICHEDIT       2004
#define IDC_MAIN_SPLITTER       2005
#define IDC_MAIN_SEARCH_PANE    2006
#define IDC_MAIN_SEARCH_BAR     2007
#define IDC_MAIN_SPLITTER_H     2008
#define IDC_TOOLBAR_HEADING_COMBO 2010
#define IDC_MAIN_VAULT_BAR      2011
#define IDC_MAIN_NODE_SEARCH_BAR 2012

// Node Search Child IDs
#define IDC_NODE_SEARCH_EDIT    3301
#define IDC_NODE_SEARCH_COUNT   3302
#define IDC_NODE_SEARCH_CLOSE   3303

// Code Block Dialog IDs
#define IDC_CODE_LANG           2101
#define IDC_CODE_TEXT           2102

// Password Dialog IDs
#define IDC_PASSWORD_NEW        2201
#define IDC_PASSWORD_CONFIRM    2202
#define IDC_PASSWORD_INPUT      2203

// Vault Management Dialog IDs
#define IDC_NEW_VAULT_NAME      2301
#define IDC_NEW_VAULT_PATH      2302
#define IDC_VAULT_LIST          2311
#define IDC_VAULT_BTN_RENAME    2312
#define IDC_VAULT_BTN_REMOVE    2313
#define IDC_VAULT_BTN_EXPLORER  2314
#define IDC_VAULT_BTN_SWITCH    2315
#define IDC_VAULT_BTN_NEW       2316
#define IDC_RENAME_VAULT_INPUT  2321
