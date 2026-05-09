#include "mew_ui_api.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

// Keep the resolved Mewjector API around so the bootstrap path does not keep asking for it...
static MewjectorAPI g_mew_ui_mj;
static const char* g_mew_ui_owner_name = "MewUIApi";
static volatile LONG g_mew_ui_debug_logs_enabled = 0;
static UINT_PTR g_mew_ui_game_base = 0ULL;
static MewFnButtonActivate g_mew_ui_next_button_activate = NULL;
static MewFnButtonCanActivate g_mew_ui_next_button_can_activate = NULL;
static MewFnSceneReadyUpdate g_mew_ui_next_scene_ready_update = NULL;
static MewButtonRecord g_mew_ui_button_records[MEW_MAX_BUTTON_RECORDS];

static volatile LONG g_mew_ui_record_lock = 0;
static volatile LONG g_mew_ui_setup_lock = 0;
static volatile LONG g_mew_ui_scene_tick_lock = 0;
static HANDLE g_mew_ui_timer_queue = NULL;
static HANDLE g_mew_ui_timer = NULL;
static volatile LONG g_mew_ui_timer_started = 0;
static volatile LONG g_mew_ui_ready = 0;
static volatile LONG g_mew_ui_install_started = 0;
static int g_mew_ui_hook_priority = 30;
static uint32_t g_mew_ui_bootstrap_interval_ms = 100U;
static MewUITickCallback g_mew_ui_tick_callback = NULL;
static void* g_mew_ui_tick_user_data = NULL;
static void* g_mew_ui_last_probe_scene_manager = NULL;
static uint8_t g_mew_ui_last_probe_ready = 255U;
static uint8_t g_mew_ui_last_probe_doing_destruction = 255U;
static uint8_t g_mew_ui_last_probe_skip_ready_tick_a = 255U;
static uint8_t g_mew_ui_last_probe_skip_ready_tick_b = 255U;
static uint8_t g_mew_ui_last_probe_skip_ready_tick_c = 255U;
static uint8_t g_mew_ui_last_probe_transition_like = 255U;
static char g_mew_ui_last_probe_scene_name[MEW_TEXT_BUFFER_MAX];

static MewPodVectorPtr* MewUI_GetSceneComponentList(void* scene_manager);
static UINT_PTR MewUI_Address(UINT_PTR rva);
static const wchar_t* MewUI_GetWideStringData(const MewWideString* value);
static size_t MewUI_GetWideStringSize(const MewWideString* value);
static void* MewUI_FindChildByNameUsingRva(void* root_node, const char* child_name, UINT_PTR find_rva, const char* caller_name);
static int MewUI_SetButtonLabelWideStringOnElement(void* label_text_element, const MewWideString* text, void** applied_targets, uint32_t* applied_target_count);
static int MewUI_SetButtonLabelWideStringOnStateName(void* button_node, const char* state_name, const MewWideString* text, void** applied_targets, uint32_t* applied_target_count);
static const char* MewUI_GetButtonDefaultStateNodeName(MewButtonState state);
static int MewUI_CopyButtonStateNodeName(void* button, MewButtonState state, char* out_buffer, size_t out_buffer_size);
static void* MewUI_FindNodeByPathOrName(void* root_node, const char* node_name, uint8_t preserve_path_mode);
static void* MewUI_FindRootOwnedChildByName(void* root_node, const char* child_name);
static void* MewUI_GetComponentRootNode(void* component);
static void* MewUI_FindTextOwnerInScene(void* scene_manager, const char* node_name, void** out_node);
static int MewUI_SetTextInSceneUsingEngineString(void* scene_manager, const char* child_name, const char* text, uint8_t use_localization_key);
static int MewUI_SetTextInSceneUsingEngineWideString(void* scene_manager, const char* child_name, const MewWideString* text);
static int MewUI_InitWideStringFromText(MewWideString* out_string, const char* text);
static void MewUI_FreeWideStringFromText(MewWideString* value);
static int MewUI_InitWideStringFromWideData(MewWideString* out_string, const wchar_t* text, size_t length);
static void MewUI_DestroyEngineWideString(MewWideString* value);
static int MewUI_LocalizeKeyToWideString(const char* key, MewWideString* out_string);
static int MewUI_FormatWideStringValues(const MewWideString* source_string, const char* const* values, uint32_t value_count, MewWideString* out_string);
static void MewUI_InitWideStringFromTemporaryWideData(MewWideString* out_string, const wchar_t* text, size_t length);
static int MewUI_CopyUtf8ToTemporaryWideBuffer(const char* text, wchar_t* out_buffer, size_t out_capacity, size_t* out_length);
static int MewUI_FormatPreparedTextValueToWide(const MewUITextFormatValue* value, wchar_t* out_buffer, size_t out_capacity, size_t* out_length);
static int MewUI_AppendPreparedTextLiteral(MewUITextFormat* format, const wchar_t* text, size_t length);
static int MewUI_AppendPreparedTextPlaceholder(MewUITextFormat* format, uint32_t value_index);
static int MewUI_IsTrackedButtonForScene(void* scene_manager, void* button);
static int MewUI_IsSceneNameTransitionLike(const MewNarrowString* scene_name);
static void MewUI_CopyNarrowStringToBuffer(const MewNarrowString* value, char* out_buffer, size_t out_buffer_size);
static void MewUI_LogSceneReadyProbe(void* scene_manager, const MewNarrowString* scene_name, uint8_t ready, uint8_t doing_destruction, uint8_t skip_ready_tick_a, uint8_t skip_ready_tick_b, uint8_t skip_ready_tick_c, uint8_t transition_like);
static void MewUI_DispatchSceneReadyTick(void* scene_manager);
static int MewUI_SetTextElementWideStringCopy(void* text_element, const MewWideString* text);
static int MewUI_SetButtonLabelWideString(void* button, const MewWideString* text);
static VOID CALLBACK MewUI_TimerProc(PVOID parameter, BOOLEAN timer_or_wait_fired);

void MewUI_SetDebugLogsEnabled(bool enabled)
{
    InterlockedExchange(&g_mew_ui_debug_logs_enabled, enabled ? 1 : 0);
}

bool MewUI_GetDebugLogsEnabled(void)
{
    return InterlockedCompareExchange(&g_mew_ui_debug_logs_enabled, 0, 0) != 0;
}

static void MewUI_APIDebugLog(const char* format, ...)
{
    char buffer[768];
    va_list args;

    if (!MewUI_GetDebugLogsEnabled())
    {
        return;
    }

    if (!g_mew_ui_mj.Log)
    {
        return;
    }

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    g_mew_ui_mj.Log(g_mew_ui_owner_name, "%s", buffer);
}

void MewUI_LogMessage(const char* format, ...)
{
    char buffer[768];
    va_list args;
    MewjectorAPI resolved_api;

    if (!format)
    {
        return;
    }

    if (!g_mew_ui_mj.Log)
    {
        memset(&resolved_api, 0, sizeof(resolved_api));

        if (MJ_Resolve(&resolved_api))
        {
            memcpy(&g_mew_ui_mj, &resolved_api, sizeof(g_mew_ui_mj));
        }
    }

    if (!g_mew_ui_mj.Log)
    {
        return;
    }

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    g_mew_ui_mj.Log(g_mew_ui_owner_name, "%s", buffer);
}

static void MewUI_LockRecords(void)
{
    while (InterlockedCompareExchange(&g_mew_ui_record_lock, 1, 0) != 0)
    {
        Sleep(0);
    }
}

static void MewUI_UnlockRecords(void)
{
    InterlockedExchange(&g_mew_ui_record_lock, 0);
}

static int MewUI_BeginSetupGuard(void)
{
    return InterlockedCompareExchange(&g_mew_ui_setup_lock, 1, 0) == 0;
}

static void MewUI_EndSetupGuard(void)
{
    InterlockedExchange(&g_mew_ui_setup_lock, 0);
}

// Turns known game RVA into a live address...
static UINT_PTR MewUI_Address(UINT_PTR rva)
{
    if (!g_mew_ui_game_base)
    {
        return 0ULL;
    }

    return g_mew_ui_game_base + rva;
}

const char* MewUI_GetNarrowStringData(const MewNarrowString* value)
{
    if (!value)
    {
        return NULL;
    }

    if (value->capacity > 15ULL)
    {
        return value->storage.heap_ptr;
    }

    return value->storage.inline_buf;
}

size_t MewUI_GetNarrowStringSize(const MewNarrowString* value)
{
    if (!value)
    {
        return 0U;
    }

    return (size_t)value->size;
}

static void MewUI_CopyNarrowStringToBuffer(const MewNarrowString* value, char* out_buffer, size_t out_buffer_size)
{
    const char* data;
    size_t length;

    if (!out_buffer || out_buffer_size == 0U)
    {
        return;
    }

    out_buffer[0] = '\0';

    if (!value)
    {
        return;
    }

    __try
    {
        data = MewUI_GetNarrowStringData(value);
        length = MewUI_GetNarrowStringSize(value);

        if (!data)
        {
            return;
        }

        if (length >= out_buffer_size)
        {
            length = out_buffer_size - 1U;
        }

        if (length > 0U)
        {
            memcpy(out_buffer, data, length);
        }

        out_buffer[length] = '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out_buffer[0] = '\0';
    }
}

static const wchar_t* MewUI_GetWideStringData(const MewWideString* value)
{
    if (!value)
    {
        return NULL;
    }

    if (value->capacity > 7ULL)
    {
        return value->storage.heap_ptr;
    }

    return value->storage.inline_buf;
}

static size_t MewUI_GetWideStringSize(const MewWideString* value)
{
    if (!value)
    {
        return 0U;
    }

    return (size_t)value->size;
}

void MewUI_InitSmallWideString(MewWideString* out_string, const wchar_t* text)
{
    size_t length;

    if (!out_string)
    {
        return;
    }

    memset(out_string, 0, sizeof(*out_string));
    out_string->capacity = 7ULL;

    if (!text)
    {
        out_string->storage.inline_buf[0] = L'\0';
        return;
    }

    length = wcslen(text);

    if (length > 7U)
    {
        length = 7U;
    }

    memcpy(out_string->storage.inline_buf, text, length * sizeof(wchar_t));
    out_string->storage.inline_buf[length] = L'\0';
    out_string->size = (uint64_t)length;
}

static int MewUI_NarrowStringEqualsLiteral(const MewNarrowString* value, const char* literal)
{
    const char* data;
    size_t value_length;
    size_t literal_length;

    if (!value || !literal)
    {
        return 0;
    }

    data = MewUI_GetNarrowStringData(value);
    value_length = MewUI_GetNarrowStringSize(value);
    literal_length = strlen(literal);

    if (!data || value_length != literal_length)
    {
        return 0;
    }

    return memcmp(data, literal, literal_length) == 0;
}

