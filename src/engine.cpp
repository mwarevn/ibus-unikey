#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <libintl.h>
#include <stdlib.h>

#include <sys/wait.h>
#include <string.h>
#include <ibus.h>

#include "unikey.h"
#include "vnconv.h"

#include "engine_private.h"
#include "unikey_config.h"

#define _(string) gettext(string)

#define CONVERT_BUF_SIZE 1024

// Shift+Shift double tap window (ms)
#define SHIFT_TOGGLE_MS 400

static unsigned char WordBreakSyms[] =
{
    ',', ';', ':', '.', '\"', '\'', '!', '?', ' ',
    '<', '>', '=', '+', '-', '*', '/', '\\',
    '_', '~', '`', '@', '#', '$', '%', '^', '&', '(', ')', '{', '}', '[', ']',
    '|'
};

static IBusEngineClass* parent_class = NULL;

// focused engine for config callback (may be NULL on startup)
static IBusUnikeyEngine* g_focused = NULL;

GType ibus_unikey_engine_get_type(void)
{
    static GType type = 0;

    static const GTypeInfo type_info = {
        sizeof(IBusUnikeyEngineClass),
        (GBaseInitFunc)NULL,
        (GBaseFinalizeFunc)NULL,
        (GClassInitFunc)ibus_unikey_engine_class_init,
        NULL,
        NULL,
        sizeof(IBusUnikeyEngine),
        0,
        (GInstanceInitFunc)ibus_unikey_engine_init,
    };

    if (type == 0)
    {
        type = g_type_register_static(IBUS_TYPE_ENGINE,
                                      "IBusUnikeyEngine",
                                      &type_info,
                                      (GTypeFlags)0);
    }

    return type;
}

void ibus_unikey_init(IBusBus* bus)
{
    UnikeySetup();
    ibus_unikey_config_init();

    ibus_unikey_config_on_changed(ibus_unikey_config_value_changed, NULL);
}

void ibus_unikey_exit()
{
    UnikeyCleanup();
}

static void ibus_unikey_engine_class_init(IBusUnikeyEngineClass* klass)
{
    GObjectClass* object_class         = G_OBJECT_CLASS(klass);
    IBusObjectClass* ibus_object_class = IBUS_OBJECT_CLASS(klass);
    IBusEngineClass* engine_class      = IBUS_ENGINE_CLASS(klass);

    parent_class = (IBusEngineClass* )g_type_class_peek_parent(klass);

    object_class->constructor = ibus_unikey_engine_constructor;
    ibus_object_class->destroy = (IBusObjectDestroyFunc)ibus_unikey_engine_destroy;

    engine_class->process_key_event = ibus_unikey_engine_process_key_event;
    engine_class->reset             = ibus_unikey_engine_reset;
    engine_class->enable            = ibus_unikey_engine_enable;
    engine_class->disable           = ibus_unikey_engine_disable;
    engine_class->focus_in          = ibus_unikey_engine_focus_in;
    engine_class->focus_out         = ibus_unikey_engine_focus_out;
    engine_class->property_activate = ibus_unikey_engine_property_activate;
}

static void ibus_unikey_engine_init(IBusUnikeyEngine* self)
{
    // defaults before config loads
    self->preeditstr = new std::string();
    self->last_key_with_shift = FALSE;
    self->vietMode = TRUE;
    self->showUnderline = FALSE;
    self->toggleHotkey = g_strdup("all");
    self->lastShiftTimeMs = 0;
    self->pendingShiftToggle = FALSE;

    ibus_unikey_engine_load_config(self);

    UnikeySetInputMethod(self->im);
    UnikeySetOutputCharset(self->oc);
    UnikeySetOptions(&self->ukopt);
    // apply vietMode globally (ukengine checks this for filtering)
    // we keep a single global vietKey flag in ukengine shared mem
    extern UkSharedMem* pShMem;
    if (pShMem) pShMem->vietKey = self->vietMode ? 1 : 0;

    ibus_unikey_engine_create_property_list(self);
    ibus_unikey_update_viet_mode_ui(self);
}

