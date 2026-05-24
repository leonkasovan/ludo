
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "ui.h"


/*
 * Boilerplate macros
 */

#define CAST_ARG(n, type) ui ## type(((struct wrap *)lua_touserdata(L, n))->control)

#define RETURN_SELF lua_pushvalue(L, 1); return 1;

#define CREATE_META(n) \
	luaL_newmetatable(L, "libui." #n);  \
	luaL_setfuncs(L, meta_ ## n, 0);

#define CREATE_OBJECT(t, c) \
	struct wrap *w = lua_newuserdata(L, sizeof(struct wrap)); \
	w->control = uiControl(c); \
	lua_newtable(L); \
	luaL_getmetatable(L, "libui." #t); \
	lua_setfield(L, -2, "__index"); \
	lua_pushcfunction(L, l_gc); \
	lua_setfield(L, -2, "__gc"); \
	lua_setmetatable(L, -2);


struct wrap {
	uiControl *control;
};

struct imgwrap {
	uiImage *img;
};

/* forward declaration — used by CREATE_OBJECT macro */
int l_gc(lua_State *L);

/* --- window tracker for app-shutdown cleanup ---------------------- */

#define MAX_TRACKED_WINDOWS 64
static uiWindow *g_tracked_windows[MAX_TRACKED_WINDOWS];
static int g_tracked_window_count = 0;

static void tracked_window_add(uiWindow *w)
{
	if (g_tracked_window_count < MAX_TRACKED_WINDOWS)
		g_tracked_windows[g_tracked_window_count++] = w;
}

static void tracked_window_remove(uiWindow *w)
{
	for (int i = 0; i < g_tracked_window_count; i++) {
		if (g_tracked_windows[i] == w) {
			g_tracked_windows[i] = g_tracked_windows[--g_tracked_window_count];
			return;
		}
	}
}

void libuilua_destroy_all_windows(void)
{
	while (g_tracked_window_count > 0) {
		uiWindow *w = g_tracked_windows[--g_tracked_window_count];
		uiControlDestroy(uiControl(w));
	}
}

void libuilua_request_close_all_windows(void)
{
	fprintf(stderr, "[libuilua] request_close_all_windows: %d tracked windows\n",
		g_tracked_window_count);
#ifdef _WIN32
	for (int i = g_tracked_window_count - 1; i >= 0; i--) {
		uiWindow *w = g_tracked_windows[i];
		HWND hwnd = (HWND)uiControlHandle(uiControl(w));
		fprintf(stderr, "[libuilua]   posting WM_CLOSE to hwnd=%p\n", (void *)hwnd);
		PostMessageW(hwnd, WM_CLOSE, 0, 0);
	}
#endif
}

/* ------------------------------------------------------------------ */

/* StaticImage widget - wraps an area with a draw handler that renders a uiImage */
struct si_handler {
	uiAreaHandler handler;      /* MUST be first */
	uiImage *img;
	int img_w, img_h;           /* natural pixel dimensions */
	unsigned char *pixels;      /* raw RGBA data (owned) */
	int pw, ph;                 /* pixel buffer dimensions */
};

struct si_lua {
	uiControl *control;         /* area cast to uiControl (offset 0, CAST_ARG-compatible) */
	struct si_handler *h;
};

/* TableModel handler - wraps Lua handler table with C callbacks */
typedef struct LuaTableModelHandler LuaTableModelHandler;
struct LuaTableModelHandler {
	uiTableModelHandler base;  /* MUST be first */
	lua_State *L;
	int handler_ref;           /* luaL_ref to Lua handler table */
	int model_udata_ref;       /* luaL_ref to Lua tmwrap userdata */
};

struct tmwrap {
	uiTableModel *model;
	LuaTableModelHandler *lmh;
};


static void create_callback_data(lua_State *L, int control_index, int fn_index, int data_index)
{
	/* Push registery key: userdata pointer to control */

	lua_pushlightuserdata(L, CAST_ARG(control_index, Control));

	/* Create table with callback data */

	lua_newtable(L);
	lua_pushvalue(L, control_index);
	lua_setfield(L, -2, "udata");
	lua_pushvalue(L, fn_index);
	lua_setfield(L, -2, "fn");
	if(data_index > 0 && data_index <= lua_gettop(L)) {
		lua_pushvalue(L, data_index);
	} else {
		lua_pushnil(L);
	}
	lua_setfield(L, -2, "data");

	/* Store in registry */

	lua_settable(L, LUA_REGISTRYINDEX);

}

static void clear_callback_data(lua_State *L, void *control)
{
	lua_pushlightuserdata(L, control);
	lua_pushnil(L);
	lua_settable(L, LUA_REGISTRYINDEX);
}

static int push_callback(lua_State *L, void *control)
{
	/* Find table with callback data in registry */

	lua_pushlightuserdata(L, control);
	lua_gettable(L, LUA_REGISTRYINDEX);
	if(!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	/* Get function, control userdata and callback data */

	lua_getfield(L, -1, "fn");
	luaL_checktype(L, -1, LUA_TFUNCTION);
	lua_getfield(L, -2, "udata");
	luaL_checktype(L, -1, LUA_TUSERDATA);
	lua_getfield(L, -3, "data");
	lua_remove(L, -4);

	return 1;
}

static void callback(lua_State *L, void *control)
{
	if(!push_callback(L, control))
		return;

	/* Call function */

	if(lua_pcall(L, 2, 0, 0) != LUA_OK) {
		lua_pop(L, 1);
	}
}

static int callback_boolean(lua_State *L, void *control, int default_value)
{
	if(!push_callback(L, control))
		return default_value;

	if(lua_pcall(L, 2, 1, 0) != LUA_OK) {
		lua_pop(L, 1);
		return default_value;
	}
	default_value = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return default_value;
}


/*
 * Area
 */

int l_NewArea(lua_State *L)
{
	static struct uiAreaHandler ah;
	CREATE_OBJECT(Area, uiNewArea(&ah));
	return 1;
}

int l_AreaSetSize(lua_State *L)
{
	uiAreaSetSize(CAST_ARG(1, Area), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
	RETURN_SELF;
}

static struct luaL_Reg meta_Area[] = {
	{ "SetSize",              l_AreaSetSize },
	{ NULL }
};


/*
 * Box
 */

int l_NewVerticalBox(lua_State *L)
{
	CREATE_OBJECT(Box, uiNewVerticalBox());
	return 1;
}

int l_NewHorizontalBox(lua_State *L)
{
	CREATE_OBJECT(Box, uiNewHorizontalBox());
	return 1;
}

int l_BoxAppend(lua_State *L)
{
	int n = lua_gettop(L);
	int stretchy = 0;

	if(lua_isnumber(L, n) || lua_isboolean(L, n)) {
		stretchy = lua_toboolean(L, n);
	}

	int i;

	for(i=2; i<=n; i++) {
		if(lua_isuserdata(L, i)) {
			uiBoxAppend(CAST_ARG(1, Box), CAST_ARG(i, Control), stretchy);
			lua_getmetatable(L, 1);
			lua_pushvalue(L, i);
			luaL_ref(L, -2);
		}
	}
	RETURN_SELF;
}

int l_BoxPadded(lua_State *L)
{
	lua_pushnumber(L, uiBoxPadded(CAST_ARG(1, Box)));
	return 1;
}

int l_BoxSetPadded(lua_State *L)
{
	uiBoxSetPadded(CAST_ARG(1, Box), luaL_checknumber(L, 2));
	RETURN_SELF;
}


int l_BoxDelete(lua_State *L)
{
	uiBoxDelete(CAST_ARG(1, Box), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

int l_BoxNumChildren(lua_State *L)
{
	lua_pushinteger(L, uiBoxNumChildren(CAST_ARG(1, Box)));
	return 1;
}

static struct luaL_Reg meta_Box[] = {
	{ "Append",               l_BoxAppend },
	{ "Padded",               l_BoxPadded },
	{ "SetPadded",            l_BoxSetPadded },
	{ "Delete",               l_BoxDelete },
	{ "NumChildren",          l_BoxNumChildren },
	{ NULL }
};


/*
 * Button
 */

int l_NewButton(lua_State *L)
{
	CREATE_OBJECT(Button, uiNewButton(
		luaL_checkstring(L, 1)
	));
	return 1;
}

static void on_button_clicked(uiButton *b, void *data)
{
	callback(data, b);
}

int l_ButtonSetText(lua_State *L)
{
	uiButtonSetText(CAST_ARG(1, Button), luaL_checkstring(L, 2));
	RETURN_SELF;
}

int l_ButtonOnClicked(lua_State *L)
{
        uiButtonOnClicked(CAST_ARG(1, Button), on_button_clicked, L);
        create_callback_data(L, 1, 2, 3);
        RETURN_SELF;
}

int l_ButtonText(lua_State *L)
{
	char *text = uiButtonText(CAST_ARG(1, Button));
	lua_pushstring(L, text ? text : "");
	uiFreeText(text);
	return 1;
}

static struct luaL_Reg meta_Button[] = {
	{ "Text",                 l_ButtonText },
	{ "SetText",              l_ButtonSetText },
	{ "OnClicked",            l_ButtonOnClicked },
	{ NULL }
};


/*
 * Checkbox
 */

int l_NewCheckbox(lua_State *L)
{
	CREATE_OBJECT(Checkbox, uiNewCheckbox(
		luaL_checkstring(L, 1)
	));
	return 1;
}

static void on_checkbox_toggled(uiCheckbox *c, void *data)
{
	callback(data, c);
}

int l_CheckboxSetText(lua_State *L)
{
	uiCheckboxSetText(CAST_ARG(1, Checkbox), luaL_checkstring(L, 2));
	RETURN_SELF;
}

int l_CheckboxOnToggled(lua_State *L)
{
        uiCheckboxOnToggled(CAST_ARG(1, Checkbox), on_checkbox_toggled, L);
        create_callback_data(L, 1, 2, 3);
        RETURN_SELF;
}

int l_CheckboxChecked(lua_State *L)
{
	lua_pushinteger(L, uiCheckboxChecked(CAST_ARG(1, Checkbox)));
	return 1;
}

int l_CheckboxSetChecked(lua_State *L)
{
	uiCheckboxSetChecked(CAST_ARG(1, Checkbox), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

static struct luaL_Reg meta_Checkbox[] = {
	{ "SetText",              l_CheckboxSetText },
	{ "OnToggled",            l_CheckboxOnToggled },
	{ "Checked",              l_CheckboxChecked },
	{ "SetChecked",           l_CheckboxSetChecked },
	{ NULL }
};


/*
 * Combobox
 */

int l_NewCombobox(lua_State *L)
{
	CREATE_OBJECT(Combobox, uiNewCombobox());
	return 1;
}

static void on_combobox_selected(uiCombobox *c, void *data)
{
	callback(data, c);
}

int l_ComboboxAppend(lua_State *L)
{
	int n = lua_gettop(L);
	int i;
	for(i=2; i<=n; i++) {
		uiComboboxAppend(CAST_ARG(1, Combobox), luaL_checkstring(L, i));
	}
	RETURN_SELF;
}

int l_ComboboxInsertAt(lua_State *L)
{
	uiComboboxInsertAt(CAST_ARG(1, Combobox), (int)luaL_checkinteger(L, 2), luaL_checkstring(L, 3));
	RETURN_SELF;
}

int l_ComboboxDelete(lua_State *L)
{
	uiComboboxDelete(CAST_ARG(1, Combobox), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

int l_ComboboxClear(lua_State *L)
{
	uiComboboxClear(CAST_ARG(1, Combobox));
	RETURN_SELF;
}

int l_ComboboxNumItems(lua_State *L)
{
	lua_pushinteger(L, uiComboboxNumItems(CAST_ARG(1, Combobox)));
	return 1;
}

int l_ComboboxOnToggled(lua_State *L)
{
	uiComboboxOnSelected(CAST_ARG(1, Combobox), on_combobox_selected, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

int l_ComboboxSelected(lua_State *L)
{
	lua_pushinteger(L, uiComboboxSelected(CAST_ARG(1, Combobox)));
	return 1;
}

int l_ComboboxSetSelected(lua_State *L)
{
	uiComboboxSetSelected(CAST_ARG(1, Combobox), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

static struct luaL_Reg meta_Combobox[] = {
	{ "Append",               l_ComboboxAppend },
	{ "InsertAt",             l_ComboboxInsertAt },
	{ "Delete",               l_ComboboxDelete },
	{ "Clear",                l_ComboboxClear },
	{ "NumItems",             l_ComboboxNumItems },
	{ "OnToggled",            l_ComboboxOnToggled },
	{ "Selected",             l_ComboboxSelected },
	{ "SetSelected",          l_ComboboxSetSelected },
	{ NULL }
};


/*
 * EditableCombobox
 */

int l_NewEditableCombobox(lua_State *L)
{
	CREATE_OBJECT(EditableCombobox, uiNewEditableCombobox());
	return 1;
}

static void on_editablecombobox_changed(uiEditableCombobox *c, void *data)
{
	callback(data, c);
}

int l_EditableComboboxAppend(lua_State *L)
{
	int n = lua_gettop(L);
	int i;
	for(i=2; i<=n; i++) {
		uiEditableComboboxAppend(CAST_ARG(1, EditableCombobox), luaL_checkstring(L, i));
	}
	RETURN_SELF;
}

int l_EditableComboboxText(lua_State *L)
{
	char *text = uiEditableComboboxText(CAST_ARG(1, EditableCombobox));
	lua_pushstring(L, text ? text : "");
	uiFreeText(text);
	return 1;
}

int l_EditableComboboxSetText(lua_State *L)
{
	uiEditableComboboxSetText(CAST_ARG(1, EditableCombobox), luaL_checkstring(L, 2));
	RETURN_SELF;
}

int l_EditableComboboxOnChanged(lua_State *L)
{
	uiEditableComboboxOnChanged(CAST_ARG(1, EditableCombobox), on_editablecombobox_changed, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

static struct luaL_Reg meta_EditableCombobox[] = {
	{ "Append",               l_EditableComboboxAppend },
	{ "Text",                 l_EditableComboboxText },
	{ "SetText",              l_EditableComboboxSetText },
	{ "OnChanged",            l_EditableComboboxOnChanged },
	{ NULL }
};


/*
 * Control
 */

int l_ControlShow(lua_State *L)
{
	uiControlShow(CAST_ARG(1, Control));
	RETURN_SELF;
}

int l_ControlHide(lua_State *L)
{
	uiControlHide(CAST_ARG(1, Control));
	RETURN_SELF;
}

int l_ControlEnable(lua_State *L)
{
	uiControlEnable(CAST_ARG(1, Control));
	RETURN_SELF;
}

int l_ControlDisable(lua_State *L)
{
	uiControlDisable(CAST_ARG(1, Control));
	RETURN_SELF;
}

int l_ControlVisible(lua_State *L)
{
	lua_pushboolean(L, uiControlVisible(CAST_ARG(1, Control)));
	return 1;
}

int l_ControlEnabled(lua_State *L)
{
	lua_pushboolean(L, uiControlEnabled(CAST_ARG(1, Control)));
	return 1;
}

int l_ControlDestroy(lua_State *L)
{
	tracked_window_remove((uiWindow *)CAST_ARG(1, Control));
	clear_callback_data(L, CAST_ARG(1, Control));
	uiControlDestroy(CAST_ARG(1, Control));
	return 0;
}



/*
 * Entry
 */

int l_NewEntry(lua_State *L)
{
	CREATE_OBJECT(Entry, uiNewEntry());
	return 1;
}

int l_EntryText(lua_State *L)
{
	char *text = uiEntryText(CAST_ARG(1, Entry));
	lua_pushstring(L, text ? text : "");
	uiFreeText(text);
	return 1;
}

int l_EntrySetText(lua_State *L)
{
	uiEntrySetText(CAST_ARG(1, Entry), luaL_checkstring(L, 2));
	RETURN_SELF;
}

static void on_entry_changed(uiEntry *e, void *data)
{
	callback(data, e);
}

int l_EntryOnChanged(lua_State *L)
{
	uiEntryOnChanged(CAST_ARG(1, Entry), on_entry_changed, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

int l_EntryReadOnly(lua_State *L)
{
	lua_pushboolean(L, uiEntryReadOnly(CAST_ARG(1, Entry)));
	return 1;
}

int l_EntrySetReadOnly(lua_State *L)
{
	uiEntrySetReadOnly(CAST_ARG(1, Entry), lua_toboolean(L, 2));
	RETURN_SELF;
}

static struct luaL_Reg meta_Entry[] = {
	{ "Text",                 l_EntryText },
	{ "SetText",              l_EntrySetText },
	{ "OnChanged",            l_EntryOnChanged },
	{ "ReadOnly",             l_EntryReadOnly },
	{ "SetReadOnly",          l_EntrySetReadOnly },
	{ NULL }
};


/*
 * Password / Search Entry constructors (share Entry meta)
 */

int l_NewPasswordEntry(lua_State *L)
{
	CREATE_OBJECT(Entry, uiNewPasswordEntry());
	return 1;
}

int l_NewSearchEntry(lua_State *L)
{
	CREATE_OBJECT(Entry, uiNewSearchEntry());
	return 1;
}


/*
 * Date/Timepicker
 */

int l_NewDateTimePicker(lua_State *L)
{
	CREATE_OBJECT(DateTimePicker, uiNewDateTimePicker());
	return 1;
}

int l_NewDatePicker(lua_State *L)
{
	CREATE_OBJECT(DateTimePicker, uiNewDatePicker());
	return 1;
}

int l_NewTimePicker(lua_State *L)
{
	CREATE_OBJECT(DateTimePicker, uiNewTimePicker());
	return 1;
}

static void on_datetimepicker_changed(uiDateTimePicker *d, void *data)
{
	callback(data, d);
}

int l_DateTimePickerTime(lua_State *L)
{
	struct tm t;
	uiDateTimePickerTime(CAST_ARG(1, DateTimePicker), &t);
	lua_newtable(L);
	lua_pushinteger(L, t.tm_year + 1900); lua_setfield(L, -2, "year");
	lua_pushinteger(L, t.tm_mon + 1);     lua_setfield(L, -2, "month");
	lua_pushinteger(L, t.tm_mday);        lua_setfield(L, -2, "day");
	lua_pushinteger(L, t.tm_hour);        lua_setfield(L, -2, "hour");
	lua_pushinteger(L, t.tm_min);         lua_setfield(L, -2, "min");
	lua_pushinteger(L, t.tm_sec);         lua_setfield(L, -2, "sec");
	lua_pushinteger(L, t.tm_wday);        lua_setfield(L, -2, "wday");
	return 1;
}

int l_DateTimePickerSetTime(lua_State *L)
{
	luaL_checktype(L, 2, LUA_TTABLE);
	struct tm t;
	memset(&t, 0, sizeof(t));
	lua_getfield(L, 2, "year");  t.tm_year = (int)lua_tointeger(L, -1) - 1900; lua_pop(L, 1);
	lua_getfield(L, 2, "month"); t.tm_mon  = (int)lua_tointeger(L, -1) - 1;    lua_pop(L, 1);
	lua_getfield(L, 2, "day");   t.tm_mday = (int)lua_tointeger(L, -1);        lua_pop(L, 1);
	lua_getfield(L, 2, "hour");  t.tm_hour = (int)lua_tointeger(L, -1);        lua_pop(L, 1);
	lua_getfield(L, 2, "min");   t.tm_min  = (int)lua_tointeger(L, -1);        lua_pop(L, 1);
	lua_getfield(L, 2, "sec");   t.tm_sec  = (int)lua_tointeger(L, -1);        lua_pop(L, 1);
	uiDateTimePickerSetTime(CAST_ARG(1, DateTimePicker), &t);
	RETURN_SELF;
}

int l_DateTimePickerOnChanged(lua_State *L)
{
	uiDateTimePickerOnChanged(CAST_ARG(1, DateTimePicker), on_datetimepicker_changed, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

static struct luaL_Reg meta_DateTimePicker[] = {
	{ "Time",                 l_DateTimePickerTime },
	{ "SetTime",              l_DateTimePickerSetTime },
	{ "OnChanged",            l_DateTimePickerOnChanged },
	{ NULL }
};




/*
 * Group
 */

int l_NewGroup(lua_State *L)
{
	CREATE_OBJECT(Group, uiNewGroup(
		luaL_checkstring(L, 1)
	));
	return 1;
}

int l_GroupTitle(lua_State *L)
{
	lua_pushstring(L, uiGroupTitle(CAST_ARG(1, Group)));
	return 1;
}

int l_GroupSetTitle(lua_State *L)
{
	const char *title = luaL_checkstring(L, 2);
	uiGroupSetTitle(CAST_ARG(1, Group), title);
	RETURN_SELF;
}

int l_GroupSetChild(lua_State *L)
{
	uiGroupSetChild(CAST_ARG(1, Group), CAST_ARG(2, Control));
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_pushboolean(L, 1);
	lua_settable(L, -3);
	RETURN_SELF;
}

int l_GroupMargined(lua_State *L)
{
	lua_pushnumber(L, uiGroupMargined(CAST_ARG(1, Group)));
	return 1;
}

int l_GroupSetMargined(lua_State *L)
{
	uiGroupSetMargined(CAST_ARG(1, Group), luaL_checknumber(L, 2));
	RETURN_SELF;
}

static struct luaL_Reg meta_Group[] = {
	{ "Title",                l_GroupTitle },
	{ "SetTitle",             l_GroupSetTitle },
	{ "SetChild",             l_GroupSetChild },
	{ "Margined",             l_GroupMargined },
	{ "SetMargined",          l_GroupSetMargined },
	{ NULL }
};


/*
 * Label
 */

int l_NewLabel(lua_State *L)
{
	CREATE_OBJECT(Label, uiNewLabel(
		luaL_checkstring(L, 1)
	));
	return 1;
}

int l_LabelText(lua_State *L)
{
	lua_pushstring(L, uiLabelText(CAST_ARG(1, Label)));
	return 1;
}

int l_LabelSetText(lua_State *L)
{
	uiLabelSetText(CAST_ARG(1, Label), luaL_checkstring(L, 2));
	RETURN_SELF;
}


static struct luaL_Reg meta_Label[] = {
	{ "Text",                 l_LabelText },
	{ "SetText",              l_LabelSetText },
	{ NULL }
};




/*
 * MultilineEntry
 */

int l_NewMultilineEntry(lua_State *L)
{
	CREATE_OBJECT(MultilineEntry, uiNewMultilineEntry());
	return 1;
}

int l_MultilineEntryText(lua_State *L)
{
	char *text = uiMultilineEntryText(CAST_ARG(1, MultilineEntry));
	lua_pushstring(L, text ? text : "");
	uiFreeText(text);
	return 1;
}

int l_MultilineEntrySetText(lua_State *L)
{
	uiMultilineEntrySetText(CAST_ARG(1, MultilineEntry), luaL_checkstring(L, 2));
	RETURN_SELF;
}

int l_MultilineEntryAppend(lua_State *L)
{
	uiMultilineEntryAppend(CAST_ARG(1, MultilineEntry), luaL_checkstring(L, 2));
	RETURN_SELF;
}

int l_MultilineEntryReadOnly(lua_State *L)
{
	lua_pushinteger(L, uiMultilineEntryReadOnly(CAST_ARG(1, MultilineEntry)));
	return 1;
}

int l_MultilineEntrySetReadOnly(lua_State *L)
{
	uiMultilineEntrySetReadOnly(CAST_ARG(1, MultilineEntry), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

static void on_multilineentry_changed(uiMultilineEntry *e, void *data)
{
	callback(data, e);
}

int l_MultilineEntryOnChanged(lua_State *L)
{
	uiMultilineEntryOnChanged(CAST_ARG(1, MultilineEntry), on_multilineentry_changed, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

static struct luaL_Reg meta_MultilineEntry[] = {
	{ "Text",                 l_MultilineEntryText },
	{ "SetText",              l_MultilineEntrySetText },
	{ "Append",               l_MultilineEntryAppend },
	{ "ReadOnly",             l_MultilineEntryReadOnly },
	{ "SetReadOnly",          l_MultilineEntrySetReadOnly },
	{ "OnChanged",            l_MultilineEntryOnChanged },
	{ NULL }
};


/*
 * NonWrappingMultilineEntry constructor (shares MultilineEntry meta)
 */

int l_NewNonWrappingMultilineEntry(lua_State *L)
{
	CREATE_OBJECT(MultilineEntry, uiNewNonWrappingMultilineEntry());
	return 1;
}


/*
 * ProgressBar
 */

int l_NewProgressBar(lua_State *L)
{
	CREATE_OBJECT(ProgressBar, uiNewProgressBar());
	return 1;
}

int l_ProgressBarSetValue(lua_State *L)
{
	double value = luaL_checknumber(L, 2);
	uiProgressBarSetValue(CAST_ARG(1, ProgressBar), value);
	RETURN_SELF;
}

int l_ProgressBarValue(lua_State *L)
{
	lua_pushinteger(L, uiProgressBarValue(CAST_ARG(1, ProgressBar)));
	return 1;
}

static struct luaL_Reg meta_ProgressBar[] = {
	{ "Value",                l_ProgressBarValue },
	{ "SetValue",             l_ProgressBarSetValue },
	{ NULL }
};


/*
 * RadioButtons
 */

int l_NewRadioButtons(lua_State *L)
{
	CREATE_OBJECT(RadioButtons, uiNewRadioButtons());
	return 1;
}


int l_RadioButtonsAppend(lua_State *L)
{
	int n = lua_gettop(L);
	int i;
	for(i=2; i<=n; i++) {
		const char *text = luaL_checkstring(L, i);
		uiRadioButtonsAppend(CAST_ARG(1, RadioButtons), text);
	}
	RETURN_SELF;
}


int l_RadioButtonsSelected(lua_State *L)
{
	lua_pushinteger(L, uiRadioButtonsSelected(CAST_ARG(1, RadioButtons)));
	return 1;
}

int l_RadioButtonsSetSelected(lua_State *L)
{
	uiRadioButtonsSetSelected(CAST_ARG(1, RadioButtons), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

static void on_radiobuttons_selected(uiRadioButtons *r, void *data)
{
	callback(data, r);
}

int l_RadioButtonsOnSelected(lua_State *L)
{
	uiRadioButtonsOnSelected(CAST_ARG(1, RadioButtons), on_radiobuttons_selected, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

static struct luaL_Reg meta_RadioButtons[] = {
	{ "Append",               l_RadioButtonsAppend },
	{ "Selected",             l_RadioButtonsSelected },
	{ "SetSelected",          l_RadioButtonsSetSelected },
	{ "OnSelected",           l_RadioButtonsOnSelected },
	{ NULL }
};




/*
 * Separator
 */

int l_NewHorizontalSeparator(lua_State *L)
{
	CREATE_OBJECT(Separator, uiNewHorizontalSeparator());
	return 1;
}

int l_NewVerticalSeparator(lua_State *L)
{
	CREATE_OBJECT(Separator, uiNewVerticalSeparator());
	return 1;
}

static struct luaL_Reg meta_Separator[] = {
	{ NULL }
};


/*
 * Slider
 */

int l_NewSlider(lua_State *L)
{
	CREATE_OBJECT(Slider, uiNewSlider(
		luaL_checknumber(L, 1),
		luaL_checknumber(L, 2)
	));
	return 1;
}

int l_SliderValue(lua_State *L)
{
	lua_pushnumber(L, uiSliderValue(CAST_ARG(1, Slider)));
	return 1;
}

int l_SliderSetValue(lua_State *L)
{
	double value = luaL_checknumber(L, 2);
	uiSliderSetValue(CAST_ARG(1, Slider), value);
	RETURN_SELF;
}

static void on_slider_changed(uiSlider *b, void *data)
{
	callback(data, b);
}

int l_SliderOnChanged(lua_State *L)
{
	uiSliderOnChanged(CAST_ARG(1, Slider), on_slider_changed, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

int l_SliderHasToolTip(lua_State *L)
{
	lua_pushboolean(L, uiSliderHasToolTip(CAST_ARG(1, Slider)));
	return 1;
}

int l_SliderSetHasToolTip(lua_State *L)
{
	uiSliderSetHasToolTip(CAST_ARG(1, Slider), lua_toboolean(L, 2));
	RETURN_SELF;
}

static void on_slider_released(uiSlider *b, void *data)
{
	callback(data, b);
}

int l_SliderOnReleased(lua_State *L)
{
	uiSliderOnReleased(CAST_ARG(1, Slider), on_slider_released, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

int l_SliderSetRange(lua_State *L)
{
	uiSliderSetRange(CAST_ARG(1, Slider), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
	RETURN_SELF;
}

static struct luaL_Reg meta_Slider[] = {
	{ "Value",                l_SliderValue },
	{ "SetValue",             l_SliderSetValue },
	{ "OnChanged",            l_SliderOnChanged },
	{ "HasToolTip",           l_SliderHasToolTip },
	{ "SetHasToolTip",        l_SliderSetHasToolTip },
	{ "OnReleased",           l_SliderOnReleased },
	{ "SetRange",             l_SliderSetRange },
	{ NULL }
};


/*
 * Spinbox
 */

int l_NewSpinbox(lua_State *L)
{
	CREATE_OBJECT(Spinbox, uiNewSpinbox(
		luaL_checknumber(L, 1),
		luaL_checknumber(L, 2)
	));
	return 1;
}

int l_SpinboxValue(lua_State *L)
{
	lua_pushnumber(L, uiSpinboxValue(CAST_ARG(1, Spinbox)));
	return 1;
}

int l_SpinboxSetValue(lua_State *L)
{
	double value = luaL_checknumber(L, 2);
	uiSpinboxSetValue(CAST_ARG(1, Spinbox), value);
	RETURN_SELF;
}

static void on_spinbox_changed(uiSpinbox *b, void *data)
{
	callback(data, b);
}

int l_SpinboxOnChanged(lua_State *L)
{
	uiSpinboxOnChanged(CAST_ARG(1, Spinbox), on_spinbox_changed, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

static struct luaL_Reg meta_Spinbox[] = {
	{ "Value",                l_SpinboxValue },
	{ "SetValue",             l_SpinboxSetValue },
	{ "OnChanged",            l_SpinboxOnChanged },
	{ NULL }
};


/*
 * Tab
 */

int l_NewTab(lua_State *L)
{
	CREATE_OBJECT(Tab, uiNewTab());
	return 1;
}

int l_TabAppend(lua_State *L)
{
	int n = lua_gettop(L);
	int i;
	for(i=2; i<=n; i+=2) {
		uiTabAppend(CAST_ARG(1, Tab), luaL_checkstring(L, i+0), CAST_ARG(i+1, Control));
		lua_getmetatable(L, 1);
		lua_pushvalue(L, i+1);
		luaL_ref(L, -2);
	}
	RETURN_SELF;
}

int l_TabInsertAt(lua_State *L)
{
	uiTabInsertAt(CAST_ARG(1, Tab), luaL_checkstring(L, 2), (int)luaL_checkinteger(L, 3), CAST_ARG(4, Control));
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 4);
	luaL_ref(L, -2);
	RETURN_SELF;
}

int l_TabDelete(lua_State *L)
{
	uiTabDelete(CAST_ARG(1, Tab), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

int l_TabNumPages(lua_State *L)
{
	lua_pushinteger(L, uiTabNumPages(CAST_ARG(1, Tab)));
	return 1;
}

int l_TabMargined(lua_State *L)
{
	lua_pushboolean(L, uiTabMargined(CAST_ARG(1, Tab), (int)luaL_checkinteger(L, 2)));
	return 1;
}

int l_TabSetMargined(lua_State *L)
{
	uiTabSetMargined(CAST_ARG(1, Tab), (int)luaL_checkinteger(L, 2), lua_toboolean(L, 3));
	RETURN_SELF;
}

int l_TabSelected(lua_State *L)
{
	lua_pushinteger(L, uiTabSelected(CAST_ARG(1, Tab)));
	return 1;
}

int l_TabSetSelected(lua_State *L)
{
	uiTabSetSelected(CAST_ARG(1, Tab), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

static struct luaL_Reg meta_Tab[] = {
	{ "Append",               l_TabAppend },
	{ "InsertAt",             l_TabInsertAt },
	{ "Delete",               l_TabDelete },
	{ "NumPages",             l_TabNumPages },
	{ "Margined",             l_TabMargined },
	{ "SetMargined",          l_TabSetMargined },
	{ "Selected",             l_TabSelected },
	{ "SetSelected",          l_TabSetSelected },
	{ NULL }
};

/*
 * Window
 */

static int on_window_closing_default(uiWindow *w, void *data)
{
	(void)data;
	fprintf(stderr, "[libuilua] on_window_closing_default: hwnd=%p\n",
		(void *)uiControlHandle(uiControl(w)));
	tracked_window_remove(w);
	uiControlDestroy(uiControl(w));
	return 0;
}

static int on_window_closing_lua(uiWindow *w, void *data)
{
	lua_State *L = data;
	fprintf(stderr, "[libuilua] on_window_closing_lua: hwnd=%p\n",
		(void *)uiControlHandle(uiControl(w)));
	int should_close = callback_boolean(L, w, 1);

	if(should_close) {
		fprintf(stderr, "[libuilua]   should_close=true, removing and destroying\n");
		tracked_window_remove(w);
		clear_callback_data(L, w);
		uiControlDestroy(uiControl(w));
	}

	return 0;
}

/*
 * If the user closes the main window while a tool-script window is still
 * open we must NOT call uiControlDestroy() from begin_app_shutdown because
 * the tool script may still be inside a nested MainStep() event loop.
 * Calling DestroyWindow() from a different call-stack frame can deadlock
 * the message pump.
 *
 * Instead we let uiQuit() cause all nested MainStep loops to exit
 * naturally.  When the Lua state is garbage-collected during
 * lua_close(), the GC calls the __gc metamethod which cleans up any
 * still-alive top-level windows here.
 */
int l_gc(lua_State *L)
{
	uiControl *control = CAST_ARG(1, Control);
	fprintf(stderr, "[libuilua] l_gc: control=%p parent=%p tracked=%d\n",
		(void *)control, uiControlParent(control), g_tracked_window_count);

	/* Only clean up top-level windows that are still in the tracker.
	 * Child controls (buttons, entries, etc.) are recursively destroyed
	 * by the parent's uiControlDestroy and appear here with a dangling
	 * parent=NULL — those must be skipped to avoid double-free. */
	if (uiControlParent(control) == NULL) {
		for (int i = 0; i < g_tracked_window_count; i++) {
			if (g_tracked_windows[i] == (uiWindow *)control) {
				fprintf(stderr, "[libuilua] l_gc: destroying tracked window hwnd=%p\n",
					(void *)uiControlHandle(control));
				tracked_window_remove((uiWindow *)control);
				clear_callback_data(L, control);
				uiControlDestroy(control);
				break;
			}
		}
	} else {
		fprintf(stderr, "[libuilua] l_gc: skipping child control (not a top-level window)\n");
	}
	return 0;
}

int l_NewWindow(lua_State *L)
{
	uiWindow *window = uiNewWindow(
		luaL_checkstring(L, 1),
		luaL_checknumber(L, 2),
		luaL_checknumber(L, 3),
		lua_toboolean(L, 4)
	);
	CREATE_OBJECT(Window, window);
	tracked_window_add(window);
	uiWindowOnClosing(window, on_window_closing_default, NULL);
	return 1;
}

int l_WindowSetChild(lua_State *L)
{
	uiWindowSetChild(CAST_ARG(1, Window), CAST_ARG(2, Control));
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_pushboolean(L, 1);
	lua_settable(L, -3);
	RETURN_SELF;
}

int l_WindowSetMargined(lua_State *L)
{
	uiWindowSetMargined(CAST_ARG(1, Window), luaL_checknumber(L, 2));
	RETURN_SELF;
}

int l_WindowOnClosing(lua_State *L)
{
	if(lua_isnoneornil(L, 2)) {
		clear_callback_data(L, CAST_ARG(1, Control));
		uiWindowOnClosing(CAST_ARG(1, Window), on_window_closing_default, NULL);
		RETURN_SELF;
	}

	luaL_checktype(L, 2, LUA_TFUNCTION);
	uiWindowOnClosing(CAST_ARG(1, Window), on_window_closing_lua, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

int l_WindowTitle(lua_State *L)
{
	char *text = uiWindowTitle(CAST_ARG(1, Window));
	lua_pushstring(L, text ? text : "");
	uiFreeText(text);
	return 1;
}

int l_WindowSetTitle(lua_State *L)
{
	uiWindowSetTitle(CAST_ARG(1, Window), luaL_checkstring(L, 2));
	RETURN_SELF;
}

int l_WindowPosition(lua_State *L)
{
	int x, y;
	uiWindowPosition(CAST_ARG(1, Window), &x, &y);
	lua_pushinteger(L, x);
	lua_pushinteger(L, y);
	return 2;
}

int l_WindowSetPosition(lua_State *L)
{
	uiWindowSetPosition(CAST_ARG(1, Window), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
	RETURN_SELF;
}

int l_WindowContentSize(lua_State *L)
{
	int w, h;
	uiWindowContentSize(CAST_ARG(1, Window), &w, &h);
	lua_pushinteger(L, w);
	lua_pushinteger(L, h);
	return 2;
}

int l_WindowSetContentSize(lua_State *L)
{
	uiWindowSetContentSize(CAST_ARG(1, Window), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
	RETURN_SELF;
}

int l_WindowFullscreen(lua_State *L)
{
	lua_pushboolean(L, uiWindowFullscreen(CAST_ARG(1, Window)));
	return 1;
}

int l_WindowSetFullscreen(lua_State *L)
{
	uiWindowSetFullscreen(CAST_ARG(1, Window), lua_toboolean(L, 2));
	RETURN_SELF;
}

int l_WindowBorderless(lua_State *L)
{
	lua_pushboolean(L, uiWindowBorderless(CAST_ARG(1, Window)));
	return 1;
}

int l_WindowSetBorderless(lua_State *L)
{
	uiWindowSetBorderless(CAST_ARG(1, Window), lua_toboolean(L, 2));
	RETURN_SELF;
}

int l_WindowResizeable(lua_State *L)
{
	lua_pushboolean(L, uiWindowResizeable(CAST_ARG(1, Window)));
	return 1;
}

int l_WindowSetResizeable(lua_State *L)
{
	uiWindowSetResizeable(CAST_ARG(1, Window), lua_toboolean(L, 2));
	RETURN_SELF;
}

int l_WindowFocused(lua_State *L)
{
	lua_pushboolean(L, uiWindowFocused(CAST_ARG(1, Window)));
	return 1;
}

int l_WindowMargined(lua_State *L)
{
	lua_pushboolean(L, uiWindowMargined(CAST_ARG(1, Window)));
	return 1;
}

static struct luaL_Reg meta_Window[] = {
	{ "Title",               l_WindowTitle },
	{ "SetTitle",            l_WindowSetTitle },
	{ "Position",            l_WindowPosition },
	{ "SetPosition",         l_WindowSetPosition },
	{ "ContentSize",         l_WindowContentSize },
	{ "SetContentSize",      l_WindowSetContentSize },
	{ "Fullscreen",          l_WindowFullscreen },
	{ "SetFullscreen",       l_WindowSetFullscreen },
	{ "Borderless",          l_WindowBorderless },
	{ "SetBorderless",       l_WindowSetBorderless },
	{ "Resizeable",          l_WindowResizeable },
	{ "SetResizeable",       l_WindowSetResizeable },
	{ "Focused",             l_WindowFocused },
	{ "Margined",            l_WindowMargined },
	{ "SetChild",            l_WindowSetChild },
	{ "SetMargined",         l_WindowSetMargined },
	{ "OnClosing",           l_WindowOnClosing },
	{ "Show",                l_ControlShow },
	{ "Hide",                l_ControlHide },
	{ "Enable",              l_ControlEnable },
	{ "Disable",             l_ControlDisable },
	{ "Visible",             l_ControlVisible },
	{ "Enabled",             l_ControlEnabled },
	{ "Destroy",             l_ControlDestroy },
	{ NULL }
};



/*
 * ColorButton
 */

int l_NewColorButton(lua_State *L)
{
	CREATE_OBJECT(ColorButton, uiNewColorButton());
	return 1;
}

int l_ColorButtonColor(lua_State *L)
{
	double r, g, b, a;
	uiColorButtonColor(CAST_ARG(1, ColorButton), &r, &g, &b, &a);
	lua_pushnumber(L, r);
	lua_pushnumber(L, g);
	lua_pushnumber(L, b);
	lua_pushnumber(L, a);
	return 4;
}

int l_ColorButtonSetColor(lua_State *L)
{
	uiColorButtonSetColor(CAST_ARG(1, ColorButton),
		luaL_checknumber(L, 2),
		luaL_checknumber(L, 3),
		luaL_checknumber(L, 4),
		luaL_checknumber(L, 5));
	RETURN_SELF;
}

static void on_colorbutton_changed(uiColorButton *b, void *data)
{
	callback(data, b);
}

int l_ColorButtonOnChanged(lua_State *L)
{
	uiColorButtonOnChanged(CAST_ARG(1, ColorButton), on_colorbutton_changed, L);
	create_callback_data(L, 1, 2, 3);
	RETURN_SELF;
}

static struct luaL_Reg meta_ColorButton[] = {
	{ "Color",                l_ColorButtonColor },
	{ "SetColor",             l_ColorButtonSetColor },
	{ "OnChanged",            l_ColorButtonOnChanged },
	{ NULL }
};


/*
 * Form
 */

int l_NewForm(lua_State *L)
{
	CREATE_OBJECT(Form, uiNewForm());
	return 1;
}

int l_FormAppend(lua_State *L)
{
	int stretchy = lua_toboolean(L, 4);
	uiFormAppend(CAST_ARG(1, Form), luaL_checkstring(L, 2), CAST_ARG(3, Control), stretchy);
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 3);
	luaL_ref(L, -2);
	RETURN_SELF;
}

int l_FormNumChildren(lua_State *L)
{
	lua_pushinteger(L, uiFormNumChildren(CAST_ARG(1, Form)));
	return 1;
}

int l_FormDelete(lua_State *L)
{
	uiFormDelete(CAST_ARG(1, Form), (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

int l_FormPadded(lua_State *L)
{
	lua_pushboolean(L, uiFormPadded(CAST_ARG(1, Form)));
	return 1;
}

int l_FormSetPadded(lua_State *L)
{
	uiFormSetPadded(CAST_ARG(1, Form), lua_toboolean(L, 2));
	RETURN_SELF;
}

static struct luaL_Reg meta_Form[] = {
	{ "Append",               l_FormAppend },
	{ "NumChildren",          l_FormNumChildren },
	{ "Delete",               l_FormDelete },
	{ "Padded",               l_FormPadded },
	{ "SetPadded",            l_FormSetPadded },
	{ NULL }
};


/*
 * Grid
 */

int l_NewGrid(lua_State *L)
{
	CREATE_OBJECT(Grid, uiNewGrid());
	return 1;
}

int l_GridAppend(lua_State *L)
{
	/* GridAppend(grid, child, left, top, xspan, yspan, hexpand, halign, vexpand, valign) */
	uiGridAppend(CAST_ARG(1, Grid), CAST_ARG(2, Control),
		(int)luaL_checkinteger(L, 3),
		(int)luaL_checkinteger(L, 4),
		(int)luaL_optinteger(L, 5, 1),
		(int)luaL_optinteger(L, 6, 1),
		lua_toboolean(L, 7),
		(uiAlign)luaL_optinteger(L, 8, uiAlignFill),
		lua_toboolean(L, 9),
		(uiAlign)luaL_optinteger(L, 10, uiAlignFill));
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	luaL_ref(L, -2);
	RETURN_SELF;
}

int l_GridPadded(lua_State *L)
{
	lua_pushboolean(L, uiGridPadded(CAST_ARG(1, Grid)));
	return 1;
}

int l_GridSetPadded(lua_State *L)
{
	uiGridSetPadded(CAST_ARG(1, Grid), lua_toboolean(L, 2));
	RETURN_SELF;
}

static struct luaL_Reg meta_Grid[] = {
	{ "Append",               l_GridAppend },
	{ "Padded",               l_GridPadded },
	{ "SetPadded",            l_GridSetPadded },
	{ NULL }
};


/*
 * TableModel + Table
 */

static int lmh_NumColumns(uiTableModelHandler *mh, uiTableModel *m)
{
	LuaTableModelHandler *lmh = (LuaTableModelHandler *)mh;
	lua_State *L = lmh->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->handler_ref);
	lua_getfield(L, -1, "NumColumns");
	lua_remove(L, -2);
	if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->model_udata_ref);
	if (lua_pcall(L, 1, 1, 0) != LUA_OK) { lua_pop(L, 1); return 0; }
	int n = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return n;
}

static uiTableValueType lmh_ColumnType(uiTableModelHandler *mh, uiTableModel *m, int col)
{
	LuaTableModelHandler *lmh = (LuaTableModelHandler *)mh;
	lua_State *L = lmh->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->handler_ref);
	lua_getfield(L, -1, "ColumnType");
	lua_remove(L, -2);
	if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return uiTableValueTypeString; }
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->model_udata_ref);
	lua_pushinteger(L, col);
	if (lua_pcall(L, 2, 1, 0) != LUA_OK) { lua_pop(L, 1); return uiTableValueTypeString; }
	uiTableValueType t = (uiTableValueType)(int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return t;
}

static int lmh_NumRows(uiTableModelHandler *mh, uiTableModel *m)
{
	LuaTableModelHandler *lmh = (LuaTableModelHandler *)mh;
	lua_State *L = lmh->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->handler_ref);
	lua_getfield(L, -1, "NumRows");
	lua_remove(L, -2);
	if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->model_udata_ref);
	if (lua_pcall(L, 1, 1, 0) != LUA_OK) { lua_pop(L, 1); return 0; }
	int n = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return n;
}

static uiImage *g_default_image = NULL;

static uiImage *get_default_image(void)
{
	if (!g_default_image) {
		unsigned char pixel[4] = {0, 0, 0, 0};
		g_default_image = uiNewImage(1, 1);
		uiImageAppend(g_default_image, pixel, 1, 1, 4);
	}
	return g_default_image;
}

static uiTableValue *lmh_CellValue(uiTableModelHandler *mh, uiTableModel *m, int row, int col)
{
	LuaTableModelHandler *lmh = (LuaTableModelHandler *)mh;
	lua_State *L = lmh->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->handler_ref);
	lua_getfield(L, -1, "CellValue");
	lua_remove(L, -2);
	if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return NULL; }
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->model_udata_ref);
	lua_pushinteger(L, row);
	lua_pushinteger(L, col);
	if (lua_pcall(L, 3, 1, 0) != LUA_OK) { lua_pop(L, 1); return NULL; }
	uiTableValue *tv = NULL;
	int ltype = lua_type(L, -1);
	if (ltype == LUA_TSTRING) {
		tv = uiNewTableValueString(lua_tostring(L, -1));
	} else if (ltype == LUA_TNUMBER) {
		tv = uiNewTableValueInt((int)lua_tointeger(L, -1));
	} else if (ltype == LUA_TBOOLEAN) {
		tv = uiNewTableValueInt(lua_toboolean(L, -1) ? 1 : 0);
	} else if (ltype == LUA_TTABLE) {
		lua_getfield(L, -1, "r"); double r = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, -1, "g"); double g = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, -1, "b"); double b = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, -1, "a"); double a = lua_tonumber(L, -1); lua_pop(L, 1);
		tv = uiNewTableValueColor(r, g, b, a);
	} else if (ltype == LUA_TUSERDATA) {
		struct imgwrap *iw = (struct imgwrap *)luaL_testudata(L, -1, "libui.Image");
		if (iw && iw->img) {
			tv = uiNewTableValueImage(iw->img);
		}
	}
	/* Fallback: if handler returned nil/wrong type for an image column,
	   use a 1x1 transparent placeholder to avoid crashing libui-ng. */
	if (!tv && mh->ColumnType) {
		uiTableValueType ct = mh->ColumnType(mh, m, col);
		if (ct == uiTableValueTypeImage) {
			tv = uiNewTableValueImage(get_default_image());
		}
	}
	lua_pop(L, 1);
	return tv;
}

static void lmh_SetCellValue(uiTableModelHandler *mh, uiTableModel *m, int row, int col,
                              const uiTableValue *v)
{
	LuaTableModelHandler *lmh = (LuaTableModelHandler *)mh;
	lua_State *L = lmh->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->handler_ref);
	lua_getfield(L, -1, "SetCellValue");
	lua_remove(L, -2);
	if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
	lua_rawgeti(L, LUA_REGISTRYINDEX, lmh->model_udata_ref);
	lua_pushinteger(L, row);
	lua_pushinteger(L, col);
	if (v == NULL) {
		lua_pushnil(L);
	} else {
		switch (uiTableValueGetType(v)) {
			case uiTableValueTypeString:
				lua_pushstring(L, uiTableValueString(v));
				break;
			case uiTableValueTypeInt:
				lua_pushinteger(L, uiTableValueInt(v));
				break;
			case uiTableValueTypeColor: {
				double r, g, b, a;
				uiTableValueColor(v, &r, &g, &b, &a);
				lua_newtable(L);
				lua_pushnumber(L, r); lua_setfield(L, -2, "r");
				lua_pushnumber(L, g); lua_setfield(L, -2, "g");
				lua_pushnumber(L, b); lua_setfield(L, -2, "b");
				lua_pushnumber(L, a); lua_setfield(L, -2, "a");
				break;
			}
			default:
				lua_pushnil(L);
		}
	}
	if (lua_pcall(L, 4, 0, 0) != LUA_OK) lua_pop(L, 1);
}

static int l_TableModelGC(lua_State *L)
{
	struct tmwrap *tw = (struct tmwrap *)lua_touserdata(L, 1);
	if (tw->model) {
		uiFreeTableModel(tw->model);
		tw->model = NULL;
	}
	if (tw->lmh) {
		luaL_unref(L, LUA_REGISTRYINDEX, tw->lmh->handler_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, tw->lmh->model_udata_ref);
		free(tw->lmh);
		tw->lmh = NULL;
	}
	return 0;
}

int l_NewTableModel(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	LuaTableModelHandler *lmh = (LuaTableModelHandler *)malloc(sizeof(LuaTableModelHandler));
	memset(lmh, 0, sizeof(LuaTableModelHandler));
	lmh->L = L;
	lmh->base.NumColumns = lmh_NumColumns;
	lmh->base.ColumnType = lmh_ColumnType;
	lmh->base.NumRows    = lmh_NumRows;
	lmh->base.CellValue  = lmh_CellValue;
	lmh->base.SetCellValue = lmh_SetCellValue;

	/* Keep reference to handler table */
	lua_pushvalue(L, 1);
	lmh->handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	/* Create the userdata */
	struct tmwrap *tw = (struct tmwrap *)lua_newuserdata(L, sizeof(struct tmwrap));
	tw->model = NULL;
	tw->lmh = lmh;

	/* Set model_udata_ref to point to this userdata so callbacks can pass it */
	lua_pushvalue(L, -1);
	lmh->model_udata_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	/* Now create the actual model */
	tw->model = uiNewTableModel(&lmh->base);

	/* Apply metatable */
	luaL_getmetatable(L, "libui.TableModel");
	lua_setmetatable(L, -2);

	return 1;
}

int l_TableModelRowInserted(lua_State *L)
{
	struct tmwrap *tw = (struct tmwrap *)lua_touserdata(L, 1);
	uiTableModelRowInserted(tw->model, (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

int l_TableModelRowChanged(lua_State *L)
{
	struct tmwrap *tw = (struct tmwrap *)lua_touserdata(L, 1);
	uiTableModelRowChanged(tw->model, (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

int l_TableModelRowDeleted(lua_State *L)
{
	struct tmwrap *tw = (struct tmwrap *)lua_touserdata(L, 1);
	uiTableModelRowDeleted(tw->model, (int)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

int l_TableModelReset(lua_State *L)
{
	struct tmwrap *tw = (struct tmwrap *)lua_touserdata(L, 1);
	uiTableModelReset(tw->model);
	RETURN_SELF;
}

static struct luaL_Reg meta_TableModel[] = {
	{ "RowInserted",          l_TableModelRowInserted },
	{ "RowChanged",           l_TableModelRowChanged },
	{ "RowDeleted",           l_TableModelRowDeleted },
	{ "Reset",                l_TableModelReset },
	{ NULL }
};


/* Table callbacks keyed by "libuilua:te:<ptr>:<event>" */
static void store_table_event(lua_State *L, void *t, const char *event, int fn_idx)
{
	char key[128];
	snprintf(key, sizeof(key), "libuilua:te:%p:%s", t, event);
	lua_pushstring(L, key);
	lua_pushvalue(L, fn_idx);
	lua_settable(L, LUA_REGISTRYINDEX);
}

static void table_row_click_cb(uiTable *t, int row, void *data)
{
	lua_State *L = data;
	char key[128];
	snprintf(key, sizeof(key), "libuilua:te:%p:rowclicked", (void*)t);
	lua_pushstring(L, key);
	lua_gettable(L, LUA_REGISTRYINDEX);
	if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
	lua_pushinteger(L, row);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) lua_pop(L, 1);
}

static void table_row_dblclick_cb(uiTable *t, int row, void *data)
{
	lua_State *L = data;
	char key[128];
	snprintf(key, sizeof(key), "libuilua:te:%p:rowdblclicked", (void*)t);
	lua_pushstring(L, key);
	lua_gettable(L, LUA_REGISTRYINDEX);
	if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
	lua_pushinteger(L, row);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) lua_pop(L, 1);
}

static void table_selection_changed_cb(uiTable *t, void *data)
{
	lua_State *L = data;
	char key[128];
	snprintf(key, sizeof(key), "libuilua:te:%p:selchanged", (void*)t);
	lua_pushstring(L, key);
	lua_gettable(L, LUA_REGISTRYINDEX);
	if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) lua_pop(L, 1);
}

int l_NewTable(lua_State *L)
{
	/* NewTable(model [, rowBgColorModelColumn]) */
	luaL_checktype(L, 1, LUA_TUSERDATA);
	struct tmwrap *tw = (struct tmwrap *)lua_touserdata(L, 1);
	uiTableParams params;
	params.Model = tw->model;
	params.RowBackgroundColorModelColumn = (int)luaL_optinteger(L, 2, -1);

	struct wrap *w = (struct wrap *)lua_newuserdata(L, sizeof(struct wrap));
	w->control = uiControl(uiNewTable(&params));
	lua_newtable(L);
	luaL_getmetatable(L, "libui.Table");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, l_gc);
	lua_setfield(L, -2, "__gc");
	/* Keep model reference alive */
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "_model");
	lua_setmetatable(L, -2);
	return 1;
}

static int l_ImageGC(lua_State *L)
{
	struct imgwrap *iw = (struct imgwrap *)lua_touserdata(L, 1);
	if (iw && iw->img) {
		uiFreeImage(iw->img);
		iw->img = NULL;
	}
	return 0;
}

static int l_NewImage(lua_State *L)
{
	double w = luaL_checknumber(L, 1);
	double h = luaL_checknumber(L, 2);
	uiImage *img = uiNewImage(w, h);
	struct imgwrap *iw = (struct imgwrap *)lua_newuserdata(L, sizeof(struct imgwrap));
	iw->img = img;
	luaL_getmetatable(L, "libui.Image");
	lua_setmetatable(L, -2);
	return 1;
}

static int l_LoadImageFromMemory(lua_State *L)
{
	size_t data_len;
	const unsigned char *data = (const unsigned char *)luaL_checklstring(L, 1, &data_len);
	int w, h, channels;
	unsigned char *pixels = stbi_load_from_memory(data, (int)data_len, &w, &h, &channels, 4);
	if (!pixels) {
		lua_pushnil(L);
		lua_pushstring(L, stbi_failure_reason());
		return 2;
	}
	int stride = w * 4;
	unsigned char *premul = (unsigned char *)malloc((size_t)(h * stride));
	if (!premul) {
		stbi_image_free(pixels);
		lua_pushnil(L);
		lua_pushstring(L, "out of memory");
		return 2;
	}
	for (int i = 0; i < w * h; i++) {
		unsigned char r = pixels[i * 4 + 0];
		unsigned char g = pixels[i * 4 + 1];
		unsigned char b = pixels[i * 4 + 2];
		unsigned char a = pixels[i * 4 + 3];
		premul[i * 4 + 0] = (unsigned char)((unsigned)r * a / 255);
		premul[i * 4 + 1] = (unsigned char)((unsigned)g * a / 255);
		premul[i * 4 + 2] = (unsigned char)((unsigned)b * a / 255);
		premul[i * 4 + 3] = a;
	}
	uiImage *img = uiNewImage((double)w, (double)h);
	uiImageAppend(img, premul, w, h, stride);
	free(premul);
	stbi_image_free(pixels);
	struct imgwrap *iw = (struct imgwrap *)lua_newuserdata(L, sizeof(struct imgwrap));
	iw->img = img;
	luaL_getmetatable(L, "libui.Image");
	lua_setmetatable(L, -2);
	return 1;
}

static int l_ImageAppend(lua_State *L)
{
	struct imgwrap *iw = (struct imgwrap *)luaL_checkudata(L, 1, "libui.Image");
	if (!iw || !iw->img) return luaL_error(L, "invalid Image");
	size_t pixels_len;
	const unsigned char *raw = (const unsigned char *)luaL_checklstring(L, 2, &pixels_len);
	int w = (int)luaL_checkinteger(L, 3);
	int h = (int)luaL_checkinteger(L, 4);
	if (w <= 0 || h <= 0) return luaL_error(L, "invalid image dimensions");
	size_t expected = (size_t)(w * h * 4);
	if (pixels_len < expected) return luaL_error(L, "pixel data too small");
	int byteStride = w * 4;
	uiImageAppend(iw->img, (void *)raw, w, h, byteStride);
	RETURN_SELF;
}

/* === StaticImage widget ============================================== */

static void si_onDraw(uiAreaHandler *ah, uiArea *a, uiAreaDrawParams *p)
{
	struct si_handler *h = (struct si_handler *)ah;
	if (!h->img || !p->Context) return;
	if (p->AreaWidth <= 0 || p->AreaHeight <= 0) return;
	if (h->img_w <= 0 || h->img_h <= 0) return;

	double scale_w = p->AreaWidth / (double)h->img_w;
	double scale_h = p->AreaHeight / (double)h->img_h;
	double scale = (scale_w < scale_h) ? scale_w : scale_h;
	double w = h->img_w * scale;
	double hh = h->img_h * scale;
	double x = (p->AreaWidth - w) / 2.0;
	double y = (p->AreaHeight - hh) / 2.0;

	uiDrawBitmap(p->Context, h->img, x, y, w, hh);
}

static void si_onMouseEvent(uiAreaHandler *ah, uiArea *a, uiAreaMouseEvent *e)
{
	(void)ah; (void)a; (void)e;
}

static void si_onMouseCrossed(uiAreaHandler *ah, uiArea *a, int left)
{
	(void)ah; (void)a; (void)left;
}

static void si_onDragBroken(uiAreaHandler *ah, uiArea *a)
{
	(void)ah; (void)a;
}

static int si_onKeyEvent(uiAreaHandler *ah, uiArea *a, uiAreaKeyEvent *e)
{
	(void)ah; (void)a; (void)e;
	return 0;
}

static int si_gc(lua_State *L)
{
	struct si_lua *s = (struct si_lua *)lua_touserdata(L, 1);
	if (s->h) {
		if (s->h->img) uiFreeImage(s->h->img);
		free(s->h->pixels);
		free(s->h);
		s->h = NULL;
	}
	return 0;
}

static void si_update_image(struct si_handler *h)
{
	if (h->img) { uiFreeImage(h->img); h->img = NULL; }
	if (!h->pixels || h->pw <= 0 || h->ph <= 0) return;
	h->img = uiNewImage((double)h->pw, (double)h->ph);
	uiImageAppend(h->img, h->pixels, h->pw, h->ph, h->pw * 4);
	h->img_w = h->pw;
	h->img_h = h->ph;
}

static int l_NewStaticImage(lua_State *L)
{
	struct si_lua *s = (struct si_lua *)lua_newuserdata(L, sizeof(struct si_lua));
	memset(s, 0, sizeof(*s));
	s->h = (struct si_handler *)malloc(sizeof(struct si_handler));
	memset(s->h, 0, sizeof(*s->h));
	s->h->handler.Draw = si_onDraw;
	s->h->handler.MouseEvent = si_onMouseEvent;
	s->h->handler.MouseCrossed = si_onMouseCrossed;
	s->h->handler.DragBroken = si_onDragBroken;
	s->h->handler.KeyEvent = si_onKeyEvent;
	s->control = uiControl(uiNewArea(&s->h->handler));

	lua_newtable(L);
	luaL_getmetatable(L, "libui.StaticImage");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, si_gc);
	lua_setfield(L, -2, "__gc");
	lua_setmetatable(L, -2);
	return 1;
}

static int l_StaticImageSetImageFromMemory(lua_State *L)
{
	struct si_lua *s = (struct si_lua *)lua_touserdata(L, 1);
	if (!s->h) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "SetImageFromMemory: widget destroyed");
		return 2;
	}
	size_t data_len;
	const unsigned char *data = (const unsigned char *)luaL_checklstring(L, 2, &data_len);
	int w, h, channels;
	unsigned char *pixels = stbi_load_from_memory(data, (int)data_len, &w, &h, &channels, 4);
	if (!pixels) {
		lua_pushboolean(L, 0);
		lua_pushfstring(L, "SetImageFromMemory: %s", stbi_failure_reason());
		return 2;
	}
	if (w > 4096 || h > 4096) {
		stbi_image_free(pixels);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "SetImageFromMemory: image too large (max 4096x4096)");
		return 2;
	}
	int npixels = w * h;
	for (int i = 0; i < npixels; i++) {
		unsigned char r = pixels[i * 4 + 0];
		unsigned char g = pixels[i * 4 + 1];
		unsigned char b = pixels[i * 4 + 2];
		unsigned char a = pixels[i * 4 + 3];
		pixels[i * 4 + 0] = (unsigned char)((unsigned)r * a / 255);
		pixels[i * 4 + 1] = (unsigned char)((unsigned)g * a / 255);
		pixels[i * 4 + 2] = (unsigned char)((unsigned)b * a / 255);
	}
	free(s->h->pixels);
	s->h->pixels = pixels;
	s->h->pw = w;
	s->h->ph = h;
	si_update_image(s->h);
	uiAreaQueueRedrawAll((uiArea *)s->control);
	lua_pushboolean(L, 1);
	return 1;
}

static int l_StaticImageSetImageFromFile(lua_State *L)
{
	struct si_lua *s = (struct si_lua *)lua_touserdata(L, 1);
	const char *path = luaL_checkstring(L, 2);

	FILE *f = fopen(path, "rb");
	if (!f) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "SetImageFromFile: cannot open file");
		return 2;
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsize <= 0) { fclose(f); lua_pushboolean(L, 0); lua_pushstring(L, "SetImageFromFile: empty file"); return 2; }

	unsigned char *fdata = (unsigned char *)malloc((size_t)fsize);
	if (!fdata || (long)fread(fdata, 1, (size_t)fsize, f) != fsize) {
		free(fdata); fclose(f);
		lua_pushboolean(L, 0); lua_pushstring(L, "SetImageFromFile: read error"); return 2;
	}
	fclose(f);

	int w, h, channels;
	unsigned char *pixels = stbi_load_from_memory(fdata, (int)fsize, &w, &h, &channels, 4);
	free(fdata);
	if (!pixels) {
		lua_pushboolean(L, 0);
		lua_pushfstring(L, "SetImageFromFile: %s", stbi_failure_reason());
		return 2;
	}
	for (int i = 0; i < w * h; i++) {
		unsigned char r = pixels[i * 4 + 0];
		unsigned char g = pixels[i * 4 + 1];
		unsigned char b = pixels[i * 4 + 2];
		unsigned char a = pixels[i * 4 + 3];
		pixels[i * 4 + 0] = (unsigned char)((unsigned)r * a / 255);
		pixels[i * 4 + 1] = (unsigned char)((unsigned)g * a / 255);
		pixels[i * 4 + 2] = (unsigned char)((unsigned)b * a / 255);
	}
	free(s->h->pixels);
	s->h->pixels = pixels;
	s->h->pw = w;
	s->h->ph = h;
	si_update_image(s->h);
	uiAreaQueueRedrawAll((uiArea *)s->control);
	lua_pushboolean(L, 1);
	return 1;
}

static int l_StaticImageClear(lua_State *L)
{
	struct si_lua *s = (struct si_lua *)lua_touserdata(L, 1);
	free(s->h->pixels); s->h->pixels = NULL;
	s->h->pw = 0; s->h->ph = 0;
	if (s->h->img) { uiFreeImage(s->h->img); s->h->img = NULL; }
	uiAreaQueueRedrawAll((uiArea *)s->control);
	lua_pushboolean(L, 1);
	return 1;
}

static struct luaL_Reg meta_StaticImage[] = {
	{ "SetImageFromMemory",    l_StaticImageSetImageFromMemory },
	{ "SetImageFromFile",      l_StaticImageSetImageFromFile },
	{ "Clear",                 l_StaticImageClear },
	{ NULL }
};

int l_TableAppendTextColumn(lua_State *L)
{
	int colorModelColumn = (int)luaL_optinteger(L, 5, -1);
	uiTableTextColumnOptionalParams tp;
	tp.ColorModelColumn = colorModelColumn;
	uiTableAppendTextColumn(CAST_ARG(1, Table),
		luaL_checkstring(L, 2),
		(int)luaL_checkinteger(L, 3),
		(int)luaL_checkinteger(L, 4),
		colorModelColumn >= 0 ? &tp : NULL);
	RETURN_SELF;
}

int l_TableAppendCheckboxColumn(lua_State *L)
{
	uiTableAppendCheckboxColumn(CAST_ARG(1, Table),
		luaL_checkstring(L, 2),
		(int)luaL_checkinteger(L, 3),
		(int)luaL_checkinteger(L, 4));
	RETURN_SELF;
}

int l_TableAppendCheckboxTextColumn(lua_State *L)
{
	int colorModelColumn = (int)luaL_optinteger(L, 7, -1);
	uiTableTextColumnOptionalParams tp;
	tp.ColorModelColumn = colorModelColumn;
	uiTableAppendCheckboxTextColumn(CAST_ARG(1, Table),
		luaL_checkstring(L, 2),
		(int)luaL_checkinteger(L, 3),
		(int)luaL_checkinteger(L, 4),
		(int)luaL_checkinteger(L, 5),
		(int)luaL_checkinteger(L, 6),
		colorModelColumn >= 0 ? &tp : NULL);
	RETURN_SELF;
}

int l_TableAppendProgressBarColumn(lua_State *L)
{
	uiTableAppendProgressBarColumn(CAST_ARG(1, Table),
		luaL_checkstring(L, 2),
		(int)luaL_checkinteger(L, 3));
	RETURN_SELF;
}

int l_TableAppendButtonColumn(lua_State *L)
{
	uiTableAppendButtonColumn(CAST_ARG(1, Table),
		luaL_checkstring(L, 2),
		(int)luaL_checkinteger(L, 3),
		(int)luaL_checkinteger(L, 4));
	RETURN_SELF;
}

int l_TableAppendImageColumn(lua_State *L)
{
	uiTableAppendImageColumn(CAST_ARG(1, Table),
		luaL_checkstring(L, 2),
		(int)luaL_checkinteger(L, 3));
	RETURN_SELF;
}

int l_TableAppendImageTextColumn(lua_State *L)
{
	int colorModelColumn = (int)luaL_optinteger(L, 6, -1);
	uiTableTextColumnOptionalParams tp;
	tp.ColorModelColumn = colorModelColumn;
	uiTableAppendImageTextColumn(CAST_ARG(1, Table),
		luaL_checkstring(L, 2),
		(int)luaL_checkinteger(L, 3),
		(int)luaL_checkinteger(L, 4),
		(int)luaL_checkinteger(L, 5),
		colorModelColumn >= 0 ? &tp : NULL);
	RETURN_SELF;
}

int l_TableHeaderVisible(lua_State *L)
{
	lua_pushboolean(L, uiTableHeaderVisible(CAST_ARG(1, Table)));
	return 1;
}

int l_TableHeaderSetVisible(lua_State *L)
{
	uiTableHeaderSetVisible(CAST_ARG(1, Table), lua_toboolean(L, 2));
	RETURN_SELF;
}

int l_TableOnRowClicked(lua_State *L)
{
	luaL_checktype(L, 2, LUA_TFUNCTION);
	store_table_event(L, CAST_ARG(1, Table), "rowclicked", 2);
	uiTableOnRowClicked(CAST_ARG(1, Table), table_row_click_cb, L);
	RETURN_SELF;
}

int l_TableOnRowDoubleClicked(lua_State *L)
{
	luaL_checktype(L, 2, LUA_TFUNCTION);
	store_table_event(L, CAST_ARG(1, Table), "rowdblclicked", 2);
	uiTableOnRowDoubleClicked(CAST_ARG(1, Table), table_row_dblclick_cb, L);
	RETURN_SELF;
}

int l_TableOnSelectionChanged(lua_State *L)
{
	luaL_checktype(L, 2, LUA_TFUNCTION);
	store_table_event(L, CAST_ARG(1, Table), "selchanged", 2);
	uiTableOnSelectionChanged(CAST_ARG(1, Table), table_selection_changed_cb, L);
	RETURN_SELF;
}

int l_TableGetSelectionMode(lua_State *L)
{
	lua_pushinteger(L, (int)uiTableGetSelectionMode(CAST_ARG(1, Table)));
	return 1;
}

int l_TableSetSelectionMode(lua_State *L)
{
	uiTableSetSelectionMode(CAST_ARG(1, Table), (uiTableSelectionMode)luaL_checkinteger(L, 2));
	RETURN_SELF;
}

int l_TableGetSelection(lua_State *L)
{
	uiTableSelection *sel = uiTableGetSelection(CAST_ARG(1, Table));
	lua_newtable(L);
	int i;
	for (i = 0; i < sel->NumRows; i++) {
		lua_pushinteger(L, sel->Rows[i]);
		lua_rawseti(L, -2, i + 1);
	}
	uiFreeTableSelection(sel);
	return 1;
}

int l_TableSetSelection(lua_State *L)
{
	luaL_checktype(L, 2, LUA_TTABLE);
	int n = (int)lua_rawlen(L, 2);
	uiTableSelection sel;
	sel.NumRows = n;
	int *rows = n > 0 ? (int *)malloc(n * sizeof(int)) : NULL;
	sel.Rows = rows;
	int i;
	for (i = 0; i < n; i++) {
		lua_rawgeti(L, 2, i + 1);
		rows[i] = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
	}
	uiTableSetSelection(CAST_ARG(1, Table), &sel);
	if (rows) free(rows);
	RETURN_SELF;
}

int l_TableColumnWidth(lua_State *L)
{
	lua_pushinteger(L, uiTableColumnWidth(CAST_ARG(1, Table), (int)luaL_checkinteger(L, 2)));
	return 1;
}

int l_TableColumnSetWidth(lua_State *L)
{
	uiTableColumnSetWidth(CAST_ARG(1, Table), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
	RETURN_SELF;
}

static struct luaL_Reg meta_Table[] = {
	{ "AppendTextColumn",         l_TableAppendTextColumn },
	{ "AppendCheckboxColumn",     l_TableAppendCheckboxColumn },
	{ "AppendCheckboxTextColumn", l_TableAppendCheckboxTextColumn },
	{ "AppendProgressBarColumn",  l_TableAppendProgressBarColumn },
	{ "AppendButtonColumn",       l_TableAppendButtonColumn },
	{ "AppendImageColumn",        l_TableAppendImageColumn },
	{ "AppendImageTextColumn",    l_TableAppendImageTextColumn },
	{ "HeaderVisible",            l_TableHeaderVisible },
	{ "HeaderSetVisible",         l_TableHeaderSetVisible },
	{ "OnRowClicked",             l_TableOnRowClicked },
	{ "OnRowDoubleClicked",       l_TableOnRowDoubleClicked },
	{ "OnSelectionChanged",       l_TableOnSelectionChanged },
	{ "GetSelectionMode",         l_TableGetSelectionMode },
	{ "SetSelectionMode",         l_TableSetSelectionMode },
	{ "GetSelection",             l_TableGetSelection },
	{ "SetSelection",             l_TableSetSelection },
	{ "ColumnWidth",              l_TableColumnWidth },
	{ "ColumnSetWidth",           l_TableColumnSetWidth },
	{ NULL }
};


/*
 * Dialogs
 */

int l_OpenFile(lua_State *L)
{
	char *path = uiOpenFile(CAST_ARG(1, Window));
	if (path) {
		lua_pushstring(L, path);
		uiFreeText(path);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_OpenFolder(lua_State *L)
{
	char *path = uiOpenFolder(CAST_ARG(1, Window));
	if (path) {
		lua_pushstring(L, path);
		uiFreeText(path);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_SaveFile(lua_State *L)
{
	char *path = uiSaveFile(CAST_ARG(1, Window));
	if (path) {
		lua_pushstring(L, path);
		uiFreeText(path);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int l_MsgBox(lua_State *L)
{
	uiMsgBox(CAST_ARG(1, Window), luaL_checkstring(L, 2), luaL_checkstring(L, 3));
	return 0;
}

int l_MsgBoxError(lua_State *L)
{
	uiMsgBoxError(CAST_ARG(1, Window), luaL_checkstring(L, 2), luaL_checkstring(L, 3));
	return 0;
}


/*
 * Various top level
 */

int l_Init(lua_State *L)
{
	uiInitOptions o;

	memset(&o, 0, sizeof (uiInitOptions));

	const char *err = uiInit(&o);

	lua_pushstring(L, err);
	return 1;
}

int l_Uninit(lua_State *L)
{
	uiUninit();
	return 0;
}

int l_Main(lua_State *L)
{
	uiMain();
	return 0;
}

int l_MainStep(lua_State *L)
{
	int r = uiMainStep(lua_toboolean(L, 1));
	if (r == 0) {
		fprintf(stderr, "[libuilua] MainStep: WM_QUIT received, "
			"posting WM_CLOSE to %d tracked windows and re-posting WM_QUIT\n",
			g_tracked_window_count);
		/* WM_QUIT received — the app is shutting down. Post WM_CLOSE to
		 * each tracked window so their OnClosing handlers fire.  This is
		 * necessary for Lua tool scripts that use
		 *   while win_open do ui.MainStep(true) end
		 * because they only break the loop when win_open becomes false,
		 * which only happens in the Lua OnClosing callback. */
		libuilua_request_close_all_windows();
		/* Re-post WM_QUIT so subsequent uiMainStep calls also return 0,
		 * preventing the loop from blocking on GetMessageW forever after
		 * the initial WM_QUIT is consumed. */
		uiQuit();
	}
	lua_pushnumber(L, r);
	return 1;
}

int l_Quit(lua_State *L)
{
	uiQuit();
	return 0;
}

static struct luaL_Reg lui_table[] = {

	{ "Init",                   l_Init },
	{ "Uninit",                 l_Uninit },
	{ "Main",                   l_Main },
	{ "MainStep",               l_MainStep },
	{ "Quit",                   l_Quit },

	/* Constructors */
	{ "NewArea",                l_NewArea },
	{ "NewButton",              l_NewButton },
	{ "NewCheckbox",            l_NewCheckbox },
	{ "NewColorButton",         l_NewColorButton },
	{ "NewCombobox",            l_NewCombobox },
	{ "NewDateTimePicker",      l_NewDateTimePicker },
	{ "NewDatePicker",          l_NewDatePicker },
	{ "NewTimePicker",          l_NewTimePicker },
	{ "NewEditableCombobox",    l_NewEditableCombobox },
	{ "NewEntry",               l_NewEntry },
	{ "NewPasswordEntry",       l_NewPasswordEntry },
	{ "NewSearchEntry",         l_NewSearchEntry },
	{ "NewForm",                l_NewForm },
	{ "NewGrid",                l_NewGrid },
	{ "NewGroup",               l_NewGroup },
	{ "NewHorizontalBox",       l_NewHorizontalBox },
	{ "NewHorizontalSeparator", l_NewHorizontalSeparator },
	{ "NewLabel",               l_NewLabel },
	{ "NewMultilineEntry",      l_NewMultilineEntry },
	{ "NewNonWrappingMultilineEntry", l_NewNonWrappingMultilineEntry },
	{ "NewProgressBar",         l_NewProgressBar },
	{ "NewRadioButtons",        l_NewRadioButtons },
	{ "NewSlider",              l_NewSlider },
	{ "NewSpinbox",             l_NewSpinbox },
	{ "NewStaticImage",         l_NewStaticImage },
	{ "NewTab",                 l_NewTab },
	{ "NewTable",               l_NewTable },
	{ "NewTableModel",          l_NewTableModel },
	{ "NewImage",               l_NewImage },
	{ "LoadImageFromMemory",    l_LoadImageFromMemory },
	{ "ImageAppend",            l_ImageAppend },
	{ "NewVerticalBox",         l_NewVerticalBox },
	{ "NewVerticalSeparator",   l_NewVerticalSeparator },
	{ "NewWindow",              l_NewWindow },

	/* Control base methods (work on any control) */
	{ "ControlShow",            l_ControlShow },
	{ "ControlHide",            l_ControlHide },
	{ "ControlEnable",          l_ControlEnable },
	{ "ControlDisable",         l_ControlDisable },
	{ "ControlVisible",         l_ControlVisible },
	{ "ControlEnabled",         l_ControlEnabled },
	{ "ControlDestroy",         l_ControlDestroy },

	/* Dialogs */
	{ "OpenFile",               l_OpenFile },
	{ "OpenFolder",             l_OpenFolder },
	{ "SaveFile",               l_SaveFile },
	{ "MsgBox",                 l_MsgBox },
	{ "MsgBoxError",            l_MsgBoxError },

	/* TableValue type constants */
	{ "TableValueTypeString",   NULL },
	{ "TableValueTypeImage",    NULL },
	{ "TableValueTypeInt",      NULL },
	{ "TableValueTypeColor",    NULL },

	/* TableModel column editability constants */
	{ "TableModelColumnNeverEditable",  NULL },
	{ "TableModelColumnAlwaysEditable", NULL },

	/* Table selection mode constants */
	{ "TableSelectionModeNone",       NULL },
	{ "TableSelectionModeZeroOrOne",  NULL },
	{ "TableSelectionModeOne",        NULL },
	{ "TableSelectionModeZeroOrMany", NULL },

	/* Grid/Form align constants */
	{ "AlignFill",              NULL },
	{ "AlignStart",             NULL },
	{ "AlignCenter",            NULL },
	{ "AlignEnd",               NULL },

	{ NULL }
};


int luaopen_libuilua(lua_State *L)
{
	CREATE_META(Area)
	CREATE_META(Box)
	CREATE_META(Button)
	CREATE_META(Checkbox)
	CREATE_META(ColorButton)
	CREATE_META(Combobox)
	CREATE_META(DateTimePicker)
	CREATE_META(EditableCombobox)
	CREATE_META(Entry)
	CREATE_META(Form)
	CREATE_META(Grid)
	CREATE_META(Group)
	CREATE_META(Label)
	CREATE_META(MultilineEntry)
	CREATE_META(ProgressBar)
	CREATE_META(RadioButtons)
	CREATE_META(Separator)
	CREATE_META(Slider)
	CREATE_META(Spinbox)
	CREATE_META(Tab)
	CREATE_META(Table)
	CREATE_META(Window)

	/* Image meta (not a uiControl, has __gc) */
	luaL_newmetatable(L, "libui.Image");
	lua_pushcfunction(L, l_ImageGC);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	/* StaticImage meta (not a uiControl, has __gc) */
	luaL_newmetatable(L, "libui.StaticImage");
	luaL_setfuncs(L, meta_StaticImage, 0);
	lua_pushcfunction(L, si_gc);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	/* TableModel meta (not a uiControl, has __gc) */
	luaL_newmetatable(L, "libui.TableModel");
	luaL_setfuncs(L, meta_TableModel, 0);
	lua_pushcfunction(L, l_TableModelGC);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	luaL_newlib(L, lui_table);

	/* Set integer constants on the returned table */
	lua_pushinteger(L, uiTableValueTypeString);    lua_setfield(L, -2, "TableValueTypeString");
	lua_pushinteger(L, uiTableValueTypeImage);     lua_setfield(L, -2, "TableValueTypeImage");
	lua_pushinteger(L, uiTableValueTypeInt);       lua_setfield(L, -2, "TableValueTypeInt");
	lua_pushinteger(L, uiTableValueTypeColor);     lua_setfield(L, -2, "TableValueTypeColor");

	lua_pushinteger(L, uiTableModelColumnNeverEditable);  lua_setfield(L, -2, "TableModelColumnNeverEditable");
	lua_pushinteger(L, uiTableModelColumnAlwaysEditable); lua_setfield(L, -2, "TableModelColumnAlwaysEditable");

	lua_pushinteger(L, uiTableSelectionModeNone);        lua_setfield(L, -2, "TableSelectionModeNone");
	lua_pushinteger(L, uiTableSelectionModeZeroOrOne);   lua_setfield(L, -2, "TableSelectionModeZeroOrOne");
	lua_pushinteger(L, uiTableSelectionModeOne);         lua_setfield(L, -2, "TableSelectionModeOne");
	lua_pushinteger(L, uiTableSelectionModeZeroOrMany);  lua_setfield(L, -2, "TableSelectionModeZeroOrMany");

	lua_pushinteger(L, uiAlignFill);   lua_setfield(L, -2, "AlignFill");
	lua_pushinteger(L, uiAlignStart);  lua_setfield(L, -2, "AlignStart");
	lua_pushinteger(L, uiAlignCenter); lua_setfield(L, -2, "AlignCenter");
	lua_pushinteger(L, uiAlignEnd);    lua_setfield(L, -2, "AlignEnd");

	return 1;
}

/*
 * End
 */