// Wraps risky type-name calls so a bad scene pointer doesn't crash...
static int MewUI_SafeGetComponentTypeName(void* component, MewNarrowString* out_name)
{
    MewComponent* typed_component;

    if (!component || !out_name)
    {
        return 0;
    }

    typed_component = (MewComponent*)component;

    __try
    {
        if (!typed_component->vtable || !typed_component->vtable->GetObjectTypeSTR)
        {
            return 0;
        }

        memset(out_name, 0, sizeof(*out_name));
        typed_component->vtable->GetObjectTypeSTR(component, out_name);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    return 1;
}

static int MewUI_IsButtonComponent(void* component)
{
    MewNarrowString type_name;
    int result;

    result = 0;

    if (!MewUI_SafeGetComponentTypeName(component, &type_name))
    {
        return 0;
    }

    result = MewUI_NarrowStringEqualsLiteral(&type_name, "Button");

    return result;
}


static int MewUI_NarrowStringContainsLiteral(const MewNarrowString* value, const char* literal)
{
    const char* data;
    size_t value_length;
    size_t literal_length;
    size_t index;

    if (!value || !literal)
    {
        return 0;
    }

    data = MewUI_GetNarrowStringData(value);
    value_length = MewUI_GetNarrowStringSize(value);
    literal_length = strlen(literal);

    if (!data || literal_length == 0U || value_length < literal_length)
    {
        return 0;
    }

    for (index = 0U; index <= value_length - literal_length; ++index)
    {
        if (memcmp(data + index, literal, literal_length) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static int MewUI_IsSceneNameTransitionLike(const MewNarrowString* scene_name)
{
    if (!scene_name)
    {
        return 0;
    }

    if (MewUI_NarrowStringContainsLiteral(scene_name, "Transition"))
    {
        return 1;
    }

    if (MewUI_NarrowStringContainsLiteral(scene_name, "transition"))
    {
        return 1;
    }

    return 0;
}

static void MewUI_LogSceneReadyProbe(void* scene_manager, const MewNarrowString* scene_name, uint8_t ready, uint8_t doing_destruction, uint8_t skip_ready_tick_a, uint8_t skip_ready_tick_b, uint8_t skip_ready_tick_c, uint8_t transition_like)
{
    char scene_name_buffer[MEW_TEXT_BUFFER_MAX];
    int should_log;

    MewUI_CopyNarrowStringToBuffer(scene_name, scene_name_buffer, sizeof(scene_name_buffer));
    should_log = 0;

    if (scene_manager != g_mew_ui_last_probe_scene_manager)
    {
        should_log = 1;
    }

    if (ready != g_mew_ui_last_probe_ready || doing_destruction != g_mew_ui_last_probe_doing_destruction || skip_ready_tick_a != g_mew_ui_last_probe_skip_ready_tick_a || skip_ready_tick_b != g_mew_ui_last_probe_skip_ready_tick_b || skip_ready_tick_c != g_mew_ui_last_probe_skip_ready_tick_c || transition_like != g_mew_ui_last_probe_transition_like)
    {
        should_log = 1;
    }

    if (strcmp(scene_name_buffer, g_mew_ui_last_probe_scene_name) != 0)
    {
        should_log = 1;
    }

    if (!should_log)
    {
        return;
    }

    g_mew_ui_last_probe_scene_manager = scene_manager;
    g_mew_ui_last_probe_ready = ready;
    g_mew_ui_last_probe_doing_destruction = doing_destruction;
    g_mew_ui_last_probe_skip_ready_tick_a = skip_ready_tick_a;
    g_mew_ui_last_probe_skip_ready_tick_b = skip_ready_tick_b;
    g_mew_ui_last_probe_skip_ready_tick_c = skip_ready_tick_c;
    g_mew_ui_last_probe_transition_like = transition_like;
    strncpy(g_mew_ui_last_probe_scene_name, scene_name_buffer, sizeof(g_mew_ui_last_probe_scene_name) - 1U);
    g_mew_ui_last_probe_scene_name[sizeof(g_mew_ui_last_probe_scene_name) - 1U] = '\0';

    MewUI_APIDebugLog("Scene ready probe: manager=%p name='%s' ready=%u destroying=%u skip={%u,%u,%u} transition_like=%u", scene_manager, scene_name_buffer, (unsigned int)ready, (unsigned int)doing_destruction, (unsigned int)skip_ready_tick_a, (unsigned int)skip_ready_tick_b, (unsigned int)skip_ready_tick_c, (unsigned int)transition_like);
}

int MewUI_IsSceneReadyForUITick(void* scene_manager)
{
    MewNarrowString* scene_name;
    uint8_t doing_destruction;
    uint8_t skip_ready_tick_a;
    uint8_t skip_ready_tick_b;
    uint8_t skip_ready_tick_c;
    uint8_t transition_like;
    uint8_t ready;

    if (!scene_manager || !g_mew_ui_game_base)
    {
        return 0;
    }

    __try
    {
        doing_destruction = *(uint8_t*)((uint8_t*)scene_manager + MEW_OFF_SCENE_DOING_DESTRUCTION);
        skip_ready_tick_a = *(uint8_t*)((uint8_t*)scene_manager + MEW_OFF_SCENE_SKIP_READY_TICK_A);
        skip_ready_tick_b = *(uint8_t*)((uint8_t*)scene_manager + MEW_OFF_SCENE_SKIP_READY_TICK_B);
        skip_ready_tick_c = *(uint8_t*)((uint8_t*)scene_manager + MEW_OFF_SCENE_SKIP_READY_TICK_C);
        scene_name = (MewNarrowString*)((uint8_t*)scene_manager + MEW_OFF_SCENE_NAME);
        transition_like = MewUI_IsSceneNameTransitionLike(scene_name) ? 1U : 0U;
        ready = (!doing_destruction && !skip_ready_tick_a && !skip_ready_tick_b && !skip_ready_tick_c && !transition_like) ? 1U : 0U;

        MewUI_LogSceneReadyProbe(scene_manager, scene_name, ready, doing_destruction, skip_ready_tick_a, skip_ready_tick_b, skip_ready_tick_c, transition_like);

        if (!ready)
        {
            return 0;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("Scene ready probe failed while reading manager=%p, skipping UI tick!", scene_manager);
        return 0;
    }

    return 1;
}

static void MewUI_DispatchSceneReadyTick(void* scene_manager)
{
    if (InterlockedCompareExchange(&g_mew_ui_ready, 0, 0) == 0)
    {
        return;
    }

    if (!MewUI_IsSceneReadyForUITick(scene_manager))
    {
        return;
    }

    if (InterlockedCompareExchange(&g_mew_ui_scene_tick_lock, 1, 0) != 0)
    {
        MewUI_APIDebugLog("Skipping scene-ready UI tick because a previous UI tick is still active! scene=%p", scene_manager);
        return;
    }

    __try
    {
        MewUI_Tick();

        if (g_mew_ui_tick_callback)
        {
            g_mew_ui_tick_callback(g_mew_ui_tick_user_data);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("Scene-ready UI tick failed, callback skipped for this frame! scene=%p", scene_manager);
    }

    InterlockedExchange(&g_mew_ui_scene_tick_lock, 0);
}

static int MewUI_IsTextComponent(void* component)
{
    MewNarrowString type_name;
    int result;

    result = 0;

    if (!MewUI_SafeGetComponentTypeName(component, &type_name))
    {
        return 0;
    }

    result = MewUI_NarrowStringContainsLiteral(&type_name, "Text");

    return result;
}

// Appends a component to a scene bucket (Using the same grow behavior as the game)...
static int MewUI_AppendSceneComponentList(void* scene_manager, uint32_t capacity_offset, uint32_t size_offset, uint32_t data_offset, void* component)
{
    MewFnResizePtrArray resize_array;
    uint32_t capacity;
    uint32_t size;
    uint32_t new_capacity;
    void*** data_ptr;
    void** data;

    if (!scene_manager || !component)
    {
        return 0;
    }

    resize_array = (MewFnResizePtrArray)MewUI_Address(MEW_RVA_RESIZE_PTR_ARRAY);

    if (!resize_array)
    {
        return 0;
    }

    __try
    {
        capacity = *(uint32_t*)((uint8_t*)scene_manager + capacity_offset);
        size = *(uint32_t*)((uint8_t*)scene_manager + size_offset);
        data_ptr = (void***)((uint8_t*)scene_manager + data_offset);
        data = *data_ptr;

        if (size == capacity)
        {
            double grown_capacity;

            grown_capacity = (double)capacity * 1.5;
            new_capacity = (uint32_t)grown_capacity;

            if (new_capacity < 2U)
            {
                new_capacity = 2U;
            }

            data = (void**)resize_array(data, (uint64_t)new_capacity * sizeof(void*));
            *data_ptr = data;
            *(uint32_t*)((uint8_t*)scene_manager + capacity_offset) = new_capacity;

            if (new_capacity < size)
            {
                *(uint32_t*)((uint8_t*)scene_manager + size_offset) = new_capacity;
                size = new_capacity;
            }
        }

        data[size] = component;
        *(uint32_t*)((uint8_t*)scene_manager + size_offset) = size + 1U;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    return 1;
}

// Reuses tracking record so callbacks can survive repeated setup calls...
static MewButtonRecord* MewUI_AllocButtonRecord(void* button)
{
    uint32_t index;
    MewButtonRecord* empty_record;

    empty_record = NULL;

    MewUI_LockRecords();

    for (index = 0U; index < MEW_MAX_BUTTON_RECORDS; ++index)
    {
        if (g_mew_ui_button_records[index].used && g_mew_ui_button_records[index].button == button)
        {
            MewUI_UnlockRecords();
            return &g_mew_ui_button_records[index];
        }

        if (!g_mew_ui_button_records[index].used && !empty_record)
        {
            empty_record = &g_mew_ui_button_records[index];
        }
    }

    if (empty_record)
    {
        memset(empty_record, 0, sizeof(*empty_record));
        empty_record->used = 1U;
        empty_record->button = button;
        empty_record->last_state = MEW_BUTTON_STATE_INVALID;
    }

    MewUI_UnlockRecords();
    return empty_record;
}

MewButtonRecord* MewUI_GetButtonRecord(void* button)
{
    uint32_t index;

    if (!button)
    {
        return NULL;
    }

    for (index = 0U; index < MEW_MAX_BUTTON_RECORDS; ++index)
    {
        if (g_mew_ui_button_records[index].used && g_mew_ui_button_records[index].button == button)
        {
            return &g_mew_ui_button_records[index];
        }
    }

    return NULL;
}

int MewUI_IsButtonTracked(void* button)
{
    return MewUI_GetButtonRecord(button) != NULL;
}

static void MewUI_FireButtonCallback(MewButtonRecord* record, MewButtonEvent event_type, MewButtonState old_state, MewButtonState new_state)
{
    if (!record || !record->callback || !record->button)
    {
        return;
    }

    record->callback(record->button, event_type, old_state, new_state, record->user_data);
}

// Runs mod UI work on the game thread once the scene reaches its ready update pass...
static void __fastcall MewUI_HookSceneReadyUpdate(void* scene_manager)
{
    if (g_mew_ui_next_scene_ready_update)
    {
        g_mew_ui_next_scene_ready_update(scene_manager);
    }

    MewUI_DispatchSceneReadyTick(scene_manager);
}

// Override interactability while keeping the game result as the default...
static uint8_t __fastcall MewUI_HookButtonCanActivate(void* button, int32_t button_index, uint8_t strict_mouse)
{
    MewButtonRecord* record;
    uint8_t result;

    result = 0U;

    if (g_mew_ui_next_button_can_activate)
    {
        result = g_mew_ui_next_button_can_activate(button, button_index, strict_mouse);
    }

    record = MewUI_GetButtonRecord(button);

    if (!record)
    {
        return result;
    }

    if (record->interact_override == MEW_BUTTON_INTERACT_FORCE_ENABLED)
    {
        return 1U;
    }

    if (record->interact_override == MEW_BUTTON_INTERACT_FORCE_DISABLED)
    {
        return 0U;
    }

    if (record->interact_override == MEW_BUTTON_INTERACT_CALLBACK && record->can_interact_callback)
    {
        return record->can_interact_callback(button, result, record->can_interact_user_data) ? 1U : 0U;
    }

    return result;
}

// Treats Button::Activate as a real click, only after we already saw the pressed state...
// (This keeps press entry activate calls from pretending to be accepted clicks)...
static void __fastcall MewUI_HookButtonActivate(void* button, uint8_t from_mouse)
{
    MewButtonRecord* record;
    MewButtonState tracked_state;
    MewButtonState new_state;

    record = MewUI_GetButtonRecord(button);

    if (!record)
    {
        if (g_mew_ui_next_button_activate)
        {
            g_mew_ui_next_button_activate(button, from_mouse);
        }

        return;
    }

    tracked_state = record->last_state;

    if (!record->suppress_original_activate && g_mew_ui_next_button_activate)
    {
        g_mew_ui_next_button_activate(button, from_mouse);
    }

    new_state = MewUI_GetButtonState(button);

    if (tracked_state != MEW_BUTTON_STATE_PRESSED)
    {
        return;
    }

    if (record->click_from_hook_seen)
    {
        return;
    }

    if (new_state == MEW_BUTTON_STATE_INVALID)
    {
        new_state = tracked_state;
    }

    record->click_from_hook_seen = 1U;
    MewUI_FireButtonCallback(record, MEW_BUTTON_EVENT_CLICK, tracked_state, new_state);
}

// Installs hooks from the timer so loader-lock-sensitive work stays out of DllMain...
static void MewUI_EnsureInstallation(void)
{
    MewjectorAPI resolved_api;

    if (InterlockedCompareExchange(&g_mew_ui_ready, 0, 0) != 0)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_mew_ui_install_started, 1, 0) != 0)
    {
        return;
    }

    memset(&resolved_api, 0, sizeof(resolved_api));

    if (!MJ_Resolve(&resolved_api))
    {
        InterlockedExchange(&g_mew_ui_install_started, 0);
        return;
    }

    if (!MewUI_Init(&resolved_api, g_mew_ui_owner_name, g_mew_ui_hook_priority))
    {
        memcpy(&g_mew_ui_mj, &resolved_api, sizeof(g_mew_ui_mj));
        MewUI_APIDebugLog("MewUI_Init failed!");
        InterlockedExchange(&g_mew_ui_install_started, 0);
        return;
    }

    InterlockedExchange(&g_mew_ui_ready, 1);
    MewUI_APIDebugLog("MewUI_Init succeeded!");
}

// Keep retrying startup until Mewjector is ready, then lets scene-ready updates drive UI work...
static VOID CALLBACK MewUI_TimerProc(PVOID parameter, BOOLEAN timer_or_wait_fired)
{
    (void)parameter;
    (void)timer_or_wait_fired;

    MewUI_EnsureInstallation();
}

int MewUI_Start(const char* owner_name, int hook_priority, uint32_t bootstrap_interval_ms, uint32_t ui_interval_ms, MewUITickCallback tick_callback, void* user_data)
{
    MewjectorAPI resolved_api;

    if (owner_name && owner_name[0] != '\0')
    {
        g_mew_ui_owner_name = owner_name;
    }

    (void)ui_interval_ms;

    g_mew_ui_hook_priority = hook_priority;
    g_mew_ui_bootstrap_interval_ms = bootstrap_interval_ms ? bootstrap_interval_ms : 100U;
    g_mew_ui_tick_callback = tick_callback;
    g_mew_ui_tick_user_data = user_data;

    memset(&resolved_api, 0, sizeof(resolved_api));

    if (MJ_Resolve(&resolved_api))
    {
        memcpy(&g_mew_ui_mj, &resolved_api, sizeof(g_mew_ui_mj));
        MewUI_APIDebugLog("Loading!");
    }

    if (InterlockedCompareExchange(&g_mew_ui_timer_started, 1, 0) != 0)
    {
        return 1;
    }

    g_mew_ui_timer_queue = CreateTimerQueue();

    if (!g_mew_ui_timer_queue)
    {
        InterlockedExchange(&g_mew_ui_timer_started, 0);
        return 0;
    }

    if (!CreateTimerQueueTimer(&g_mew_ui_timer, g_mew_ui_timer_queue, MewUI_TimerProc, NULL, g_mew_ui_bootstrap_interval_ms, g_mew_ui_bootstrap_interval_ms, WT_EXECUTEDEFAULT))
    {
        DeleteTimerQueue(g_mew_ui_timer_queue);
        g_mew_ui_timer_queue = NULL;
        g_mew_ui_timer = NULL;
        InterlockedExchange(&g_mew_ui_timer_started, 0);
        return 0;
    }

    MewUI_APIDebugLog("Bootstrap timer started!");
    return 1;
}

void MewUI_Stop(void)
{
    if (g_mew_ui_timer_queue)
    {
        DeleteTimerQueueEx(g_mew_ui_timer_queue, INVALID_HANDLE_VALUE);
        g_mew_ui_timer_queue = NULL;
        g_mew_ui_timer = NULL;
    }

    InterlockedExchange(&g_mew_ui_timer_started, 0);
    MewUI_APIDebugLog("Unloading!");

    if (InterlockedCompareExchange(&g_mew_ui_ready, 0, 0) != 0)
    {
        MewUI_Shutdown();
    }

    InterlockedExchange(&g_mew_ui_ready, 0);
    InterlockedExchange(&g_mew_ui_install_started, 0);
}

int MewUI_IsReady(void)
{
    return InterlockedCompareExchange(&g_mew_ui_ready, 0, 0) != 0;
}

const MewjectorAPI* MewUI_GetMewjector(void)
{
    if (!g_mew_ui_mj.GetGameBase)
    {
        return NULL;
    }

    return &g_mew_ui_mj;
}

int MewUI_Init(const MewjectorAPI* api, const char* owner_name, int hook_priority)
{
    if (!api || !api->GetGameBase || !api->InstallHook)
    {
        return 0;
    }

    memset(&g_mew_ui_mj, 0, sizeof(g_mew_ui_mj));
    memcpy(&g_mew_ui_mj, api, sizeof(g_mew_ui_mj));

    if (owner_name && owner_name[0] != '\0')
    {
        g_mew_ui_owner_name = owner_name;
    }

    g_mew_ui_game_base = g_mew_ui_mj.GetGameBase();

    if (!g_mew_ui_game_base)
    {
        return 0;
    }

    if (!g_mew_ui_mj.InstallHook(MEW_RVA_SCENE_READY_UPDATE, MEW_RVA_SCENE_READY_UPDATE_STOLEN_BYTES, (void*)MewUI_HookSceneReadyUpdate, (void**)&g_mew_ui_next_scene_ready_update, hook_priority, g_mew_ui_owner_name))
    {
        MewUI_APIDebugLog("Could not install scene-ready update hook!");
        return 0;
    }

    if (!g_mew_ui_mj.InstallHook(MEW_RVA_BUTTON_CAN_ACTIVATE, MEW_RVA_BUTTON_CAN_ACTIVATE_STOLEN_BYTES, (void*)MewUI_HookButtonCanActivate, (void**)&g_mew_ui_next_button_can_activate, hook_priority, g_mew_ui_owner_name))
    {
        MewUI_APIDebugLog("Could not install button can-activate hook!");
        return 0;
    }

    if (!g_mew_ui_mj.InstallHook(MEW_RVA_BUTTON_ACTIVATE, MEW_RVA_BUTTON_ACTIVATE_STOLEN_BYTES, (void*)MewUI_HookButtonActivate, (void**)&g_mew_ui_next_button_activate, hook_priority, g_mew_ui_owner_name))
    {
        MewUI_APIDebugLog("Could not install button activate hook!");
        return 0;
    }

    return 1;
}

void MewUI_Shutdown(void)
{
    memset(g_mew_ui_button_records, 0, sizeof(g_mew_ui_button_records));
    memset(&g_mew_ui_mj, 0, sizeof(g_mew_ui_mj));
    g_mew_ui_game_base = 0ULL;
    g_mew_ui_next_button_activate = NULL;
    g_mew_ui_next_button_can_activate = NULL;
    g_mew_ui_next_scene_ready_update = NULL;
    InterlockedExchange(&g_mew_ui_scene_tick_lock, 0);
    InterlockedExchange(&g_mew_ui_ready, 0);
    InterlockedExchange(&g_mew_ui_install_started, 0);
}

MewDirector* MewUI_GetMewDirector(void)
{
    MewDirector** director_ptr;

    if (!g_mew_ui_game_base)
    {
        return NULL;
    }

    director_ptr = (MewDirector**)MewUI_Address(MEW_RVA_MEWDIRECTOR_SINGLETON);

    __try
    {
        return *director_ptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return NULL;
    }
}

void* MewUI_GetSceneByName(const char* scene_name)
{
    MewDirector* mew_director;
    void** current;
    void** end;

    if (!scene_name)
    {
        return NULL;
    }

    mew_director = MewUI_GetMewDirector();

    if (!mew_director || !mew_director->director)
    {
        return NULL;
    }

    __try
    {
        current = mew_director->director->scenes.begin;
        end = mew_director->director->scenes.end;

        while (current && current < end)
        {
            void* scene_manager;
            MewNarrowString* scene_name_string;

            scene_manager = *current;
            current++;

            if (!scene_manager)
            {
                continue;
            }

            scene_name_string = (MewNarrowString*)((uint8_t*)scene_manager + MEW_OFF_SCENE_NAME);

            if (MewUI_NarrowStringEqualsLiteral(scene_name_string, scene_name))
            {
                return scene_manager;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return NULL;
    }

    return NULL;
}

static MewUISceneRefreshResult MewUI_FinishSceneBindingRefresh(MewUISceneBinding* binding, MewUISceneRefreshResult result, void* old_scene_manager, void* new_scene_manager)
{
    if (binding && binding->callback)
    {
        binding->callback(binding, result, old_scene_manager, new_scene_manager, binding->user_data);
    }

    return result;
}

void MewUI_InitSceneBinding(MewUISceneBinding* binding, const char* scene_name, MewUISceneRefreshCallback callback, void* user_data)
{
    if (!binding)
    {
        return;
    }

    memset(binding, 0, sizeof(*binding));

    if (scene_name)
    {
        strncpy(binding->scene_name, scene_name, sizeof(binding->scene_name) - 1U);
        binding->scene_name[sizeof(binding->scene_name) - 1U] = '\0';
    }

    binding->callback = callback;
    binding->user_data = user_data;
}

void MewUI_ClearSceneBinding(MewUISceneBinding* binding)
{
    if (!binding)
    {
        return;
    }

    memset(binding, 0, sizeof(*binding));
}

MewUISceneRefreshResult MewUI_RefreshSceneBinding(MewUISceneBinding* binding)
{
    void* old_scene_manager;
    void* new_scene_manager;

    if (!binding || binding->scene_name[0] == '\0')
    {
        return MEW_UI_SCENE_REFRESH_UNAVAILABLE;
    }

    old_scene_manager = binding->scene_manager;
    new_scene_manager = MewUI_GetSceneByName(binding->scene_name);

    if (!new_scene_manager || MewUI_IsSceneDestroying(new_scene_manager))
    {
        if (binding->active)
        {
            binding->active = 0U;
            binding->scene_manager = NULL;
            binding->generation++;
            return MewUI_FinishSceneBindingRefresh(binding, MEW_UI_SCENE_REFRESH_UNLOADED, old_scene_manager, NULL);
        }

        binding->scene_manager = NULL;
        return MEW_UI_SCENE_REFRESH_UNAVAILABLE;
    }

    if (!binding->active)
    {
        binding->active = 1U;
        binding->scene_manager = new_scene_manager;
        binding->generation++;
        return MewUI_FinishSceneBindingRefresh(binding, MEW_UI_SCENE_REFRESH_LOADED, NULL, new_scene_manager);
    }

    if (old_scene_manager != new_scene_manager)
    {
        binding->scene_manager = new_scene_manager;
        binding->generation++;
        return MewUI_FinishSceneBindingRefresh(binding, MEW_UI_SCENE_REFRESH_CHANGED, old_scene_manager, new_scene_manager);
    }

    return MEW_UI_SCENE_REFRESH_UNCHANGED;
}

void* MewUI_GetSceneBindingScene(const MewUISceneBinding* binding)
{
    if (!binding || !binding->active || !binding->scene_manager)
    {
        return NULL;
    }

    if (MewUI_IsSceneDestroying(binding->scene_manager))
    {
        return NULL;
    }

    return binding->scene_manager;
}

uint32_t MewUI_GetSceneBindingGeneration(const MewUISceneBinding* binding)
{
    if (!binding)
    {
        return 0U;
    }

    return binding->generation;
}

int MewUI_IsSceneBindingActive(const MewUISceneBinding* binding)
{
    return MewUI_GetSceneBindingScene(binding) != NULL;
}

const char* MewUI_GetSceneRefreshResultName(MewUISceneRefreshResult result)
{
    switch (result)
    {
        case MEW_UI_SCENE_REFRESH_UNAVAILABLE:
        {
            return "UNAVAILABLE";
        }

        case MEW_UI_SCENE_REFRESH_UNCHANGED:
        {
            return "UNCHANGED";
        }

        case MEW_UI_SCENE_REFRESH_LOADED:
        {
            return "LOADED";
        }

        case MEW_UI_SCENE_REFRESH_CHANGED:
        {
            return "CHANGED";
        }

        case MEW_UI_SCENE_REFRESH_UNLOADED:
        {
            return "UNLOADED";
        }

        default:
        {
            return "UNKNOWN";
        }
    }
}

void* MewUI_GetContextFromScene(void* scene_manager)
{
    MewFnContextFromManager context_from_manager;

    if (!scene_manager)
    {
        return NULL;
    }

    context_from_manager = (MewFnContextFromManager)MewUI_Address(MEW_RVA_CONTEXT_FROM_MANAGER);

    if (!context_from_manager)
    {
        return NULL;
    }

    __try
    {
        return context_from_manager(scene_manager);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return NULL;
    }
}

// Uses the engine child lookup so hierarchy traversal stays aligned with the game...
// These lookup routines consume the MewNarrowString argument before returning by the way!
static void* MewUI_FindChildByNameUsingRva(void* root_node, const char* child_name, UINT_PTR find_rva, const char* caller_name)
{
    MewFnFindChildByName find_child;
    MewFnInitNarrowString init_string;
    MewFnDestroyNarrowString destroy_string;
    MewNarrowString name_string;
    uint8_t name_initialized;
    uint8_t name_transferred_to_engine;
    void* result;

    if (!root_node || !child_name)
    {
        return NULL;
    }

    find_child = (MewFnFindChildByName)MewUI_Address(find_rva);
    init_string = (MewFnInitNarrowString)MewUI_Address(MEW_RVA_INIT_NARROW_STRING);
    destroy_string = (MewFnDestroyNarrowString)MewUI_Address(MEW_RVA_DESTROY_NARROW_STRING);

    if (!find_child || !init_string || !destroy_string)
    {
        return NULL;
    }

    memset(&name_string, 0, sizeof(name_string));
    name_initialized = 0U;
    name_transferred_to_engine = 0U;
    result = NULL;

    __try
    {
        init_string(&name_string, child_name);
        name_initialized = 1U;

        if (name_string.capacity > 15ULL)
        {
            MewUI_APIDebugLog("%s transferring heap search string to engine: root=%p child='%s' size=%llu capacity=%llu", caller_name ? caller_name : "FindChildByName", root_node, child_name, (unsigned long long)name_string.size, (unsigned long long)name_string.capacity);
        }

        name_transferred_to_engine = 1U;
        result = find_child(root_node, &name_string);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("%s failed: root=%p child='%s' transferred=%u", caller_name ? caller_name : "FindChildByName", root_node, child_name, (unsigned int)name_transferred_to_engine);
        result = NULL;
    }

    if (name_initialized && !name_transferred_to_engine)
    {
        __try
        {
            destroy_string(&name_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return result;
}

void* MewUI_FindChildByName(void* root_node, const char* child_name)
{
    return MewUI_FindChildByNameUsingRva(root_node, child_name, MEW_RVA_UI_FIND_CHILD_BY_NAME, "FindChildByName");
}

static void* MewUI_FindRootOwnedChildByName(void* root_node, const char* child_name)
{
    return MewUI_FindChildByNameUsingRva(root_node, child_name, MEW_RVA_FIND_UI_CHILD_ALT, "FindRootOwnedChildByName");
}

static void* MewUI_FindNodeByPathOrName(void* root_node, const char* node_name, uint8_t preserve_path_mode)
{
    MewFnFindNodeByPathOrName find_node;
    MewFnInitNarrowString init_string;
    MewFnDestroyNarrowString destroy_string;
    MewNarrowString name_string;
    uint8_t name_initialized;
    uint8_t name_transferred_to_engine;
    void* result;

    if (!root_node || !node_name)
    {
        return NULL;
    }

    find_node = (MewFnFindNodeByPathOrName)MewUI_Address(MEW_RVA_UI_FIND_NODE_BY_PATH_OR_NAME);
    init_string = (MewFnInitNarrowString)MewUI_Address(MEW_RVA_INIT_NARROW_STRING);
    destroy_string = (MewFnDestroyNarrowString)MewUI_Address(MEW_RVA_DESTROY_NARROW_STRING);

    if (!find_node || !init_string || !destroy_string)
    {
        return NULL;
    }

    memset(&name_string, 0, sizeof(name_string));
    name_initialized = 0U;
    name_transferred_to_engine = 0U;
    result = NULL;

    __try
    {
        init_string(&name_string, node_name);
        name_initialized = 1U;

        if (name_string.capacity > 15ULL)
        {
            MewUI_APIDebugLog("FindNodeByPathOrName transferring heap search string to engine: root=%p node='%s' size=%llu capacity=%llu", root_node, node_name, (unsigned long long)name_string.size, (unsigned long long)name_string.capacity);
        }

        name_transferred_to_engine = 1U;
        result = find_node(root_node, &name_string, preserve_path_mode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("FindNodeByPathOrName failed: root=%p node='%s' transferred=%u", root_node, node_name, (unsigned int)name_transferred_to_engine);
        result = NULL;
    }

    if (name_initialized && !name_transferred_to_engine)
    {
        __try
        {
            destroy_string(&name_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return result;
}

static int MewUI_InitWideStringFromWideData(MewWideString* out_string, const wchar_t* text, size_t length)
{
    if (!out_string || (!text && length != 0U))
    {
        return 0;
    }

    memset(out_string, 0, sizeof(*out_string));
    out_string->size = (uint64_t)length;

    if (length <= 7U)
    {
        out_string->capacity = 7ULL;

        if (length > 0U)
        {
            memcpy(out_string->storage.inline_buf, text, length * sizeof(wchar_t));
        }

        out_string->storage.inline_buf[length] = L'\0';
        return 1;
    }

    out_string->capacity = (uint64_t)length;
    out_string->storage.heap_ptr = (wchar_t*)malloc((length + 1U) * sizeof(wchar_t));

    if (!out_string->storage.heap_ptr)
    {
        memset(out_string, 0, sizeof(*out_string));
        return 0;
    }

    memcpy(out_string->storage.heap_ptr, text, length * sizeof(wchar_t));
    out_string->storage.heap_ptr[length] = L'\0';
    return 1;
}

static void MewUI_DestroyEngineWideString(MewWideString* value)
{
    MewFnDestroyWideString destroy_wide_string;

    if (!value)
    {
        return;
    }

    destroy_wide_string = (MewFnDestroyWideString)MewUI_Address(MEW_RVA_DESTROY_WIDE_STRING);

    if (!destroy_wide_string)
    {
        memset(value, 0, sizeof(*value));
        return;
    }

    __try
    {
        destroy_wide_string(value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    memset(value, 0, sizeof(*value));
}

static int MewUI_LocalizeKeyToWideString(const char* key, MewWideString* out_string)
{
    MewFnInitNarrowString init_string;
    MewFnDestroyNarrowString destroy_string;
    MewFnLocalizeNarrowKey localize_key;
    void* localization_manager;
    MewNarrowString key_string;
    uint8_t key_initialized;
    int result;

    if (!key || !out_string)
    {
        return 0;
    }

    init_string = (MewFnInitNarrowString)MewUI_Address(MEW_RVA_INIT_NARROW_STRING);
    destroy_string = (MewFnDestroyNarrowString)MewUI_Address(MEW_RVA_DESTROY_NARROW_STRING);
    localize_key = (MewFnLocalizeNarrowKey)MewUI_Address(MEW_RVA_LOCALIZE_NARROW_KEY);
    localization_manager = (void*)MewUI_Address(MEW_RVA_LOCALIZATION_MANAGER);

    if (!init_string || !destroy_string || !localize_key || !localization_manager)
    {
        return 0;
    }

    memset(&key_string, 0, sizeof(key_string));
    memset(out_string, 0, sizeof(*out_string));
    key_initialized = 0U;
    result = 0;

    __try
    {
        init_string(&key_string, key);
        key_initialized = 1U;
        localize_key(localization_manager, out_string, &key_string);
        result = out_string->size > 0ULL ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = 0;
    }

    if (key_initialized)
    {
        __try
        {
            destroy_string(&key_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    if (!result)
    {
        MewUI_DestroyEngineWideString(out_string);
    }

    return result;
}

static int MewUI_ParseLocalizationPlaceholder(const wchar_t* text, size_t length, size_t offset, uint32_t* out_index, size_t* out_token_length)
{
    size_t cursor;
    uint32_t index;
    uint8_t saw_digit;

    if (!text || !out_index || !out_token_length || offset >= length || text[offset] != L'{')
    {
        return 0;
    }

    cursor = offset + 1U;

    if (cursor >= length || text[cursor] != L'v')
    {
        return 0;
    }

    cursor++;
    index = 0U;
    saw_digit = 0U;

    while (cursor < length && text[cursor] >= L'0' && text[cursor] <= L'9')
    {
        saw_digit = 1U;
        index = (index * 10U) + (uint32_t)(text[cursor] - L'0');
        cursor++;
    }

    if (!saw_digit || cursor >= length || text[cursor] != L'}')
    {
        return 0;
    }

    *out_index = index;
    *out_token_length = (cursor - offset) + 1U;
    return 1;
}

static void MewUI_InitWideStringFromTemporaryWideData(MewWideString* out_string, const wchar_t* text, size_t length)
{
    if (!out_string)
    {
        return;
    }

    memset(out_string, 0, sizeof(*out_string));
    out_string->size = (uint64_t)length;

    if (length <= 7U)
    {
        out_string->capacity = 7ULL;

        if (text && length > 0U)
        {
            memcpy(out_string->storage.inline_buf, text, length * sizeof(wchar_t));
        }

        out_string->storage.inline_buf[length] = L'\0';
        return;
    }

    out_string->capacity = (uint64_t)length;
    out_string->storage.heap_ptr = (wchar_t*)text;
}

static int MewUI_FormatWideStringValues(const MewWideString* source_string, const char* const* values, uint32_t value_count, MewWideString* out_string)
{
    const wchar_t* source_data;
    size_t source_length;
    MewWideString* converted_values;
    wchar_t* output_data;
    size_t output_length;
    size_t source_index;
    size_t output_index;
    uint32_t value_index;
    int result;

    if (!source_string || !out_string)
    {
        return 0;
    }

    source_data = MewUI_GetWideStringData(source_string);
    source_length = MewUI_GetWideStringSize(source_string);

    if (!source_data && source_length != 0U)
    {
        return 0;
    }

    converted_values = NULL;

    if (value_count > 0U)
    {
        converted_values = (MewWideString*)calloc(value_count, sizeof(MewWideString));

        if (!converted_values)
        {
            return 0;
        }

        for (value_index = 0U; value_index < value_count; ++value_index)
        {
            if (values && values[value_index])
            {
                MewUI_InitWideStringFromText(&converted_values[value_index], values[value_index]);
            }
        }
    }

    output_length = 0U;
    source_index = 0U;

    while (source_index < source_length)
    {
        uint32_t placeholder_index;
        size_t token_length;

        if (MewUI_ParseLocalizationPlaceholder(source_data, source_length, source_index, &placeholder_index, &token_length) && placeholder_index < value_count && converted_values && values && values[placeholder_index])
        {
            output_length += MewUI_GetWideStringSize(&converted_values[placeholder_index]);
            source_index += token_length;
        }
        else
        {
            output_length++;
            source_index++;
        }
    }

    output_data = (wchar_t*)malloc((output_length + 1U) * sizeof(wchar_t));

    if (!output_data)
    {
        for (value_index = 0U; value_index < value_count; ++value_index)
        {
            MewUI_FreeWideStringFromText(&converted_values[value_index]);
        }

        free(converted_values);
        return 0;
    }

    output_index = 0U;
    source_index = 0U;

    while (source_index < source_length)
    {
        uint32_t placeholder_index;
        size_t token_length;

        if (MewUI_ParseLocalizationPlaceholder(source_data, source_length, source_index, &placeholder_index, &token_length) && placeholder_index < value_count && converted_values && values && values[placeholder_index])
        {
            const wchar_t* replacement_data;
            size_t replacement_length;

            replacement_data = MewUI_GetWideStringData(&converted_values[placeholder_index]);
            replacement_length = MewUI_GetWideStringSize(&converted_values[placeholder_index]);

            if (replacement_length > 0U && replacement_data)
            {
                memcpy(output_data + output_index, replacement_data, replacement_length * sizeof(wchar_t));
                output_index += replacement_length;
            }

            source_index += token_length;
        }
        else
        {
            output_data[output_index] = source_data[source_index];
            output_index++;
            source_index++;
        }
    }

    output_data[output_index] = L'\0';
    result = MewUI_InitWideStringFromWideData(out_string, output_data, output_index);
    free(output_data);

    for (value_index = 0U; value_index < value_count; ++value_index)
    {
        MewUI_FreeWideStringFromText(&converted_values[value_index]);
    }

    free(converted_values);
    return result;
}

static int MewUI_SetTextElementWideStringCopy(void* text_element, const MewWideString* text)
{
    MewFnCopyWideStringObject copy_wide_string;
    MewFnDestroyWideString destroy_wide_string;
    MewFnSetTextElementWideString set_text;
    MewWideString temporary_string;
    uint8_t temporary_initialized;
    uint8_t temporary_transferred_to_engine;
    int result;

    if (!text_element || !text)
    {
        return 0;
    }

    copy_wide_string = (MewFnCopyWideStringObject)MewUI_Address(MEW_RVA_COPY_WIDE_STRING_OBJECT);
    destroy_wide_string = (MewFnDestroyWideString)MewUI_Address(MEW_RVA_DESTROY_WIDE_STRING);
    set_text = (MewFnSetTextElementWideString)MewUI_Address(MEW_RVA_SET_TEXT_ELEMENT_WIDE_STRING);

    if (!copy_wide_string || !destroy_wide_string || !set_text)
    {
        return 0;
    }

    memset(&temporary_string, 0, sizeof(temporary_string));
    temporary_initialized = 0U;
    temporary_transferred_to_engine = 0U;
    result = 0;

    __try
    {
        copy_wide_string(&temporary_string, text);
        temporary_initialized = 1U;
        temporary_transferred_to_engine = 1U;
        set_text(text_element, &temporary_string, 0U, 1U);
        result = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("SetTextElementWideStringCopy failed: textElement=%p textSize=%llu transferred=%u", text_element, (unsigned long long)MewUI_GetWideStringSize(text), (unsigned int)temporary_transferred_to_engine);
        result = 0;
    }

    if (temporary_initialized && !temporary_transferred_to_engine)
    {
        __try
        {
            destroy_wide_string(&temporary_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return result;
}

static int MewUI_SetButtonLabelWideStringOnElement(void* label_text_element, const MewWideString* text, void** applied_targets, uint32_t* applied_target_count)
{
    uint32_t target_index;

    if (!label_text_element || !text || !applied_targets || !applied_target_count)
    {
        return 0;
    }

    for (target_index = 0U; target_index < *applied_target_count; ++target_index)
    {
        if (applied_targets[target_index] == label_text_element)
        {
            return 0;
        }
    }

    if (*applied_target_count < MEW_BUTTON_LABEL_TARGET_MAX)
    {
        applied_targets[*applied_target_count] = label_text_element;
        *applied_target_count = *applied_target_count + 1U;
    }

    return MewUI_SetTextElementWideStringCopy(label_text_element, text);
}

static int MewUI_SetButtonLabelWideStringOnStateName(void* button_node, const char* state_name, const MewWideString* text, void** applied_targets, uint32_t* applied_target_count)
{
    char label_path[MEW_TEXT_BUFFER_MAX];
    void* state_node;
    void* label_text_element;
    int applied_any;

    if (!button_node || !state_name || state_name[0] == '\0' || !text || !applied_targets || !applied_target_count)
    {
        return 0;
    }

    applied_any = 0;
    state_node = MewUI_FindNodeByPathOrName(button_node, state_name, 0U);

    if (state_node)
    {
        label_text_element = MewUI_FindNodeByPathOrName(state_node, "label", 0U);

        if (label_text_element && MewUI_SetButtonLabelWideStringOnElement(label_text_element, text, applied_targets, applied_target_count))
        {
            applied_any = 1;
        }
    }

    if (snprintf(label_path, sizeof(label_path), "%s.label", state_name) > 0)
    {
        label_path[sizeof(label_path) - 1U] = '\0';
        label_text_element = MewUI_FindNodeByPathOrName(button_node, label_path, 0U);

        if (label_text_element && MewUI_SetButtonLabelWideStringOnElement(label_text_element, text, applied_targets, applied_target_count))
        {
            applied_any = 1;
        }
    }

    return applied_any;
}

static const char* MewUI_GetButtonDefaultStateNodeName(MewButtonState state)
{
    switch (state)
    {
        case MEW_BUTTON_STATE_IDLE:
        {
            return "up";
        }

        case MEW_BUTTON_STATE_HOVERED:
        {
            return "over";
        }

        case MEW_BUTTON_STATE_PRESSED:
        {
            return "down";
        }

        case MEW_BUTTON_STATE_SELECTED:
        {
            return "selected";
        }

        case MEW_BUTTON_STATE_DISABLED:
        {
            return "disabled";
        }

        case MEW_BUTTON_STATE_TRANSITION:
        {
            return "enable";
        }

        default:
        {
            return NULL;
        }
    }
}

static int MewUI_CopyButtonStateNodeName(void* button, MewButtonState state, char* out_buffer, size_t out_buffer_size)
{
    MewNarrowString* state_name;
    uint32_t state_index;

    if (!button || !out_buffer || out_buffer_size == 0U || state < MEW_BUTTON_STATE_IDLE || state > MEW_BUTTON_STATE_TRANSITION)
    {
        return 0;
    }

    out_buffer[0] = '\0';
    state_index = (uint32_t)state;

    __try
    {
        state_name = (MewNarrowString*)((uint8_t*)button + MEW_OFF_BUTTON_STATE_NODE_NAMES + (state_index * sizeof(MewNarrowString)));

        if (MewUI_GetNarrowStringSize(state_name) > 0U)
        {
            MewUI_CopyNarrowStringToBuffer(state_name, out_buffer, out_buffer_size);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out_buffer[0] = '\0';
        return 0;
    }

    return out_buffer[0] != '\0' ? 1 : 0;
}

static int MewUI_SetButtonLabelWideString(void* button, const MewWideString* text)
{
    MewFnAssignWideStringData assign_wide_string;
    void* button_node;
    void* applied_targets[MEW_BUTTON_LABEL_TARGET_MAX];
    const char* default_state_name;
    char custom_state_name[MEW_TEXT_BUFFER_MAX];
    uint32_t applied_target_count;
    uint32_t state_index;
    int applied_any;

    if (!button || !text)
    {
        return 0;
    }

    assign_wide_string = (MewFnAssignWideStringData)MewUI_Address(MEW_RVA_ASSIGN_WIDE_STRING_DATA);

    if (!assign_wide_string)
    {
        return 0;
    }

    __try
    {
        button_node = *(void**)((uint8_t*)button + MEW_OFF_BUTTON_NODE);
        assign_wide_string((uint8_t*)button + MEW_OFF_BUTTON_LABEL_TEXT, MewUI_GetWideStringData(text), MewUI_GetWideStringSize(text));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("SetButtonLabelWideString failed while reading button node or assigning internal label! button=%p", button);
        return 0;
    }

    if (!button_node)
    {
        MewUI_APIDebugLog("SetButtonLabelWideString skipped: button node is NULL! button=%p", button);
        return 0;
    }

    memset(applied_targets, 0, sizeof(applied_targets));
    applied_target_count = 0U;
    applied_any = 0;

    {
        void* root_label_text_element;

        root_label_text_element = MewUI_FindNodeByPathOrName(button_node, "label", 0U);

        if (root_label_text_element && MewUI_SetButtonLabelWideStringOnElement(root_label_text_element, text, applied_targets, &applied_target_count))
        {
            applied_any = 1;
        }
    }

    for (state_index = 0U; state_index < MEW_BUTTON_STATE_NODE_NAME_COUNT; ++state_index)
    {
        MewButtonState state;

        state = (MewButtonState)state_index;
        custom_state_name[0] = '\0';

        if (MewUI_CopyButtonStateNodeName(button, state, custom_state_name, sizeof(custom_state_name)))
        {
            if (MewUI_SetButtonLabelWideStringOnStateName(button_node, custom_state_name, text, applied_targets, &applied_target_count))
            {
                applied_any = 1;
            }
        }

        default_state_name = MewUI_GetButtonDefaultStateNodeName(state);

        if (default_state_name && (custom_state_name[0] == '\0' || strcmp(custom_state_name, default_state_name) != 0))
        {
            if (MewUI_SetButtonLabelWideStringOnStateName(button_node, default_state_name, text, applied_targets, &applied_target_count))
            {
                applied_any = 1;
            }
        }
    }

    if (!applied_any)
    {
        MewUI_APIDebugLog("SetButtonLabelWideString skipped: no label text children were found for button=%p node=%p", button, button_node);
        return 0;
    }

    return 1;
}


// Shared text setter where the flag decides whether text means a key or direct text...
static int MewUI_SetTextElementInternal(void* text_element, const char* text, uint8_t use_localization_key, void* scene_manager)
{
    MewFnInitNarrowString init_string;
    MewFnDestroyNarrowString destroy_string;
    MewFnSetTextElementString set_text;
    MewNarrowString text_string;
    uint8_t text_initialized;
    uint8_t text_transferred_to_engine;
    int result;

    (void)scene_manager;

    if (!text_element || !text)
    {
        return 0;
    }

    init_string = (MewFnInitNarrowString)MewUI_Address(MEW_RVA_INIT_NARROW_STRING);
    destroy_string = (MewFnDestroyNarrowString)MewUI_Address(MEW_RVA_DESTROY_NARROW_STRING);
    set_text = (MewFnSetTextElementString)MewUI_Address(MEW_RVA_SET_TEXT_ELEMENT_STRING);

    if (!init_string || !destroy_string || !set_text)
    {
        return 0;
    }

    memset(&text_string, 0, sizeof(text_string));
    text_initialized = 0U;
    text_transferred_to_engine = 0U;
    result = 0;

    __try
    {
        init_string(&text_string, text);
        text_initialized = 1U;

        if (text_string.capacity > 15ULL)
        {
            MewUI_APIDebugLog("SetTextElementInternal transferring heap text string to engine: textElement=%p useLocalization=%u text='%s' size=%llu capacity=%llu", text_element, (unsigned int)use_localization_key, text, (unsigned long long)text_string.size, (unsigned long long)text_string.capacity);
        }

        text_transferred_to_engine = 1U;
        set_text(text_element, &text_string, use_localization_key, 1U);
        result = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("SetTextElementInternal failed: textElement=%p useLocalization=%u text='%s' transferred=%u", text_element, (unsigned int)use_localization_key, text, (unsigned int)text_transferred_to_engine);
        result = 0;
    }

    if (text_initialized && !text_transferred_to_engine)
    {
        __try
        {
            destroy_string(&text_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return result;
}

int MewUI_SetTextElementFromLocalizationKey(void* text_element, const char* key)
{
    return MewUI_SetTextElementInternal(text_element, key, 1U, NULL);
}

int MewUI_SetTextElementFromLocalizationKeyValue(void* text_element, const char* key, const char* value0)
{
    const char* values[1];

    values[0] = value0;
    return MewUI_SetTextElementFromLocalizationKeyValues(text_element, key, values, 1U);
}

int MewUI_SetTextElementFromLocalizationKeyValues(void* text_element, const char* key, const char* const* values, uint32_t value_count)
{
    MewWideString localized_string;
    MewWideString formatted_string;
    int result;

    if (!text_element || !key)
    {
        return 0;
    }

    memset(&localized_string, 0, sizeof(localized_string));
    memset(&formatted_string, 0, sizeof(formatted_string));
    result = 0;

    if (!MewUI_LocalizeKeyToWideString(key, &localized_string))
    {
        return 0;
    }

    if (MewUI_FormatWideStringValues(&localized_string, values, value_count, &formatted_string))
    {
        result = MewUI_SetTextElementWideStringCopy(text_element, &formatted_string);
        MewUI_FreeWideStringFromText(&formatted_string);
    }

    MewUI_DestroyEngineWideString(&localized_string);
    return result;
}

int MewUI_SetTextChildFromLocalizationKey(void* root_node, const char* child_name, const char* key)
{
    void* text_element;

    text_element = MewUI_FindChildByName(root_node, child_name);

    if (!text_element)
    {
        return 0;
    }

    return MewUI_SetTextElementFromLocalizationKey(text_element, key);
}

int MewUI_SetTextChildFromLocalizationKeyValue(void* root_node, const char* child_name, const char* key, const char* value0)
{
    const char* values[1];

    values[0] = value0;
    return MewUI_SetTextChildFromLocalizationKeyValues(root_node, child_name, key, values, 1U);
}

int MewUI_SetTextChildFromLocalizationKeyValues(void* root_node, const char* child_name, const char* key, const char* const* values, uint32_t value_count)
{
    void* text_element;

    text_element = MewUI_FindChildByName(root_node, child_name);

    if (!text_element)
    {
        return 0;
    }

    return MewUI_SetTextElementFromLocalizationKeyValues(text_element, key, values, value_count);
}

int MewUI_SetButtonRoleName(void* button, const char* role_name)
{
    MewFnAssignNarrowStringLiteral assign_string;
    size_t length;

    if (!button || !role_name)
    {
        return 0;
    }

    assign_string = (MewFnAssignNarrowStringLiteral)MewUI_Address(MEW_RVA_ASSIGN_NARROW_STRING_LITERAL);

    if (!assign_string)
    {
        return 0;
    }

    length = strlen(role_name);

    __try
    {
        assign_string((uint8_t*)button + MEW_OFF_BUTTON_ROLE_NAME, role_name, (uint64_t)length);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    return 1;
}

int MewUI_GetButtonRoleName(void* button, char* out_buffer, size_t out_buffer_size)
{
    MewNarrowString* role_string;
    const char* data;
    size_t length;

    if (!button || !out_buffer || out_buffer_size == 0U)
    {
        return 0;
    }

    out_buffer[0] = '\0';

    __try
    {
        role_string = (MewNarrowString*)((uint8_t*)button + MEW_OFF_BUTTON_ROLE_NAME);
        data = MewUI_GetNarrowStringData(role_string);
        length = MewUI_GetNarrowStringSize(role_string);

        if (!data || length == 0U || length >= 4096U)
        {
            return 0;
        }

        if (length >= out_buffer_size)
        {
            length = out_buffer_size - 1U;
        }

        memcpy(out_buffer, data, length);
        out_buffer[length] = '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out_buffer[0] = '\0';
        return 0;
    }

    return 1;
}

MewButtonState MewUI_GetButtonState(void* button)
{
    if (!button)
    {
        return MEW_BUTTON_STATE_INVALID;
    }

    __try
    {
        return (MewButtonState)(*(int32_t*)((uint8_t*)button + MEW_OFF_BUTTON_STATE));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return MEW_BUTTON_STATE_INVALID;
    }
}

int MewUI_SetButtonState(void* button, MewButtonState state)
{
    if (!button)
    {
        return 0;
    }

    __try
    {
        *(int32_t*)((uint8_t*)button + MEW_OFF_BUTTON_STATE) = (int32_t)state;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    return 1;
}

int MewUI_SetButtonEnabled(void* button, int enabled)
{
    if (!button)
    {
        return 0;
    }

    __try
    {
        *(uint8_t*)((uint8_t*)button + MEW_OFF_COMPONENT_ENABLED) = enabled ? 1U : 0U;
        *(uint8_t*)((uint8_t*)button + MEW_OFF_BUTTON_ACTIVATE_BYTE) = enabled ? 1U : 0U;

        if (!enabled)
        {
            *(int32_t*)((uint8_t*)button + MEW_OFF_BUTTON_STATE) = (int32_t)MEW_BUTTON_STATE_DISABLED;
        }
        else if (*(int32_t*)((uint8_t*)button + MEW_OFF_BUTTON_STATE) == (int32_t)MEW_BUTTON_STATE_DISABLED)
        {
            *(int32_t*)((uint8_t*)button + MEW_OFF_BUTTON_STATE) = (int32_t)MEW_BUTTON_STATE_IDLE;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    return 1;
}

int MewUI_SetButtonInteractable(void* button, int interactable)
{
    MewButtonRecord* record;

    record = MewUI_GetButtonRecord(button);

    if (!record)
    {
        return 0;
    }

    record->interact_override = interactable ? MEW_BUTTON_INTERACT_FORCE_ENABLED : MEW_BUTTON_INTERACT_FORCE_DISABLED;
    record->can_interact_callback = NULL;
    record->can_interact_user_data = NULL;

    if (!interactable)
    {
        MewUI_SetButtonState(button, MEW_BUTTON_STATE_DISABLED);
    }
    else if (MewUI_GetButtonState(button) == MEW_BUTTON_STATE_DISABLED)
    {
        MewUI_SetButtonState(button, MEW_BUTTON_STATE_IDLE);
    }

    return 1;
}

int MewUI_ClearButtonInteractOverride(void* button)
{
    MewButtonRecord* record;

    record = MewUI_GetButtonRecord(button);

    if (!record)
    {
        return 0;
    }

    record->interact_override = MEW_BUTTON_INTERACT_GAME_DEFAULT;
    record->can_interact_callback = NULL;
    record->can_interact_user_data = NULL;

    return 1;
}

int MewUI_SetButtonCanInteractCallback(void* button, MewButtonCanInteractCallback callback, void* user_data)
{
    MewButtonRecord* record;

    record = MewUI_GetButtonRecord(button);

    if (!record || !callback)
    {
        return 0;
    }

    record->interact_override = MEW_BUTTON_INTERACT_CALLBACK;
    record->can_interact_callback = callback;
    record->can_interact_user_data = user_data;

    return 1;
}

// Tries UTF-8 first, then falls back to the local ANSI code page for older text...
static int MewUI_InitWideStringFromText(MewWideString* out_string, const char* text)
{
    int required_length;
    int converted_length;
    UINT code_page;

    if (!out_string || !text)
    {
        return 0;
    }

    memset(out_string, 0, sizeof(*out_string));

    code_page = CP_UTF8;
    required_length = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);

    if (required_length <= 0)
    {
        code_page = CP_ACP;
        required_length = MultiByteToWideChar(code_page, 0, text, -1, NULL, 0);
    }

    if (required_length <= 0)
    {
        return 0;
    }

    out_string->size = (uint64_t)(required_length - 1);

    if (out_string->size <= 7ULL)
    {
        out_string->capacity = 7ULL;
        converted_length = MultiByteToWideChar(code_page, code_page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, text, -1, out_string->storage.inline_buf, 8);
    }
    else
    {
        out_string->capacity = out_string->size;
        out_string->storage.heap_ptr = (wchar_t*)malloc((size_t)required_length * sizeof(wchar_t));

        if (!out_string->storage.heap_ptr)
        {
            memset(out_string, 0, sizeof(*out_string));
            return 0;
        }

        converted_length = MultiByteToWideChar(code_page, code_page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, text, -1, out_string->storage.heap_ptr, required_length);
    }

    if (converted_length <= 0)
    {
        if (out_string->capacity > 7ULL)
        {
            free(out_string->storage.heap_ptr);
        }

        memset(out_string, 0, sizeof(*out_string));
        return 0;
    }

    return 1;
}

static void MewUI_FreeWideStringFromText(MewWideString* value)
{
    if (!value)
    {
        return;
    }

    if (value->capacity > 7ULL && value->storage.heap_ptr)
    {
        free(value->storage.heap_ptr);
    }

    memset(value, 0, sizeof(*value));
}

int MewUI_SetButtonLabelText(void* button, const char* text)
{
    MewWideString label_string;
    int result;

    if (!button || !text)
    {
        return 0;
    }

    memset(&label_string, 0, sizeof(label_string));

    if (!MewUI_InitWideStringFromText(&label_string, text))
    {
        return 0;
    }

    result = MewUI_SetButtonLabelWideString(button, &label_string);
    MewUI_FreeWideStringFromText(&label_string);
    return result;
}

int MewUI_ClearButtonLabel(void* button)
{
    return MewUI_SetButtonLabelText(button, "");
}

int MewUI_SetButtonLabelFromLocalizationKey(void* button, const char* key)
{
    MewWideString localized_string;
    int result;

    if (!button || !key)
    {
        return 0;
    }

    memset(&localized_string, 0, sizeof(localized_string));
    result = 0;

    if (MewUI_LocalizeKeyToWideString(key, &localized_string))
    {
        result = MewUI_SetButtonLabelWideString(button, &localized_string);
        MewUI_DestroyEngineWideString(&localized_string);
    }

    return result;
}

int MewUI_SetButtonLabelFromLocalizationKeyValue(void* button, const char* key, const char* value0)
{
    const char* values[1];

    values[0] = value0;
    return MewUI_SetButtonLabelFromLocalizationKeyValues(button, key, values, 1U);
}

int MewUI_SetButtonLabelFromLocalizationKeyValues(void* button, const char* key, const char* const* values, uint32_t value_count)
{
    MewWideString localized_string;
    MewWideString formatted_string;
    int result;

    if (!button || !key)
    {
        return 0;
    }

    memset(&localized_string, 0, sizeof(localized_string));
    memset(&formatted_string, 0, sizeof(formatted_string));
    result = 0;

    if (!MewUI_LocalizeKeyToWideString(key, &localized_string))
    {
        return 0;
    }

    if (MewUI_FormatWideStringValues(&localized_string, values, value_count, &formatted_string))
    {
        result = MewUI_SetButtonLabelWideString(button, &formatted_string);
        MewUI_FreeWideStringFromText(&formatted_string);
    }

    MewUI_DestroyEngineWideString(&localized_string);
    return result;
}

void* MewUI_CreateButtonFromNode(const MewButtonCreateInfo* create_info)
{
    MewFnAllocComponentFromType alloc_component;
    MewFnButtonConstruct construct_button;
    MewFnSceneRegisterComponent register_component;
    MewFnContextAttachComponentRef attach_component_ref;
    MewFnButtonSetupFromNode setup_from_node;
    MewFnInitNarrowString init_string;
    MewFnDestroyNarrowString destroy_string;
    MewButtonRecord* record;
    MewNarrowString label_key_string;
    uint8_t callback_scratch[MEW_SIZE_CALLBACK_BINDING_SCRATCH];
    const char* setup_label_key;
    uint8_t label_key_initialized;
    uint8_t label_key_transferred_to_engine;
    void* scene_manager;
    void* context;
    void* node;
    void* button;

    if (!create_info || !create_info->scene_manager)
    {
        MewUI_APIDebugLog("CreateButtonFromNode refused: missing create_info or scene_manager!");
        return NULL;
    }

    if (!MewUI_BeginSetupGuard())
    {
        MewUI_APIDebugLog("CreateButtonFromNode refused: setup guard is already active! scene=%p node='%s' role='%s'", create_info->scene_manager, create_info->node_name ? create_info->node_name : "", create_info->role_name ? create_info->role_name : "");
        return NULL;
    }

    scene_manager = create_info->scene_manager;
    context = create_info->context;
    node = create_info->button_node;

    MewUI_APIDebugLog("CreateButtonFromNode begin: scene=%p context=%p root=%p node=%p nodeName='%s' role='%s' labelKey='%s'", scene_manager, context, create_info->root_node, node, create_info->node_name ? create_info->node_name : "", create_info->role_name ? create_info->role_name : "", create_info->label_key ? create_info->label_key : "");

    if (!node && create_info->root_node && create_info->node_name)
    {
        node = MewUI_FindChildByName(create_info->root_node, create_info->node_name);
        MewUI_APIDebugLog("CreateButtonFromNode root search: nodeName='%s' found=%p", create_info->node_name, node);
    }

    if (!node)
    {
        MewUI_APIDebugLog("CreateButtonFromNode refused: node not found! scene=%p nodeName='%s' role='%s'", scene_manager, create_info->node_name ? create_info->node_name : "", create_info->role_name ? create_info->role_name : "");
        MewUI_EndSetupGuard();
        return NULL;
    }

    __try
    {
        if (*(uint8_t*)((uint8_t*)scene_manager + MEW_OFF_SCENE_DOING_DESTRUCTION))
        {
            MewUI_APIDebugLog("CreateButtonFromNode refused: scene is destroying! scene=%p node=%p role='%s'", scene_manager, node, create_info->role_name ? create_info->role_name : "");
            MewUI_EndSetupGuard();
            return NULL;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("CreateButtonFromNode refused: exception while checking scene destruction! scene=%p node=%p", scene_manager, node);
        MewUI_EndSetupGuard();
        return NULL;
    }

    if (!context)
    {
        context = MewUI_GetContextFromScene(scene_manager);
        MewUI_APIDebugLog("CreateButtonFromNode context helper returned context=%p for scene=%p node=%p role='%s'", context, scene_manager, node, create_info->role_name ? create_info->role_name : "");
    }

    if (!context)
    {
        MewUI_APIDebugLog("CreateButtonFromNode refused: no context! scene=%p node=%p role='%s'", scene_manager, node, create_info->role_name ? create_info->role_name : "");
        MewUI_EndSetupGuard();
        return NULL;
    }

    alloc_component = (MewFnAllocComponentFromType)MewUI_Address(MEW_RVA_ALLOC_COMPONENT_FROM_TYPE);
    construct_button = (MewFnButtonConstruct)MewUI_Address(MEW_RVA_BUTTON_CONSTRUCT);
    register_component = (MewFnSceneRegisterComponent)MewUI_Address(MEW_RVA_SCENE_REGISTER_COMPONENT);
    attach_component_ref = (MewFnContextAttachComponentRef)MewUI_Address(MEW_RVA_CONTEXT_ATTACH_COMPONENT_REF);
    setup_from_node = (MewFnButtonSetupFromNode)MewUI_Address(MEW_RVA_BUTTON_SETUP_FROM_NODE);
    init_string = (MewFnInitNarrowString)MewUI_Address(MEW_RVA_INIT_NARROW_STRING);
    destroy_string = (MewFnDestroyNarrowString)MewUI_Address(MEW_RVA_DESTROY_NARROW_STRING);

    if (!alloc_component || !construct_button || !register_component || !attach_component_ref || !setup_from_node || !init_string || !destroy_string)
    {
        MewUI_APIDebugLog("CreateButtonFromNode refused: missing engine function. alloc=%p construct=%p register=%p attach=%p setup=%p init=%p destroy=%p", alloc_component, construct_button, register_component, attach_component_ref, setup_from_node, init_string, destroy_string);
        MewUI_EndSetupGuard();
        return NULL;
    }

    button = NULL;

    __try
    {
        button = alloc_component((void*)MewUI_Address(MEW_RVA_BUTTON_TYPE_DESCRIPTOR));

        if (!button)
        {
            MewUI_APIDebugLog("CreateButtonFromNode failed: allocator returned NULL. scene=%p node=%p role='%s'", scene_manager, node, create_info->role_name ? create_info->role_name : "");
            MewUI_EndSetupGuard();
            return NULL;
        }

        memset(button, 0, MEW_SIZE_BUTTON);
        button = construct_button(button);

        if (!button)
        {
            MewUI_APIDebugLog("CreateButtonFromNode failed: constructor returned NULL. scene=%p node=%p role='%s'", scene_manager, node, create_info->role_name ? create_info->role_name : "");
            MewUI_EndSetupGuard();
            return NULL;
        }

        *(void**)((uint8_t*)button + MEW_OFF_COMPONENT_CONTEXT) = context;
        *(void**)((uint8_t*)button + MEW_OFF_COMPONENT_MANAGER) = scene_manager;
        *(void**)((uint8_t*)button + MEW_OFF_COMPONENT_MANAGER_HEADER) = *(void**)scene_manager;
        register_component(scene_manager, button);
        *(uint8_t*)((uint8_t*)button + MEW_OFF_COMPONENT_FLAGS_A) = (uint8_t)((*(uint8_t*)((uint8_t*)button + MEW_OFF_COMPONENT_FLAGS_A) & MEW_COMPONENT_FLAGS_A_KEEP_SETUP_MASK) | MEW_COMPONENT_FLAGS_A_SET_SETUP_MASK);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("CreateButtonFromNode failed during alloc/construct/register. scene=%p context=%p node=%p button=%p", scene_manager, context, node, button);
        MewUI_EndSetupGuard();
        return NULL;
    }

    if (!MewUI_AppendSceneComponentList(scene_manager, MEW_OFF_MANAGER_BUCKET_UPDATE_CAPACITY, MEW_OFF_MANAGER_BUCKET_UPDATE_SIZE, MEW_OFF_MANAGER_BUCKET_UPDATE_DATA, button))
    {
        MewUI_APIDebugLog("CreateButtonFromNode failed: update bucket append failed! scene=%p button=%p", scene_manager, button);
        MewUI_EndSetupGuard();
        return NULL;
    }

    __try
    {
        *(uint8_t*)((uint8_t*)button + MEW_OFF_COMPONENT_FLAGS_A) |= MEW_COMPONENT_FLAGS_A_CLICKABLE_MASK;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("CreateButtonFromNode failed: could not set clickable flag! button=%p", button);
        MewUI_EndSetupGuard();
        return NULL;
    }

    if (!MewUI_AppendSceneComponentList(scene_manager, MEW_OFF_MANAGER_BUCKET_CLICK_CAPACITY, MEW_OFF_MANAGER_BUCKET_CLICK_SIZE, MEW_OFF_MANAGER_BUCKET_CLICK_DATA, button))
    {
        MewUI_APIDebugLog("CreateButtonFromNode failed: click bucket append failed! scene=%p button=%p", scene_manager, button);
        MewUI_EndSetupGuard();
        return NULL;
    }

    __try
    {
        *(uint8_t*)((uint8_t*)button + MEW_OFF_COMPONENT_FLAGS_A) &= MEW_COMPONENT_FLAGS_A_POST_CLICK_MASK;
        attach_component_ref((uint8_t*)context + MEW_OFF_CONTEXT_COMPONENT_REFS, &button);
        *(uint8_t*)((uint8_t*)button + MEW_OFF_COMPONENT_LAYER) = *(uint8_t*)((uint8_t*)context + MEW_OFF_CONTEXT_LAYER);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("CreateButtonFromNode failed: context attach/layer copy failed! context=%p button=%p", context, button);
        MewUI_EndSetupGuard();
        return NULL;
    }

    memset(&label_key_string, 0, sizeof(label_key_string));
    memset(callback_scratch, 0, sizeof(callback_scratch));
    label_key_initialized = 0U;
    label_key_transferred_to_engine = 0U;

    setup_label_key = create_info->label_key ? create_info->label_key : "";

    __try
    {
        // Native setup owns the button initial label path, so leave that part alone...
        // The safety bit is keeping mod-created buttons out of the Button render bucket...
        init_string(&label_key_string, setup_label_key);
        label_key_initialized = 1U;

        if (label_key_string.capacity > 15ULL)
        {
            MewUI_APIDebugLog("CreateButtonFromNode transferring heap label key to setup: button=%p node=%p labelKey='%s' size=%llu capacity=%llu", button, node, setup_label_key, (unsigned long long)label_key_string.size, (unsigned long long)label_key_string.capacity);
        }

        label_key_transferred_to_engine = 1U;
        setup_from_node(button, node, callback_scratch, &label_key_string);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (label_key_initialized && !label_key_transferred_to_engine)
        {
            __try
            {
                destroy_string(&label_key_string);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        MewUI_APIDebugLog("CreateButtonFromNode failed: setup-from-node raised an exception. button=%p node=%p labelKey='%s' transferred=%u", button, node, create_info->label_key ? create_info->label_key : "", (unsigned int)label_key_transferred_to_engine);
        MewUI_EndSetupGuard();
        return NULL;
    }

    if (create_info->role_name)
    {
        if (!MewUI_SetButtonRoleName(button, create_info->role_name))
        {
            MewUI_APIDebugLog("CreateButtonFromNode warning: failed to set roleName='%s' button=%p", create_info->role_name, button);
        }
    }

    // Native setup applies the root label first, then this mirrors it into every state label...
    // That includes custom state-node names Button::Update may swap to later...
    if (create_info->label_key)
    {
        if (!MewUI_SetButtonLabelFromLocalizationKey(button, create_info->label_key))
        {
            MewUI_APIDebugLog("CreateButtonFromNode warning: failed to apply localization label to state labels! button=%p key='%s'", button, create_info->label_key);
        }
    }
    else if (create_info->label_text)
    {
        if (!MewUI_SetButtonLabelText(button, create_info->label_text))
        {
            MewUI_APIDebugLog("CreateButtonFromNode warning: failed to set literal label text! button=%p", button);
        }
    }

    __try
    {
        *(uint8_t*)((uint8_t*)button + MEW_OFF_COMPONENT_ENABLED) = create_info->enabled ? 1U : 0U;
        *(uint8_t*)((uint8_t*)button + MEW_OFF_BUTTON_ACTIVATE_BYTE) = create_info->activate_enabled ? 1U : 0U;
        *(uint8_t*)((uint8_t*)button + MEW_OFF_BUTTON_MOUSE_STRICT_BYTE) = create_info->strict_mouse ? 1U : 0U;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("CreateButtonFromNode warning: failed to set enabled/activation bytes! button=%p", button);
    }

    record = MewUI_AllocButtonRecord(button);

    if (record)
    {
        record->owned_by_ui = 1U;
        record->scene_manager = scene_manager;
        record->context = context;
        record->root_node = create_info->root_node;
        record->button_node = node;
        record->callback = create_info->callback;
        record->user_data = create_info->user_data;
        record->interact_override = create_info->interact_override;
        record->can_interact_callback = create_info->can_interact_callback;
        record->can_interact_user_data = create_info->can_interact_user_data;
        record->click_from_hook_seen = 0U;
        record->last_state = MewUI_GetButtonState(button);

        if (create_info->role_name)
        {
            strncpy(record->role_name, create_info->role_name, sizeof(record->role_name) - 1U);
            record->role_name[sizeof(record->role_name) - 1U] = '\0';
        }

        MewUI_APIDebugLog("CreateButtonFromNode succeeded: button=%p scene=%p context=%p node=%p role='%s' initialState=%s", button, scene_manager, context, node, record->role_name, MewUI_GetButtonStateName(record->last_state));
    }
    else
    {
        MewUI_APIDebugLog("CreateButtonFromNode warning: button created but no tracking record was available. button=%p", button);
    }

    MewUI_EndSetupGuard();
    return button;
}

void* MewUI_CreateButtonFromNamedNode(const MewButtonCreateInfo* create_info)
{
    MewButtonCreateInfo copy;

    if (!create_info)
    {
        return NULL;
    }

    copy = *create_info;

    if (!copy.button_node && copy.root_node && copy.node_name)
    {
        copy.button_node = MewUI_FindChildByName(copy.root_node, copy.node_name);
    }

    return MewUI_CreateButtonFromNode(&copy);
}

int MewUI_IsSceneDestroying(void* scene_manager)
{
    if (!scene_manager)
    {
        return 1;
    }

    __try
    {
        return *(uint8_t*)((uint8_t*)scene_manager + MEW_OFF_SCENE_DOING_DESTRUCTION) != 0U;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 1;
    }
}

int MewUI_IsComponentInScene(void* scene_manager, void* component)
{
    MewPodVectorPtr* list;
    uint32_t index;

    if (!scene_manager || !component)
    {
        return 0;
    }

    if (MewUI_IsSceneDestroying(scene_manager))
    {
        return 0;
    }

    list = MewUI_GetSceneComponentList(scene_manager);

    if (!list)
    {
        return 0;
    }

    __try
    {
        for (index = 0U; index < list->size; ++index)
        {
            if (list->data[index] == component)
            {
                return 1;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    return 0;
}

static int MewUI_IsTrackedButtonForScene(void* scene_manager, void* button)
{
    MewButtonRecord* record;

    if (!scene_manager || !button)
    {
        return 0;
    }

    if (MewUI_IsSceneDestroying(scene_manager))
    {
        return 0;
    }

    record = MewUI_GetButtonRecord(button);

    if (!record || !record->used || record->button != button)
    {
        return 0;
    }

    if (record->scene_manager != scene_manager)
    {
        return 0;
    }

    if (MewUI_GetButtonState(button) == MEW_BUTTON_STATE_INVALID)
    {
        memset(record, 0, sizeof(*record));
        return 0;
    }

    return 1;
}

void* MewUI_SetupButtonFromNode(const MewButtonCreateInfo* create_info, void** io_button, int* out_created)
{
    MewButtonCreateInfo copy;
    void* existing_button;
    void* created_button;

    if (out_created)
    {
        *out_created = 0;
    }

    if (!create_info || !create_info->scene_manager || !create_info->node_name)
    {
        if (io_button)
        {
            *io_button = NULL;
        }

        return NULL;
    }

    if (MewUI_IsSceneDestroying(create_info->scene_manager))
    {
        MewUI_APIDebugLog("SetupButtonFromNode waiting: scene is destroying. scene=%p node='%s' role='%s'", create_info->scene_manager, create_info->node_name ? create_info->node_name : "", create_info->role_name ? create_info->role_name : "");

        if (io_button)
        {
            *io_button = NULL;
        }

        return NULL;
    }

    if (io_button && *io_button)
    {
        if (MewUI_IsTrackedButtonForScene(create_info->scene_manager, *io_button))
        {
            if (create_info->label_key)
            {
                MewUI_SetButtonLabelFromLocalizationKey(*io_button, create_info->label_key);
            }
            else if (create_info->label_text)
            {
                MewUI_SetButtonLabelText(*io_button, create_info->label_text);
            }

            return *io_button;
        }

        if (MewUI_IsComponentInScene(create_info->scene_manager, *io_button))
        {
            if (create_info->label_key)
            {
                MewUI_SetButtonLabelFromLocalizationKey(*io_button, create_info->label_key);
            }
            else if (create_info->label_text)
            {
                MewUI_SetButtonLabelText(*io_button, create_info->label_text);
            }

            return *io_button;
        }

        *io_button = NULL;
    }

    existing_button = NULL;

    if (create_info->role_name)
    {
        existing_button = MewUI_FindButtonByRole(create_info->scene_manager, create_info->role_name);

        if (existing_button)
        {
            if (!MewUI_IsButtonTracked(existing_button))
            {
                MewUI_RegisterExistingButton(existing_button, create_info->role_name, create_info->callback, create_info->user_data);
            }

            if (io_button)
            {
                *io_button = existing_button;
            }

            return existing_button;
        }
    }

    copy = *create_info;

    if (!copy.button_node)
    {
        copy.button_node = MewUI_FindChildByNameInScene(copy.scene_manager, copy.node_name);
    }

    if (!copy.button_node)
    {
        MewUI_APIDebugLog("SetupButtonFromNode waiting: node not found! scene=%p node='%s' role='%s'", copy.scene_manager, copy.node_name ? copy.node_name : "", copy.role_name ? copy.role_name : "");

        if (io_button)
        {
            *io_button = NULL;
        }

        return NULL;
    }

    created_button = MewUI_CreateButtonFromNode(&copy);

    if (!created_button)
    {
        if (io_button)
        {
            *io_button = NULL;
        }

        return NULL;
    }

    if (io_button)
    {
        *io_button = created_button;
    }

    if (out_created)
    {
        *out_created = 1;
    }

    return created_button;
}

void* MewUI_SetupButtonInScene(const MewNamedButtonCreateInfo* create_info, void** io_button, int* out_created)
{
    MewButtonCreateInfo scene_create_info;
    void* scene_manager;
    void* button;

    if (out_created)
    {
        *out_created = 0;
    }

    if (!create_info || !create_info->scene_name || !create_info->node_name)
    {
        if (io_button)
        {
            *io_button = NULL;
        }

        return NULL;
    }

    scene_manager = MewUI_GetSceneByName(create_info->scene_name);

    if (!scene_manager)
    {
        if (io_button)
        {
            *io_button = NULL;
        }

        return NULL;
    }

    memset(&scene_create_info, 0, sizeof(scene_create_info));
    scene_create_info.scene_manager = scene_manager;
    scene_create_info.node_name = create_info->node_name;
    scene_create_info.role_name = create_info->role_name;
    scene_create_info.label_key = create_info->label_key;
    scene_create_info.label_text = create_info->label_text;
    scene_create_info.enabled = create_info->enabled;
    scene_create_info.activate_enabled = create_info->activate_enabled;
    scene_create_info.strict_mouse = create_info->strict_mouse;
    scene_create_info.interact_override = create_info->interact_override;
    scene_create_info.can_interact_callback = create_info->can_interact_callback;
    scene_create_info.can_interact_user_data = create_info->can_interact_user_data;
    scene_create_info.callback = create_info->callback;
    scene_create_info.user_data = create_info->user_data;

    button = MewUI_SetupButtonFromNode(&scene_create_info, io_button, out_created);

    if (button && create_info->interact_override == MEW_BUTTON_INTERACT_FORCE_ENABLED)
    {
        MewUI_SetButtonInteractable(button, 1);
    }

    return button;
}

void* MewUI_SetupButtonFromLocalizationKey(const char* scene_name, const char* node_name, const char* role_name, const char* label_key, MewButtonCallback callback, void* user_data, void** io_button, int* out_created)
{
    MewNamedButtonCreateInfo create_info;

    memset(&create_info, 0, sizeof(create_info));
    create_info.scene_name = scene_name;
    create_info.node_name = node_name;
    create_info.role_name = role_name;
    create_info.label_key = label_key;
    create_info.enabled = 1U;
    create_info.activate_enabled = 1U;
    create_info.strict_mouse = 0U;
    create_info.interact_override = MEW_BUTTON_INTERACT_FORCE_ENABLED;
    create_info.callback = callback;
    create_info.user_data = user_data;

    return MewUI_SetupButtonInScene(&create_info, io_button, out_created);
}

void* MewUI_SetupButtonWithoutLabel(const char* scene_name, const char* node_name, const char* role_name, MewButtonCallback callback, void* user_data, void** io_button, int* out_created)
{
    MewNamedButtonCreateInfo create_info;

    memset(&create_info, 0, sizeof(create_info));
    create_info.scene_name = scene_name;
    create_info.node_name = node_name;
    create_info.role_name = role_name;
    create_info.label_key = NULL;
    create_info.label_text = "";
    create_info.enabled = 1U;
    create_info.activate_enabled = 1U;
    create_info.strict_mouse = 0U;
    create_info.interact_override = MEW_BUTTON_INTERACT_FORCE_ENABLED;
    create_info.callback = callback;
    create_info.user_data = user_data;

    return MewUI_SetupButtonInScene(&create_info, io_button, out_created);
}

static int MewUI_SetTextInSceneInternal(void* scene_manager, const char* child_name, const char* text, uint8_t use_localization_key)
{
    void* text_element;

    if (!scene_manager || !child_name || !text)
    {
        return 0;
    }

    if (MewUI_IsSceneDestroying(scene_manager))
    {
        return 0;
    }

    text_element = MewUI_FindNodeInSceneByName(scene_manager, child_name);

    if (!text_element)
    {
        MewUI_APIDebugLog("Refusing to set text for '%s': no node matched that name!", child_name);
        return 0;
    }

    return MewUI_SetTextElementInternal(text_element, text, use_localization_key, scene_manager);
}

int MewUI_SetTextInSceneFromLocalizationKey(void* scene_manager, const char* child_name, const char* key)
{
    return MewUI_SetTextInSceneInternal(scene_manager, child_name, key, 1U);
}

int MewUI_SetTextInSceneFromLocalizationKeyValue(void* scene_manager, const char* child_name, const char* key, const char* value0)
{
    const char* values[1];

    values[0] = value0;
    return MewUI_SetTextInSceneFromLocalizationKeyValues(scene_manager, child_name, key, values, 1U);
}

int MewUI_SetTextInSceneFromLocalizationKeyValues(void* scene_manager, const char* child_name, const char* key, const char* const* values, uint32_t value_count)
{
    void* text_element;

    if (!scene_manager || !child_name || !key)
    {
        return 0;
    }

    if (MewUI_IsSceneDestroying(scene_manager))
    {
        return 0;
    }

    text_element = MewUI_FindNodeInSceneByName(scene_manager, child_name);

    if (!text_element)
    {
        MewUI_APIDebugLog("Refusing to set formatted text for '%s': no node matched that name!", child_name);
        return 0;
    }

    return MewUI_SetTextElementFromLocalizationKeyValues(text_element, key, values, value_count);
}

int MewUI_SetTextFromLocalizationKey(const char* scene_name, const char* child_name, const char* key)
{
    void* scene_manager;

    if (!scene_name || !child_name || !key)
    {
        return 0;
    }

    scene_manager = MewUI_GetSceneByName(scene_name);

    if (!scene_manager)
    {
        return 0;
    }

    return MewUI_SetTextInSceneFromLocalizationKey(scene_manager, child_name, key);
}

int MewUI_SetTextFromLocalizationKeyValue(const char* scene_name, const char* child_name, const char* key, const char* value0)
{
    const char* values[1];

    values[0] = value0;
    return MewUI_SetTextFromLocalizationKeyValues(scene_name, child_name, key, values, 1U);
}

int MewUI_SetTextFromLocalizationKeyValues(const char* scene_name, const char* child_name, const char* key, const char* const* values, uint32_t value_count)
{
    void* scene_manager;

    if (!scene_name || !child_name || !key)
    {
        return 0;
    }

    scene_manager = MewUI_GetSceneByName(scene_name);

    if (!scene_manager)
    {
        return 0;
    }

    return MewUI_SetTextInSceneFromLocalizationKeyValues(scene_manager, child_name, key, values, value_count);
}

static int MewUI_CopyUtf8ToTemporaryWideBuffer(const char* text, wchar_t* out_buffer, size_t out_capacity, size_t* out_length)
{
    int required_length;
    int converted_length;
    UINT code_page;

    if (!out_buffer || out_capacity == 0U || !out_length)
    {
        return 0;
    }

    out_buffer[0] = L'\0';
    *out_length = 0U;

    if (!text)
    {
        return 1;
    }

    code_page = CP_UTF8;
    required_length = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);

    if (required_length <= 0)
    {
        code_page = CP_ACP;
        required_length = MultiByteToWideChar(code_page, 0, text, -1, NULL, 0);
    }

    if (required_length <= 0)
    {
        return 0;
    }

    if ((size_t)required_length > out_capacity)
    {
        required_length = (int)out_capacity;
    }

    converted_length = MultiByteToWideChar(code_page, code_page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, text, -1, out_buffer, required_length);

    if (converted_length <= 0)
    {
        out_buffer[0] = L'\0';
        return 0;
    }

    out_buffer[out_capacity - 1U] = L'\0';
    *out_length = wcslen(out_buffer);
    return 1;
}

static int MewUI_FormatPreparedTextValueToWide(const MewUITextFormatValue* value, wchar_t* out_buffer, size_t out_capacity, size_t* out_length)
{
    wchar_t format_buffer[16];
    uint32_t precision;
    int written;

    if (!value || !out_buffer || out_capacity == 0U || !out_length)
    {
        return 0;
    }

    out_buffer[0] = L'\0';
    *out_length = 0U;
    precision = value->precision;

    if (precision > 9U)
    {
        precision = 9U;
    }

    switch (value->type)
    {
        case MEW_UI_TEXT_FORMAT_VALUE_STRING:
            return MewUI_CopyUtf8ToTemporaryWideBuffer(value->data.string_value, out_buffer, out_capacity, out_length);

        case MEW_UI_TEXT_FORMAT_VALUE_INT32:
            written = _snwprintf(out_buffer, out_capacity, L"%d", (int)value->data.int32_value);
            break;

        case MEW_UI_TEXT_FORMAT_VALUE_UINT32:
            written = _snwprintf(out_buffer, out_capacity, L"%u", (unsigned int)value->data.uint32_value);
            break;

        case MEW_UI_TEXT_FORMAT_VALUE_FLOAT:
            _snwprintf(format_buffer, sizeof(format_buffer) / sizeof(format_buffer[0]), L"%%.%uf", (unsigned int)precision);
            format_buffer[(sizeof(format_buffer) / sizeof(format_buffer[0])) - 1U] = L'\0';
            written = _snwprintf(out_buffer, out_capacity, format_buffer, (double)value->data.float_value);
            break;

        case MEW_UI_TEXT_FORMAT_VALUE_DOUBLE:
            _snwprintf(format_buffer, sizeof(format_buffer) / sizeof(format_buffer[0]), L"%%.%uf", (unsigned int)precision);
            format_buffer[(sizeof(format_buffer) / sizeof(format_buffer[0])) - 1U] = L'\0';
            written = _snwprintf(out_buffer, out_capacity, format_buffer, value->data.double_value);
            break;

        default:
            return 0;
    }

    out_buffer[out_capacity - 1U] = L'\0';

    if (written < 0)
    {
        return 0;
    }

    *out_length = wcslen(out_buffer);
    return 1;
}

static int MewUI_AppendPreparedTextLiteral(MewUITextFormat* format, const wchar_t* text, size_t length)
{
    MewUITextFormatSegment* segment;

    if (!format || (!text && length != 0U))
    {
        return 0;
    }

    if (length == 0U)
    {
        return 1;
    }

    if (format->segment_count >= MEW_TEXT_FORMAT_MAX_SEGMENTS)
    {
        return 0;
    }

    segment = &format->segments[format->segment_count];
    memset(segment, 0, sizeof(*segment));

    if (!MewUI_InitWideStringFromWideData(&segment->literal, text, length))
    {
        return 0;
    }

    segment->is_value = 0U;
    segment->value_index = 0U;
    format->segment_count++;
    return 1;
}

static int MewUI_AppendPreparedTextPlaceholder(MewUITextFormat* format, uint32_t value_index)
{
    MewUITextFormatSegment* segment;

    if (!format || value_index >= MEW_TEXT_FORMAT_MAX_VALUES)
    {
        return 0;
    }

    if (format->segment_count >= MEW_TEXT_FORMAT_MAX_SEGMENTS)
    {
        return 0;
    }

    segment = &format->segments[format->segment_count];
    memset(segment, 0, sizeof(*segment));
    segment->is_value = 1U;
    segment->value_index = value_index;
    format->segment_count++;

    if (value_index + 1U > format->value_count)
    {
        format->value_count = value_index + 1U;
    }

    return 1;
}

void MewUI_ClearTextFormat(MewUITextFormat* format)
{
    uint32_t index;

    if (!format)
    {
        return;
    }

    for (index = 0U; index < format->segment_count && index < MEW_TEXT_FORMAT_MAX_SEGMENTS; ++index)
    {
        if (!format->segments[index].is_value)
        {
            MewUI_FreeWideStringFromText(&format->segments[index].literal);
        }
    }

    memset(format, 0, sizeof(*format));
}

int MewUI_PrepareTextFormat(const char* scene_name, const char* child_name, const char* key, MewUITextFormat* io_format)
{
    void* scene_manager;
    void* text_element;
    MewWideString localized_string;
    const wchar_t* localized_data;
    size_t localized_length;
    size_t literal_start;
    size_t cursor;
    uint32_t placeholder_index;
    size_t token_length;

    if (!scene_name || !child_name || !key || !io_format)
    {
        return 0;
    }

    if (io_format->prepared)
    {
        if (io_format->scene_manager && io_format->text_element && !MewUI_IsSceneDestroying(io_format->scene_manager))
        {
            return 1;
        }

        MewUI_ClearTextFormat(io_format);
    }
    else
    {
        memset(io_format, 0, sizeof(*io_format));
    }

    scene_manager = MewUI_GetSceneByName(scene_name);

    if (!scene_manager || MewUI_IsSceneDestroying(scene_manager))
    {
        return 0;
    }

    text_element = MewUI_FindNodeInSceneByName(scene_manager, child_name);

    if (!text_element)
    {
        return 0;
    }

    memset(&localized_string, 0, sizeof(localized_string));

    if (!MewUI_LocalizeKeyToWideString(key, &localized_string))
    {
        return 0;
    }

    localized_data = MewUI_GetWideStringData(&localized_string);
    localized_length = MewUI_GetWideStringSize(&localized_string);

    if (!localized_data && localized_length != 0U)
    {
        MewUI_DestroyEngineWideString(&localized_string);
        return 0;
    }

    MewUI_ClearTextFormat(io_format);
    literal_start = 0U;
    cursor = 0U;

    while (cursor < localized_length)
    {
        if (MewUI_ParseLocalizationPlaceholder(localized_data, localized_length, cursor, &placeholder_index, &token_length) && placeholder_index < MEW_TEXT_FORMAT_MAX_VALUES)
        {
            if (!MewUI_AppendPreparedTextLiteral(io_format, localized_data + literal_start, cursor - literal_start))
            {
                MewUI_ClearTextFormat(io_format);
                MewUI_DestroyEngineWideString(&localized_string);
                return 0;
            }

            if (!MewUI_AppendPreparedTextPlaceholder(io_format, placeholder_index))
            {
                MewUI_ClearTextFormat(io_format);
                MewUI_DestroyEngineWideString(&localized_string);
                return 0;
            }

            cursor += token_length;
            literal_start = cursor;
        }
        else
        {
            cursor++;
        }
    }

    if (!MewUI_AppendPreparedTextLiteral(io_format, localized_data + literal_start, localized_length - literal_start))
    {
        MewUI_ClearTextFormat(io_format);
        MewUI_DestroyEngineWideString(&localized_string);
        return 0;
    }

    io_format->scene_manager = scene_manager;
    io_format->text_element = text_element;
    io_format->prepared = 1U;

    MewUI_DestroyEngineWideString(&localized_string);
    return 1;
}

int MewUI_SetPreparedTextFormatTypedValues(MewUITextFormat* format, const MewUITextFormatValue* values, uint32_t value_count)
{
    wchar_t value_texts[MEW_TEXT_FORMAT_MAX_VALUES][MEW_TEXT_FORMAT_VALUE_BUFFER_MAX];
    size_t value_lengths[MEW_TEXT_FORMAT_MAX_VALUES];
    uint8_t value_ready[MEW_TEXT_FORMAT_MAX_VALUES];
    wchar_t output_text[MEW_TEXT_BUFFER_MAX];
    size_t output_length;
    uint32_t index;
    MewWideString output_string;
    int result;

    if (!format || !format->prepared || !format->scene_manager || !format->text_element)
    {
        return 0;
    }

    if (MewUI_IsSceneDestroying(format->scene_manager))
    {
        MewUI_ClearTextFormat(format);
        return 0;
    }

    if (format->segment_count > MEW_TEXT_FORMAT_MAX_SEGMENTS)
    {
        MewUI_ClearTextFormat(format);
        return 0;
    }

    if (format->value_count > value_count || value_count > MEW_TEXT_FORMAT_MAX_VALUES)
    {
        return 0;
    }

    if (format->value_count > 0U && !values)
    {
        return 0;
    }

    memset(value_texts, 0, sizeof(value_texts));
    memset(value_lengths, 0, sizeof(value_lengths));
    memset(value_ready, 0, sizeof(value_ready));

    for (index = 0U; index < format->segment_count; ++index)
    {
        MewUITextFormatSegment* segment;

        segment = &format->segments[index];

        if (segment->is_value && !value_ready[segment->value_index])
        {
            if (!MewUI_FormatPreparedTextValueToWide(&values[segment->value_index], value_texts[segment->value_index], MEW_TEXT_FORMAT_VALUE_BUFFER_MAX, &value_lengths[segment->value_index]))
            {
                return 0;
            }

            value_ready[segment->value_index] = 1U;
        }
    }

    output_length = 0U;

    for (index = 0U; index < format->segment_count; ++index)
    {
        MewUITextFormatSegment* segment;
        const wchar_t* literal_data;
        size_t literal_length;
        size_t copy_length;

        segment = &format->segments[index];

        if (segment->is_value)
        {
            copy_length = value_lengths[segment->value_index];

            if (output_length + copy_length >= MEW_TEXT_BUFFER_MAX)
            {
                return 0;
            }

            if (copy_length > 0U)
            {
                memcpy(output_text + output_length, value_texts[segment->value_index], copy_length * sizeof(wchar_t));
                output_length += copy_length;
            }
        }
        else
        {
            literal_data = MewUI_GetWideStringData(&segment->literal);
            literal_length = MewUI_GetWideStringSize(&segment->literal);

            if (!literal_data && literal_length != 0U)
            {
                MewUI_ClearTextFormat(format);
                return 0;
            }

            if (output_length + literal_length >= MEW_TEXT_BUFFER_MAX)
            {
                return 0;
            }

            if (literal_length > 0U)
            {
                memcpy(output_text + output_length, literal_data, literal_length * sizeof(wchar_t));
                output_length += literal_length;
            }
        }
    }

    output_text[output_length] = L'\0';
    MewUI_InitWideStringFromTemporaryWideData(&output_string, output_text, output_length);
    result = MewUI_SetTextElementWideStringCopy(format->text_element, &output_string);

    if (!result)
    {
        MewUI_ClearTextFormat(format);
    }

    return result;
}

int MewUI_SetPreparedTextFormatValues(MewUITextFormat* format, const char* const* values, uint32_t value_count)
{
    MewUITextFormatValue typed_values[MEW_TEXT_FORMAT_MAX_VALUES];
    uint32_t index;

    if (value_count > MEW_TEXT_FORMAT_MAX_VALUES)
    {
        return 0;
    }

    memset(typed_values, 0, sizeof(typed_values));

    for (index = 0U; index < value_count; ++index)
    {
        typed_values[index].type = MEW_UI_TEXT_FORMAT_VALUE_STRING;
        typed_values[index].data.string_value = values ? values[index] : NULL;
    }

    return MewUI_SetPreparedTextFormatTypedValues(format, typed_values, value_count);
}

int MewUI_SetPreparedTextFormatValue(MewUITextFormat* format, const char* value0)
{
    const char* values[1];

    values[0] = value0;
    return MewUI_SetPreparedTextFormatValues(format, values, 1U);
}

int MewUI_SetPreparedTextFormatInt32(MewUITextFormat* format, int32_t value0)
{
    MewUITextFormatValue value;

    memset(&value, 0, sizeof(value));
    value.type = MEW_UI_TEXT_FORMAT_VALUE_INT32;
    value.data.int32_value = value0;
    return MewUI_SetPreparedTextFormatTypedValues(format, &value, 1U);
}

int MewUI_SetPreparedTextFormatUInt32(MewUITextFormat* format, uint32_t value0)
{
    MewUITextFormatValue value;

    memset(&value, 0, sizeof(value));
    value.type = MEW_UI_TEXT_FORMAT_VALUE_UINT32;
    value.data.uint32_value = value0;
    return MewUI_SetPreparedTextFormatTypedValues(format, &value, 1U);
}

int MewUI_SetPreparedTextFormatFloat(MewUITextFormat* format, float value0, uint32_t precision)
{
    MewUITextFormatValue value;

    memset(&value, 0, sizeof(value));
    value.type = MEW_UI_TEXT_FORMAT_VALUE_FLOAT;
    value.precision = precision;
    value.data.float_value = value0;
    return MewUI_SetPreparedTextFormatTypedValues(format, &value, 1U);
}

int MewUI_SetPreparedTextFormatDouble(MewUITextFormat* format, double value0, uint32_t precision)
{
    MewUITextFormatValue value;

    memset(&value, 0, sizeof(value));
    value.type = MEW_UI_TEXT_FORMAT_VALUE_DOUBLE;
    value.precision = precision;
    value.data.double_value = value0;
    return MewUI_SetPreparedTextFormatTypedValues(format, &value, 1U);
}

int MewUI_PrepareTextFormatValue0(const char* scene_name, const char* child_name, const char* key, MewUITextFormatValue0* io_format)
{
    return MewUI_PrepareTextFormat(scene_name, child_name, key, io_format);
}

int MewUI_SetPreparedTextFormatValue0UInt32(MewUITextFormatValue0* format, uint32_t value)
{
    return MewUI_SetPreparedTextFormatUInt32(format, value);
}

void MewUI_ClearTextFormatValue0(MewUITextFormatValue0* format)
{
    MewUI_ClearTextFormat(format);
}

void* MewUI_CreateButtonInScene(const MewButtonCreateInfo* create_info)
{
    MewButtonCreateInfo copy;

    if (!create_info || !create_info->scene_manager || !create_info->node_name)
    {
        return NULL;
    }

    copy = *create_info;

    if (!copy.button_node && copy.root_node && copy.node_name)
    {
        copy.button_node = MewUI_FindChildByName(copy.root_node, copy.node_name);
    }

    if (!copy.button_node)
    {
        copy.button_node = MewUI_FindNodeInSceneByName(copy.scene_manager, copy.node_name);
    }

    if (!copy.button_node)
    {
        return NULL;
    }

    return MewUI_CreateButtonFromNode(&copy);
}

// Pulls the scene component list table used for searches and liveness checks...
static MewPodVectorPtr* MewUI_GetSceneComponentList(void* scene_manager)
{
    if (!scene_manager)
    {
        return NULL;
    }

    __try
    {
        return *(MewPodVectorPtr**)((uint8_t*)scene_manager + MEW_OFF_SCENE_COMPONENT_LISTS);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return NULL;
    }
}

// Does a cheap pointer sanity check before probing possible UI nodes from component memory...
static int MewUI_IsReadablePointer(const void* pointer, size_t byte_count)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t start_address;
    uintptr_t end_address;
    uintptr_t region_end_address;
    DWORD protect_flags;

    if (!pointer || byte_count == 0U)
    {
        return 0;
    }

    memset(&info, 0, sizeof(info));

    if (!VirtualQuery(pointer, &info, sizeof(info)))
    {
        return 0;
    }

    if (info.State != MEM_COMMIT)
    {
        return 0;
    }

    protect_flags = info.Protect & 0xFFU;

    if (protect_flags == PAGE_NOACCESS || (info.Protect & PAGE_GUARD) != 0U)
    {
        return 0;
    }

    start_address = (uintptr_t)pointer;
    end_address = start_address + (uintptr_t)byte_count;
    region_end_address = (uintptr_t)info.BaseAddress + (uintptr_t)info.RegionSize;

    if (end_address < start_address)
    {
        return 0;
    }

    return end_address <= region_end_address;
}

static void* MewUI_GetComponentRootNode(void* component)
{
    void* root_node;

    if (!component)
    {
        return NULL;
    }

    if (!MewUI_IsReadablePointer(component, 0x40U))
    {
        return NULL;
    }

    root_node = NULL;

    __try
    {
        root_node = *(void**)((uint8_t*)component + 0x38U);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        root_node = NULL;
    }

    if (!root_node || root_node == component)
    {
        return NULL;
    }

    if (!MewUI_IsReadablePointer(root_node, 0x88U))
    {
        return NULL;
    }

    return root_node;
}

static void* MewUI_FindTextOwnerInScene(void* scene_manager, const char* node_name, void** out_node)
{
    MewPodVectorPtr* list;
    uint32_t component_index;

    if (out_node)
    {
        *out_node = NULL;
    }

    if (!scene_manager || !node_name)
    {
        return NULL;
    }

    list = MewUI_GetSceneComponentList(scene_manager);

    if (!list)
    {
        return NULL;
    }

    __try
    {
        for (component_index = 0U; component_index < list->size; ++component_index)
        {
            void* component;
            void* root_node;
            void* found_node;

            component = list->data[component_index];

            if (!component)
            {
                continue;
            }

            root_node = MewUI_GetComponentRootNode(component);

            if (!root_node)
            {
                continue;
            }

            found_node = MewUI_FindRootOwnedChildByName(root_node, node_name);

            if (found_node)
            {
                if (out_node)
                {
                    *out_node = found_node;
                }

                return component;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (out_node)
        {
            *out_node = NULL;
        }

        return NULL;
    }

    return NULL;
}

static int MewUI_SetTextInSceneUsingEngineString(void* scene_manager, const char* child_name, const char* text, uint8_t use_localization_key)
{
    MewFnUIRootSetTextString set_text;
    MewFnInitNarrowString init_string;
    MewFnDestroyNarrowString destroy_string;
    MewNarrowString child_name_string;
    MewNarrowString text_string;
    uint8_t child_name_initialized;
    uint8_t child_name_transferred_to_engine;
    uint8_t text_initialized;
    uint8_t text_transferred_to_engine;
    void* text_owner;
    int result;

    if (!scene_manager || !child_name || !text || !use_localization_key)
    {
        return 0;
    }

    set_text = (MewFnUIRootSetTextString)MewUI_Address(MEW_RVA_UI_ROOT_SET_TEXT_STRING);
    init_string = (MewFnInitNarrowString)MewUI_Address(MEW_RVA_INIT_NARROW_STRING);
    destroy_string = (MewFnDestroyNarrowString)MewUI_Address(MEW_RVA_DESTROY_NARROW_STRING);

    if (!set_text || !init_string || !destroy_string)
    {
        return 0;
    }

    text_owner = MewUI_FindTextOwnerInScene(scene_manager, child_name, NULL);

    if (!text_owner)
    {
        return 0;
    }

    memset(&child_name_string, 0, sizeof(child_name_string));
    memset(&text_string, 0, sizeof(text_string));
    child_name_initialized = 0U;
    child_name_transferred_to_engine = 0U;
    text_initialized = 0U;
    text_transferred_to_engine = 0U;
    result = 0;

    __try
    {
        init_string(&child_name_string, child_name);
        child_name_initialized = 1U;
        init_string(&text_string, text);
        text_initialized = 1U;

        child_name_transferred_to_engine = 1U;
        text_transferred_to_engine = 1U;
        set_text(text_owner, &child_name_string, &text_string);
        result = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("SetTextInSceneUsingEngineString failed: owner=%p node='%s' text='%s' childTransferred=%u textTransferred=%u", text_owner, child_name, text, (unsigned int)child_name_transferred_to_engine, (unsigned int)text_transferred_to_engine);
        result = 0;
    }

    if (child_name_initialized && !child_name_transferred_to_engine)
    {
        __try
        {
            destroy_string(&child_name_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    if (text_initialized && !text_transferred_to_engine)
    {
        __try
        {
            destroy_string(&text_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return result;
}

static int MewUI_SetTextInSceneUsingEngineWideString(void* scene_manager, const char* child_name, const MewWideString* text)
{
    MewFnUIRootSetTextWideString set_text;
    MewFnInitNarrowString init_string;
    MewFnDestroyNarrowString destroy_string;
    MewFnCopyWideStringObject copy_wide_string;
    MewFnDestroyWideString destroy_wide_string;
    MewNarrowString child_name_string;
    MewWideString engine_text_string;
    uint8_t child_name_initialized;
    uint8_t child_name_transferred_to_engine;
    uint8_t text_initialized;
    uint8_t text_transferred_to_engine;
    void* text_owner;
    int result;

    if (!scene_manager || !child_name || !text)
    {
        return 0;
    }

    set_text = (MewFnUIRootSetTextWideString)MewUI_Address(MEW_RVA_UI_ROOT_SET_TEXT_WIDE_STRING);
    init_string = (MewFnInitNarrowString)MewUI_Address(MEW_RVA_INIT_NARROW_STRING);
    destroy_string = (MewFnDestroyNarrowString)MewUI_Address(MEW_RVA_DESTROY_NARROW_STRING);
    copy_wide_string = (MewFnCopyWideStringObject)MewUI_Address(MEW_RVA_COPY_WIDE_STRING_OBJECT);
    destroy_wide_string = (MewFnDestroyWideString)MewUI_Address(MEW_RVA_DESTROY_WIDE_STRING);

    if (!set_text || !init_string || !destroy_string || !copy_wide_string || !destroy_wide_string)
    {
        return 0;
    }

    text_owner = MewUI_FindTextOwnerInScene(scene_manager, child_name, NULL);

    if (!text_owner)
    {
        return 0;
    }

    memset(&child_name_string, 0, sizeof(child_name_string));
    memset(&engine_text_string, 0, sizeof(engine_text_string));
    child_name_initialized = 0U;
    child_name_transferred_to_engine = 0U;
    text_initialized = 0U;
    text_transferred_to_engine = 0U;
    result = 0;

    __try
    {
        init_string(&child_name_string, child_name);
        child_name_initialized = 1U;
        copy_wide_string(&engine_text_string, text);
        text_initialized = 1U;

        child_name_transferred_to_engine = 1U;
        text_transferred_to_engine = 1U;
        set_text(text_owner, &child_name_string, &engine_text_string);
        result = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MewUI_APIDebugLog("SetTextInSceneUsingEngineWideString failed: owner=%p node='%s' textSize=%llu childTransferred=%u textTransferred=%u", text_owner, child_name, (unsigned long long)MewUI_GetWideStringSize(text), (unsigned int)child_name_transferred_to_engine, (unsigned int)text_transferred_to_engine);
        result = 0;
    }

    if (child_name_initialized && !child_name_transferred_to_engine)
    {
        __try
        {
            destroy_string(&child_name_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    if (text_initialized && !text_transferred_to_engine)
    {
        __try
        {
            destroy_wide_string(&engine_text_string);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return result;
}

void* MewUI_FindNodeInSceneByName(void* scene_manager, const char* node_name)
{
    void* found_node;

    if (!scene_manager || !node_name)
    {
        return NULL;
    }

    found_node = NULL;
    MewUI_FindTextOwnerInScene(scene_manager, node_name, &found_node);
    return found_node;
}

void* MewUI_FindChildByNameInScene(void* scene_manager, const char* child_name)
{
    return MewUI_FindNodeInSceneByName(scene_manager, child_name);
}

void* MewUI_FindButtonByRole(void* scene_manager, const char* role_name)
{
    void* buttons[1];
    uint32_t count;

    count = MewUI_FindButtonsByRole(scene_manager, role_name, buttons, 1U);

    if (count == 0U)
    {
        return NULL;
    }

    return buttons[0];
}

uint32_t MewUI_FindButtonsByRole(void* scene_manager, const char* role_name, void** out_buttons, uint32_t out_button_capacity)
{
    MewPodVectorPtr* list;
    uint32_t index;
    uint32_t found_count;

    if (!scene_manager || !role_name || !out_buttons || out_button_capacity == 0U)
    {
        return 0U;
    }

    list = MewUI_GetSceneComponentList(scene_manager);
    found_count = 0U;

    if (!list)
    {
        return 0U;
    }

    __try
    {
        for (index = 0U; index < list->size; ++index)
        {
            void* component;
            MewNarrowString* role_string;

            component = list->data[index];

            if (!component || !MewUI_IsButtonComponent(component))
            {
                continue;
            }

            role_string = (MewNarrowString*)((uint8_t*)component + MEW_OFF_BUTTON_ROLE_NAME);

            if (!MewUI_NarrowStringEqualsLiteral(role_string, role_name))
            {
                continue;
            }

            out_buttons[found_count] = component;
            found_count++;

            if (found_count >= out_button_capacity)
            {
                break;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return found_count;
    }

    return found_count;
}

void* MewUI_FindButtonByNodeName(void* scene_manager, const char* node_name)
{
    MewPodVectorPtr* list;
    void* target_node;
    uint32_t index;

    if (!scene_manager || !node_name)
    {
        return NULL;
    }

    if (MewUI_IsSceneDestroying(scene_manager))
    {
        return NULL;
    }

    target_node = MewUI_FindNodeInSceneByName(scene_manager, node_name);

    if (!target_node)
    {
        return NULL;
    }

    list = MewUI_GetSceneComponentList(scene_manager);

    if (!list)
    {
        return NULL;
    }

    __try
    {
        for (index = 0U; index < list->size; ++index)
        {
            MewButtonRecord* record;
            void* component;
            void* button_node;

            component = list->data[index];

            if (!component || !MewUI_IsButtonComponent(component))
            {
                continue;
            }

            record = MewUI_GetButtonRecord(component);

            if (record && record->owned_by_ui)
            {
                continue;
            }

            button_node = *(void**)((uint8_t*)component + MEW_OFF_BUTTON_NODE);

            if (button_node == target_node)
            {
                return component;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return NULL;
    }

    return NULL;
}

void* MewUI_HookExistingButtonByNodeName(const char* scene_name, const char* node_name, MewButtonCallback callback, void* user_data, void** io_button)
{
    void* scene_manager;
    void* button;

    if (!scene_name || !node_name)
    {
        if (io_button)
        {
            *io_button = NULL;
        }

        return NULL;
    }

    scene_manager = MewUI_GetSceneByName(scene_name);

    if (!scene_manager)
    {
        if (io_button)
        {
            *io_button = NULL;
        }

        return NULL;
    }

    if (io_button && *io_button)
    {
        if (MewUI_IsComponentInScene(scene_manager, *io_button))
        {
            if (!MewUI_IsButtonTracked(*io_button))
            {
                MewUI_RegisterExistingButton(*io_button, NULL, callback, user_data);
            }

            return *io_button;
        }

        *io_button = NULL;
    }

    button = MewUI_FindButtonByNodeName(scene_manager, node_name);

    if (!button)
    {
        return NULL;
    }

    MewUI_RegisterExistingButton(button, NULL, callback, user_data);

    if (io_button)
    {
        *io_button = button;
    }

    return button;
}

void* MewUI_HookExistingButtonByNodeNameExclusive(const char* scene_name, const char* node_name, MewButtonCallback callback, void* user_data, void** io_button)
{
    void* button;

    button = MewUI_HookExistingButtonByNodeName(scene_name, node_name, callback, user_data, io_button);

    if (!button)
    {
        return NULL;
    }

    MewUI_SetButtonSuppressOriginalActivate(button, 1);
    return button;
}

const char* MewUI_GetButtonEventName(MewButtonEvent event_type)
{
    switch (event_type)
    {
        case MEW_BUTTON_EVENT_STATE_CHANGED:
        {
            return "state changed";
        }

        case MEW_BUTTON_EVENT_HOVER_ENTER:
        {
            return "hover enter";
        }

        case MEW_BUTTON_EVENT_HOVER_EXIT:
        {
            return "hover exit";
        }

        case MEW_BUTTON_EVENT_PRESS_BEGIN:
        {
            return "press begin";
        }

        case MEW_BUTTON_EVENT_PRESS_CANCEL:
        {
            return "press cancel";
        }

        case MEW_BUTTON_EVENT_CLICK:
        {
            return "click";
        }

        case MEW_BUTTON_EVENT_DISABLED:
        {
            return "disabled";
        }

        default:
        {
            return "unknown";
        }
    }
}

const char* MewUI_GetButtonStateName(MewButtonState state)
{
    switch (state)
    {
        case MEW_BUTTON_STATE_IDLE:
        {
            return "idle";
        }

        case MEW_BUTTON_STATE_HOVERED:
        {
            return "hovered";
        }

        case MEW_BUTTON_STATE_PRESSED:
        {
            return "pressed";
        }

        case MEW_BUTTON_STATE_SELECTED:
        {
            return "selected";
        }

        case MEW_BUTTON_STATE_DISABLED:
        {
            return "disabled";
        }

        case MEW_BUTTON_STATE_TRANSITION:
        {
            return "transition";
        }

        case MEW_BUTTON_STATE_INVALID:
        default:
        {
            return "invalid";
        }
    }
}

int MewUI_SetButtonSuppressOriginalActivate(void* button, int suppress_original_activate)
{
    MewButtonRecord* record;

    record = MewUI_GetButtonRecord(button);

    if (!record)
    {
        return 0;
    }

    record->suppress_original_activate = suppress_original_activate ? 1U : 0U;
    return 1;
}

int MewUI_RegisterExistingButton(void* button, const char* role_name, MewButtonCallback callback, void* user_data)
{
    MewButtonRecord* record;

    if (!button)
    {
        return 0;
    }

    record = MewUI_AllocButtonRecord(button);

    if (!record)
    {
        return 0;
    }

    record->owned_by_ui = 0U;
    record->callback = callback;
    record->user_data = user_data;
    record->interact_override = MEW_BUTTON_INTERACT_GAME_DEFAULT;
    record->can_interact_callback = NULL;
    record->can_interact_user_data = NULL;
    record->suppress_original_activate = 0U;
    record->click_from_hook_seen = 0U;
    record->last_state = MewUI_GetButtonState(button);

    if (role_name)
    {
        strncpy(record->role_name, role_name, sizeof(record->role_name) - 1U);
        record->role_name[sizeof(record->role_name) - 1U] = '\0';
    }
    else
    {
        MewUI_GetButtonRoleName(button, record->role_name, sizeof(record->role_name));
    }

    __try
    {
        record->scene_manager = *(void**)((uint8_t*)button + MEW_OFF_COMPONENT_MANAGER);
        record->context = *(void**)((uint8_t*)button + MEW_OFF_COMPONENT_CONTEXT);
        record->button_node = *(void**)((uint8_t*)button + MEW_OFF_BUTTON_NODE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return 1;
}

void MewUI_Tick(void)
{
    uint32_t index;

    for (index = 0U; index < MEW_MAX_BUTTON_RECORDS; ++index)
    {
        MewButtonRecord* record;
        MewButtonState old_state;
        MewButtonState new_state;

        record = &g_mew_ui_button_records[index];

        if (!record->used || !record->button)
        {
            continue;
        }

        if (record->scene_manager && !MewUI_IsComponentInScene(record->scene_manager, record->button))
        {
            MewUI_APIDebugLog("Clearing tracked button record: button left scene or scene is no longer readable! index=%u button=%p scene=%p role='%s'", (unsigned int)index, record->button, record->scene_manager, record->role_name);
            memset(record, 0, sizeof(*record));
            continue;
        }

        old_state = record->last_state;
        new_state = MewUI_GetButtonState(record->button);

        if (new_state == MEW_BUTTON_STATE_INVALID)
        {
            memset(record, 0, sizeof(*record));
            continue;
        }

        if (old_state == new_state)
        {
            continue;
        }

        record->last_state = new_state;
        MewUI_FireButtonCallback(record, MEW_BUTTON_EVENT_STATE_CHANGED, old_state, new_state);

        if (new_state == MEW_BUTTON_STATE_HOVERED && old_state != MEW_BUTTON_STATE_PRESSED)
        {
            MewUI_FireButtonCallback(record, MEW_BUTTON_EVENT_HOVER_ENTER, old_state, new_state);
        }

        if (old_state == MEW_BUTTON_STATE_HOVERED && new_state != MEW_BUTTON_STATE_PRESSED)
        {
            MewUI_FireButtonCallback(record, MEW_BUTTON_EVENT_HOVER_EXIT, old_state, new_state);
        }

        if (new_state == MEW_BUTTON_STATE_PRESSED)
        {
            record->click_from_hook_seen = 0U;
            MewUI_FireButtonCallback(record, MEW_BUTTON_EVENT_PRESS_BEGIN, old_state, new_state);
        }

        if (old_state == MEW_BUTTON_STATE_PRESSED && new_state != MEW_BUTTON_STATE_PRESSED)
        {
            if (!record->click_from_hook_seen && (new_state == MEW_BUTTON_STATE_HOVERED || new_state == MEW_BUTTON_STATE_DISABLED))
            {
                MewUI_FireButtonCallback(record, MEW_BUTTON_EVENT_CLICK, old_state, new_state);
            }
            else if (new_state == MEW_BUTTON_STATE_IDLE)
            {
                MewUI_FireButtonCallback(record, MEW_BUTTON_EVENT_PRESS_CANCEL, old_state, new_state);
            }

            record->click_from_hook_seen = 0U;
        }

        if (new_state == MEW_BUTTON_STATE_DISABLED)
        {
            MewUI_FireButtonCallback(record, MEW_BUTTON_EVENT_DISABLED, old_state, new_state);
        }
    }
}