static IBusProperty* find_prop_from_list(IBusPropList* list, const char* key)
{
    for (guint i = 0; i < list->properties->len ; i++)
    {
        IBusProperty* prop = ibus_prop_list_get(list, i);
        if (prop == NULL)
            return NULL;
        if (strcmp(ibus_property_get_key(prop), key) == 0)
            return prop;
    }
    return NULL;
}

static void ibus_unikey_engine_update_property_list(IBusUnikeyEngine* self)
{
    bool b;
    IBusProperty* prop;

    b = self->ukopt.spellCheckEnabled;
    prop = find_prop_from_list(self->prop_list, CONFIG_SPELLCHECK);
    if (prop != NULL)
        ibus_property_set_state(prop, (b == 1) ? PROP_STATE_CHECKED:PROP_STATE_UNCHECKED);

    b = self->ukopt.autoNonVnRestore;
    prop = find_prop_from_list(self->prop_list, CONFIG_AUTORESTORENONVN);
    if (prop != NULL)
        ibus_property_set_state(prop, (b == 1) ? PROP_STATE_CHECKED:PROP_STATE_UNCHECKED);

    b = self->ukopt.macroEnabled;
    prop = find_prop_from_list(self->prop_list, CONFIG_MACROENABLED);
    if (prop != NULL)
        ibus_property_set_state(prop, (b == 1) ? PROP_STATE_CHECKED:PROP_STATE_UNCHECKED);

    ibus_unikey_update_viet_mode_ui(self);
    if (g_focused == self) {
        ibus_engine_update_property(IBUS_ENGINE(self), find_prop_from_list(self->prop_list, "viet-mode"));
    }
}

static void ibus_unikey_engine_load_config(IBusUnikeyEngine* self)
{
    gchar* str = NULL;
    gboolean b = FALSE;

    auto im = input_method_map.at("telex").first;
    if (ibus_unikey_config_get_string(CONFIG_INPUTMETHOD, &str) && str)
    {
        auto it = input_method_map.find(std::string(str));
        if (it != input_method_map.end()) im = it->second.first;
        g_free(str); str = NULL;
    }
    self->im = im;

    auto oc = output_charset_map.at("unicode").first;
    if (ibus_unikey_config_get_string(CONFIG_OUTPUTCHARSET, &str) && str)
    {
        auto it = output_charset_map.find(std::string(str));
        if (it != output_charset_map.end()) oc = it->second.first;
        g_free(str); str = NULL;
    }
    self->oc = oc;

    if (ibus_unikey_config_get_boolean(CONFIG_FREEMARKING, &b))
        self->ukopt.freeMarking = b;

    if (ibus_unikey_config_get_boolean(CONFIG_MODERNSTYLE, &b))
        self->ukopt.modernStyle = b;

    if (ibus_unikey_config_get_boolean(CONFIG_MACROENABLED, &b))
        self->ukopt.macroEnabled = b;

    if (ibus_unikey_config_get_boolean(CONFIG_SPELLCHECK, &b))
        self->ukopt.spellCheckEnabled = b;

    if (ibus_unikey_config_get_boolean(CONFIG_AUTORESTORENONVN, &b))
        self->ukopt.autoNonVnRestore = b;

    if (ibus_unikey_config_get_boolean(CONFIG_STANDALONEW, &b))
        self->process_w_at_begin = b;

    // new keys — keep previous value if not present
    if (ibus_unikey_config_get_boolean(CONFIG_SHOW_UNDERLINE, &b))
        self->showUnderline = b;
    if (ibus_unikey_config_get_boolean(CONFIG_VIETMODE, &b)) {
        self->vietMode = b;
        extern UkSharedMem* pShMem;
        if (pShMem) pShMem->vietKey = b ? 1 : 0;
    }
    if (ibus_unikey_config_get_string(CONFIG_TOGGLE_HOTKEY, &str) && str) {
        g_free(self->toggleHotkey);
        self->toggleHotkey = str; // take ownership
    }

    // load macro
    gchar* fn = get_macro_file();
    UnikeyLoadMacroTable(fn);
    g_free(fn);
}

static GObject* ibus_unikey_engine_constructor(GType type,
                                               guint n_construct_params,
                                               GObjectConstructParam* construct_params)
{
    IBusUnikeyEngine* self;
    self = (IBusUnikeyEngine*)
        G_OBJECT_CLASS(parent_class)->constructor(type,
                                                  n_construct_params,
                                                  construct_params);
    return (GObject*)self;
}

static void ibus_unikey_engine_destroy(IBusUnikeyEngine* self)
{
    if (self->preeditstr) { delete self->preeditstr; self->preeditstr = nullptr; }
    if (self->prop_list)  { g_object_unref(self->prop_list); self->prop_list = nullptr; }
    if (self->toggleHotkey) { g_free(self->toggleHotkey); self->toggleHotkey = nullptr; }
    if (g_focused == self) g_focused = NULL;
    IBUS_OBJECT_CLASS(parent_class)->destroy((IBusObject*)self);
}

static void ibus_unikey_buffer_reset(IBusEngine* engine)
{
    auto* self = (IBusUnikeyEngine*)engine;
    ibus_engine_hide_preedit_text(engine);
    if (self->preeditstr) self->preeditstr->clear();
    UnikeyResetBuf();
}

static void ibus_unikey_buffer_commit(IBusEngine* engine)
{
    auto* self = (IBusUnikeyEngine*)engine;
    if (self->preeditstr && self->preeditstr->length() > 0)
    {
        IBusText *text = ibus_text_new_from_string(self->preeditstr->c_str());
        ibus_engine_commit_text(engine, text);
    }
    ibus_unikey_buffer_reset(engine);
}

static void ibus_unikey_engine_focus_in(IBusEngine* engine)
{
    auto* self = (IBusUnikeyEngine*)engine;
    g_focused = self;
    // sync ukengine vietKey with this engine's mode
    extern UkSharedMem* pShMem;
    if (pShMem) pShMem->vietKey = self->vietMode ? 1 : 0;

    ibus_engine_register_properties(engine, self->prop_list);
    // update toggle label immediately
    ibus_unikey_update_viet_mode_ui(self);

    parent_class->focus_in(engine);
}

static void ibus_unikey_engine_focus_out(IBusEngine* engine)
{
    // commit pending preedit instead of discarding — avoid losing text on alt-tab
    auto* self = (IBusUnikeyEngine*)engine;
    if (self->preeditstr && !self->preeditstr->empty()) {
        IBusText *text = ibus_text_new_from_string(self->preeditstr->c_str());
        ibus_engine_commit_text(engine, text);
    }
    ibus_unikey_buffer_reset(engine);
    if (g_focused == self) {
        // keep g_focused for config callback that may fire during focus change
    }
    parent_class->focus_out(engine);
}

static void ibus_unikey_engine_reset(IBusEngine* engine)
{
    // reset requested by client (e.g. cursor moved) — commit first
    auto* self = (IBusUnikeyEngine*)engine;
    if (self->preeditstr && !self->preeditstr->empty()) {
        IBusText *text = ibus_text_new_from_string(self->preeditstr->c_str());
        ibus_engine_commit_text(engine, text);
    }
    ibus_unikey_buffer_reset(engine);
    parent_class->reset(engine);
}

static void ibus_unikey_engine_enable(IBusEngine* engine)
{
    parent_class->enable(engine);
}

static void ibus_unikey_engine_disable(IBusEngine* engine)
{
    parent_class->disable(engine);
}

static void ibus_unikey_config_value_changed(gchar* name, gpointer user_data)
{
    (void)user_data;
    if (!g_focused) return; // no focused engine yet (startup)

    // reload config for focused engine only; other engines will reload on next focus_in
    ibus_unikey_engine_load_config(g_focused);

    UnikeySetInputMethod(g_focused->im);
    UnikeySetOutputCharset(g_focused->oc);
    UnikeySetOptions(&g_focused->ukopt);
    extern UkSharedMem* pShMem;
    if (pShMem) pShMem->vietKey = g_focused->vietMode ? 1 : 0;

    ibus_unikey_engine_update_property_list(g_focused);
    // push property updates to panel
    ibus_engine_update_property(IBUS_ENGINE(g_focused),
        find_prop_from_list(g_focused->prop_list, CONFIG_SPELLCHECK));
    ibus_engine_update_property(IBUS_ENGINE(g_focused),
        find_prop_from_list(g_focused->prop_list, CONFIG_AUTORESTORENONVN));
    ibus_engine_update_property(IBUS_ENGINE(g_focused),
        find_prop_from_list(g_focused->prop_list, CONFIG_MACROENABLED));
    ibus_engine_update_property(IBUS_ENGINE(g_focused),
        find_prop_from_list(g_focused->prop_list, "viet-mode"));
}

// ------------ vietMode toggle helpers ------------

static void ibus_unikey_update_viet_mode_ui(IBusUnikeyEngine* self)
{
    IBusProperty* prop = find_prop_from_list(self->prop_list, "viet-mode");
    if (!prop) return;
    IBusText* label = ibus_text_new_from_string(self->vietMode ? "VI" : "EN");
    ibus_property_set_label(prop, label);
    // also use symbol as icon hint if needed
    ibus_property_set_symbol(prop, label);
    ibus_property_set_state(prop, self->vietMode ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED);
    // keep prop tooltip
    const char* tip = self->vietMode ? _("Vietnamese — hotkey to switch to EN") : _("English — hotkey to switch to VI");
    IBusText* tooltip = ibus_text_new_from_string(tip);
    ibus_property_set_tooltip(prop, tooltip);
}

static void ibus_unikey_toggle_viet_mode(IBusUnikeyEngine* self)
{
    // commit pending composition before switching — avoids half-word corruption
    ibus_unikey_buffer_commit(IBUS_ENGINE(self));
    self->vietMode = !self->vietMode;
    extern UkSharedMem* pShMem;
    if (pShMem) pShMem->vietKey = self->vietMode ? 1 : 0;
    UnikeyResetBuf();
    ibus_unikey_config_set_boolean(CONFIG_VIETMODE, self->vietMode);
    ibus_unikey_update_viet_mode_ui(self);
    ibus_engine_update_property(IBUS_ENGINE(self), find_prop_from_list(self->prop_list, "viet-mode"));
}

static gboolean viet_hotkey_allows(IBusUnikeyEngine* self, const char* want)
{
    if (!self->toggleHotkey) return true;
    if (strcmp(self->toggleHotkey, "all")==0) return true;
    return strcmp(self->toggleHotkey, want)==0;
}

static gboolean ibus_unikey_is_toggle_hotkey(IBusUnikeyEngine* self, guint keyval, guint modifiers, gboolean isShiftShiftAlone)
{
    // Ctrl+Space
    if (keyval == IBUS_space && (modifiers & IBUS_CONTROL_MASK)) {
        if (viet_hotkey_allows(self, "ctrl-space")) return TRUE;
    }
    // Shift+Space (with shift)
    if (keyval == IBUS_space && (modifiers & IBUS_SHIFT_MASK)) {
        if (viet_hotkey_allows(self, "shift-space")) return TRUE;
    }
    // bare Shift press (Shift_L / Shift_R alone) — handled via isShiftShiftAlone
    if (isShiftShiftAlone) {
        if (viet_hotkey_allows(self, "shift-shift")) return TRUE;
    }
    return FALSE;
}

static void ibus_unikey_engine_property_activate(IBusEngine* engine,
                                                 const gchar* prop_name,
                                                 guint prop_state)
{
    auto* self = (IBusUnikeyEngine*)engine;

    if (strcmp(prop_name, "more-settings") == 0)
    {
        int ret = system(LIBEXECDIR "/ibus-setup-unikey &");
        if (ret == -1)
            g_print("Failed to open ibus-setup-unikey");
        return;
    }
    if (strcmp(prop_name, "viet-mode") == 0)
    {
        (void)prop_state;
        ibus_unikey_toggle_viet_mode(self);
        return;
    }
    if (strcmp(prop_name, CONFIG_SPELLCHECK) == 0)
    {
        self->ukopt.spellCheckEnabled = prop_state > 0;
        ibus_unikey_config_set_boolean(prop_name, prop_state > 0);
    }
    else if (strcmp(prop_name, CONFIG_AUTORESTORENONVN) == 0)
    {
        self->ukopt.autoNonVnRestore = prop_state > 0;
        ibus_unikey_config_set_boolean(prop_name, prop_state > 0);
    }
    else if (strcmp(prop_name, CONFIG_MACROENABLED) == 0)
    {
        self->ukopt.macroEnabled = prop_state > 0;
        ibus_unikey_config_set_boolean(prop_name, prop_state > 0);
    }
}

static void ibus_unikey_engine_create_property_list(IBusUnikeyEngine* self)
{
    IBusProperty* prop;
    IBusText* label;

    if (self->prop_list != NULL)
        return;
    self->prop_list = ibus_prop_list_new();
    g_object_ref_sink(self->prop_list);

    // viet-mode toggle (EN/VI) — first item, most visible
    label = ibus_text_new_from_string(self->vietMode ? "VI" : "EN");
    IBusText* tooltip_v = ibus_text_new_from_string(_("Click to toggle EN/VI"));
    prop = ibus_property_new("viet-mode",
                             PROP_TYPE_TOGGLE,
                             label,
                             "",
                             tooltip_v,
                             TRUE,
                             TRUE,
                             self->vietMode ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED,
                             NULL);
    ibus_prop_list_append(self->prop_list, prop);

    // spellcheck property
    label = ibus_text_new_from_static_string(_("Enable spell check"));
    prop = ibus_property_new(CONFIG_SPELLCHECK,
                             PROP_TYPE_TOGGLE,
                             label,
                             "",
                             NULL,
                             TRUE,
                             TRUE,
                             (self->ukopt.spellCheckEnabled==1)?
                             PROP_STATE_CHECKED:PROP_STATE_UNCHECKED,
                             NULL);
    ibus_prop_list_append(self->prop_list, prop);

    // auto restore property
    label = ibus_text_new_from_static_string(_("Auto restore non Vietnamese word"));
    prop = ibus_property_new(CONFIG_AUTORESTORENONVN,
                             PROP_TYPE_TOGGLE,
                             label,
                             "",
                             NULL,
                             TRUE,
                             TRUE,
                             (self->ukopt.autoNonVnRestore==1)?
                             PROP_STATE_CHECKED:PROP_STATE_UNCHECKED,
                             NULL);
    ibus_prop_list_append(self->prop_list, prop);

    // macroEnabled property
    label = ibus_text_new_from_static_string(_("Enable Macro"));
    prop = ibus_property_new(CONFIG_MACROENABLED,
                             PROP_TYPE_TOGGLE,
                             label,
                             "",
                             NULL,
                             TRUE,
                             TRUE,
                             (self->ukopt.macroEnabled==1)?
                             PROP_STATE_CHECKED:PROP_STATE_UNCHECKED,
                             NULL);
    ibus_prop_list_append(self->prop_list, prop);

    // more setting property
    label = ibus_text_new_from_static_string(_("More settings..."));
    prop = ibus_property_new("more-settings",
                             PROP_TYPE_NORMAL,
                             label,
                             "",
                             NULL,
                             TRUE,
                             TRUE,
                             PROP_STATE_UNCHECKED,
                             NULL);
    ibus_prop_list_append(self->prop_list, prop);
}

static void ibus_unikey_engine_update_preedit_string(IBusEngine *engine, const gchar *string, gboolean visible)
{
    auto* self = (IBusUnikeyEngine*)engine;
    IBusText *text = ibus_text_new_from_string(string);

    // only underline if user opted in — default off (no gạch chân)
    if (self->showUnderline) {
        ibus_text_append_attribute(text, IBUS_ATTR_TYPE_UNDERLINE, IBUS_ATTR_UNDERLINE_SINGLE, 0, -1);
    }

    guint len = ibus_text_get_length(text);
    // Use COMMIT mode so surrounding text handling is consistent; but if underline off,
    // preedit still works. COMMIT is needed for Chrome/VSCode to keep composition inline.
    ibus_engine_update_preedit_text_with_mode(engine, text, len, visible, IBUS_ENGINE_PREEDIT_COMMIT);
}

static void ibus_unikey_engine_erase_chars(IBusEngine *engine, int count)
{
    auto* self = (IBusUnikeyEngine*)engine;
    if (!self->preeditstr || count <= 0) return;
    int i = (int)self->preeditstr->length();
    while (i > 0 && count > 0) {
        unsigned char code = (unsigned char)self->preeditstr->at(i-1);
        if ((code >> 6) != 2) { // first byte of utf-8 char
            count--;
        }
        i--;
    }
    if (i < 0) i = 0;
    self->preeditstr->erase(i);
}

// code from x-unikey, for convert charset that not is XUtf-8
int latinToUtf(unsigned char* dst, unsigned char* src, int inSize, int* pOutSize)
{
    int i;
    int outLeft;
    unsigned char ch;

    outLeft = *pOutSize;

    for (i=0; i<inSize; i++)
    {
        ch = *src++;
        if (ch < 0x80)
        {
            outLeft -= 1;
            if (outLeft >= 0)
                *dst++ = ch;
        }
        else
        {
            outLeft -= 2;
            if (outLeft >= 0)
            {
                *dst++ = (0xC0 | ch >> 6);
                *dst++ = (0x80 | (ch & 0x3F));
            }
        }
    }

    *pOutSize = outLeft;
    return (outLeft >= 0);
}


static gboolean ibus_unikey_engine_process_key_event(IBusEngine* engine,
                                                     guint keyval,
                                                     guint keycode,
                                                     guint modifiers)
{
    auto* self = (IBusUnikeyEngine*)engine;
    gboolean handled = ibus_unikey_engine_process_key_event_preedit(engine, keyval, keycode, modifiers);

    // track last_key_with_shift for legacy Shift+Space restore detection
    if (keyval >= IBUS_space && keyval <= IBUS_asciitilde)
        self->last_key_with_shift = (modifiers & IBUS_SHIFT_MASK) ? TRUE : FALSE;
    else if (keyval != IBUS_Shift_L && keyval != IBUS_Shift_R)
        self->last_key_with_shift = FALSE;

    return handled;
}

static gboolean ibus_unikey_engine_process_key_event_preedit(IBusEngine* engine,
                                                             guint keyval,
                                                             guint keycode,
                                                             guint modifiers)
{
    auto* self = (IBusUnikeyEngine*)engine;
    (void)keycode;

    if (modifiers & IBUS_RELEASE_MASK)
        return false;

    // System hotkeys (Win/Super/Meta/Hyper + anything, e.g. Win+Space to
    // switch input source): commit current preedit and DON'T consume the
    // event, otherwise the desktop never sees Space.
    if (modifiers & (IBUS_SUPER_MASK | IBUS_HYPER_MASK | IBUS_META_MASK | IBUS_MOD4_MASK))
    {
        if (self->preeditstr && !self->preeditstr->empty())
            ibus_unikey_buffer_commit(engine);
        return false;
    }
    // Bare Super/Hyper/Meta press alone (no modifier bit yet) — also commit
    // so the subsequent Super+Space isn't swallowed. Covers Win key down.
    if (keyval == IBUS_Super_L || keyval == IBUS_Super_R
        || keyval == IBUS_Hyper_L || keyval == IBUS_Hyper_R
        || keyval == IBUS_Meta_L  || keyval == IBUS_Meta_R)
    {
        if (self->preeditstr && !self->preeditstr->empty())
            ibus_unikey_buffer_commit(engine);
        return false;
    }

    // ---- EN/VI toggle hotkeys (highest priority, before any commit/pass) ----
    // Ctrl+Space and Shift+Space are detected as printable space with modifiers
    // Shift+Shift (bare Shift tap twice) is detected via timing window below

    // Ctrl+Space / Shift+Space
    if (ibus_unikey_is_toggle_hotkey(self, keyval, modifiers, FALSE)) {
        // For Shift+Space we only toggle if not at word beginning would have been Restore;
        // now we prefer explicit toggle — consume the key so no space is inserted.
        ibus_unikey_toggle_viet_mode(self);
        return true;
    }

    // Bare Shift alone: Shift_L / Shift_R without other modifiers — possible Shift+Shift
    if ((keyval == IBUS_Shift_L || keyval == IBUS_Shift_R)
        && !(modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK))) {

        guint32 now = g_get_monotonic_time() / 1000; // ms
        // If this Shift alone follows a previous Shift within window -> toggle
        if (self->pendingShiftToggle && (now - self->lastShiftTimeMs) <= SHIFT_TOGGLE_MS) {
            if (ibus_unikey_is_toggle_hotkey(self, keyval, modifiers, TRUE)) {
                self->pendingShiftToggle = FALSE;
                self->lastShiftTimeMs = 0;
                ibus_unikey_toggle_viet_mode(self);
                return true;
            }
        }
        // otherwise arm the pending flag and wait for next Shift
        // If user holds Shift and types, pending will be cleared by printable path
        self->pendingShiftToggle = TRUE;
        self->lastShiftTimeMs = now;
        // Don't consume single Shift — let it pass for caps/shift typing
        return false;
    }

    // Any non-Shift key clears pending Shift+Shift? No — keep it for double-tap
    // but clamp window: if too old, clear
    if (self->pendingShiftToggle) {
        guint32 now = g_get_monotonic_time() / 1000;
        if ((now - self->lastShiftTimeMs) > SHIFT_TOGGLE_MS)
            self->pendingShiftToggle = FALSE;
    }

    // ---- EN passthrough: when vietMode==OFF, don't filter Vietnamese ----
    // Still allow BackSpace/restore in EN? No — pure passthrough for English.
    // Only toggle hotkey above is handled. Everything else is forwarded.
    if (!self->vietMode) {
        // Need to ensure we have no stale preedit; if switching left some, commit
        if (self->preeditstr && !self->preeditstr->empty()) {
            ibus_unikey_buffer_commit(engine);
        }
        return false;
    }

    if (modifiers & IBUS_CONTROL_MASK
             || modifiers & IBUS_MOD1_MASK // alternate mask
             || keyval == IBUS_Control_L
             || keyval == IBUS_Control_R
             || keyval == IBUS_Tab
             || keyval == IBUS_Return
             || keyval == IBUS_Delete
             || keyval == IBUS_KP_Enter
             || (keyval >= IBUS_Home && keyval <= IBUS_Insert)
             || (keyval >= IBUS_KP_Home && keyval <= IBUS_KP_Delete)
        )
    {
        ibus_unikey_buffer_commit(engine);
        return false;
    }

    else if ((keyval >= IBUS_Caps_Lock && keyval <= IBUS_Hyper_R))
    {
        return false;
    }

    // capture BackSpace
    else if (keyval == IBUS_BackSpace)
    {
        UnikeyBackspacePress();

        if (UnikeyBackspaces == 0 || self->preeditstr->empty())
        {
            return false;
        }
        else
        {
            if ((int)self->preeditstr->length() <= UnikeyBackspaces)
            {
                ibus_unikey_buffer_reset(engine);
            }
            else
            {
                ibus_unikey_engine_erase_chars(engine, UnikeyBackspaces);
                ibus_unikey_engine_update_preedit_string(engine, self->preeditstr->c_str(), true);
            }

            // change tone position after press backspace
            if (UnikeyBufChars > 0)
            {
                if (self->oc == CONV_CHARSET_XUTF8)
                {
                    self->preeditstr->append((const gchar*)UnikeyBuf, UnikeyBufChars);
                }
                else
                {
                    unsigned char buf[CONVERT_BUF_SIZE];
                    int bufSize = CONVERT_BUF_SIZE;
                    latinToUtf(buf, UnikeyBuf, UnikeyBufChars, &bufSize);
                    self->preeditstr->append((const gchar*)buf, CONVERT_BUF_SIZE - bufSize);
                }
                ibus_unikey_engine_update_preedit_string(engine, self->preeditstr->c_str(), true);
            }
        }
        return true;
    } // end capture BackSpace

    else if (keyval >=IBUS_KP_Multiply && keyval <=IBUS_KP_9)
    {
        ibus_unikey_buffer_commit(engine);
        return false;
    }

    // capture ascii printable char
    else if ((keyval >= IBUS_space && keyval <=IBUS_asciitilde)
            || keyval == IBUS_Shift_L || keyval == IBUS_Shift_R) // sure this have IBUS_SHIFT_MASK
    {
        // printable char clears pending shift-toggle after use
        if (keyval >= IBUS_space && keyval <= IBUS_asciitilde)
            self->pendingShiftToggle = FALSE;

        UnikeySetCapsState(modifiers & IBUS_SHIFT_MASK, modifiers & IBUS_LOCK_MASK);

        // process keyval

        if ((self->im == UkTelex || self->im == UkSimpleTelex2)
            && self->process_w_at_begin == false
            && UnikeyAtWordBeginning()
            && (keyval == IBUS_w || keyval == IBUS_W))
        {
            UnikeyPutChar(keyval);
            self->preeditstr->append(keyval==IBUS_w?"w":"W");
            ibus_unikey_engine_update_preedit_string(engine, self->preeditstr->c_str(), true);
            return true;
        }

        // Legacy Shift+Space restore: only if this Space was NOT a viet toggle.
        // Toggle for Shift+Space / Ctrl+Space already returned at top, so any Space
        // reaching here is intentionally NOT a toggle (hotkey filter excluded it).
        // Keep old restore behaviour for users who disabled toggle for shift-space.
        if (keyval == IBUS_space && (modifiers & IBUS_SHIFT_MASK)
            && self->last_key_with_shift == FALSE && !UnikeyAtWordBeginning())
        {
            UnikeyRestoreKeyStrokes();
        }
        else if (keyval == IBUS_Shift_L || keyval == IBUS_Shift_R)
        {
            UnikeyRestoreKeyStrokes();
        }
        else
        {
            UnikeyFilter(keyval);
        }
        // end process keyval

        // process result of ukengine
        if (UnikeyBackspaces > 0)
        {
            if ((int)self->preeditstr->length() <= UnikeyBackspaces)
            {
                self->preeditstr->clear();
            }
            else
            {
                ibus_unikey_engine_erase_chars(engine, UnikeyBackspaces);
            }
        }

        if (UnikeyBufChars > 0)
        {
            if (self->oc == CONV_CHARSET_XUTF8)
            {
                self->preeditstr->append((const gchar*)UnikeyBuf, UnikeyBufChars);
            }
            else
            {
                unsigned char buf[CONVERT_BUF_SIZE];
                int bufSize = CONVERT_BUF_SIZE;
                latinToUtf(buf, UnikeyBuf, UnikeyBufChars, &bufSize);
                self->preeditstr->append((const gchar*)buf, CONVERT_BUF_SIZE - bufSize);
            }
        }
        else if (keyval != IBUS_Shift_L && keyval != IBUS_Shift_R) // if ukengine not process
        {
            int n;
            char s[6];
            n = g_unichar_to_utf8(keyval, s); // convert ucs4 to utf8 char
            self->preeditstr->append(s, n);
        }
        // end process result of ukengine

        // commit string: if need — WordBreakSyms trigger commit, but stay as preedit commit
        // to avoid extra surrounding-text race. Commit via preedit commit is more reliable
        // than delete_surrounding on Wayland. Keep simple.
        if (self->preeditstr->length() > 0)
        {
            for (guint i = 0; i < sizeof(WordBreakSyms); i++)
            {
                if (WordBreakSyms[i] == self->preeditstr->at(self->preeditstr->length()-1)
                    && WordBreakSyms[i] == (unsigned char)keyval)
                {
                    ibus_unikey_buffer_commit(engine);
                    return true;
                }
            }
        }
        // end commit string

        ibus_unikey_engine_update_preedit_string(engine, self->preeditstr->c_str(), true);
        return true;
    } //end capture printable char

    // non process key
    ibus_unikey_buffer_commit(engine);
    return false;
}
