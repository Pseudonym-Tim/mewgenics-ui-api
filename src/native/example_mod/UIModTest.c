#include "../mew_ui_api.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static const char* const MOD_NAME = "MewUI API Test Mod";
static const char* const SCENE_NAME = "House";

static const char* const TEXT_NODE_NAME = "test_text";
static const char* const TEXT_LOCALIZATION_KEY = "TEST_TEXT";

static const char* const BUTTON_NODE_NAME = "test_button";
static const char* const BUTTON_ROLE_NAME = "Example_Button";
static const char* const BUTTON_LABEL_KEY = "TEST_BUTTON_TEXT";
static const char* const BUTTON_CLICK_LABEL_KEY = "TEST_BUTTON_TEXT_VALUE";
static const char* const BUTTON_CLICK_SOUND_EVENT = "MapZoomerCoin_End";

static const char* const TEST_TOGGLE_NODE_NAME = "test_toggle";
static const char* const TEST_TOGGLE_ROLE_NAME = "Test_Toggle";

static const char* const TEST_NAV_VALUE_NODE_NAME = "test_nav_value";
static const char* const TEST_NAV_LEFT_NODE_NAME = "test_nav_left";
static const char* const TEST_NAV_RIGHT_NODE_NAME = "test_nav_right";
static const char* const TEST_NAV_VALUE_TEXT_KEY = "TEST_NAV_VALUE_TEXT";
static const char* const TEST_NAV_LEFT_ROLE_NAME = "Test_Nav_Left";
static const char* const TEST_NAV_RIGHT_ROLE_NAME = "Test_Nav_Right";

static const int MEW_UI_HOOK_PRIORITY = 30;
static const uint32_t UI_BOOTSTRAP_INTERVAL_MS = 100U;
static const uint32_t UI_TICK_INTERVAL_MS = 16U; // (60fps)...

static MewUISceneBinding g_houseScene;
static void* g_exampleButton = NULL;

static bool g_textReady = false;
static bool g_buttonReady = false;
static bool g_testToggleReady = false;
static bool g_testNavigationReady = false;
static uint32_t g_exampleButtonClicks = 0U;

static const char* const g_testNavValues[] =
{
    "1",
    "2",
    "3",
    NULL
};

static MewUIToggleBinding g_testToggle;
static MewUINavigationBinding g_testNavigation;
static bool g_testBindingsInitialized = false;
static uint32_t g_testSetupRetryTick = 0U;

#define TEST_SETUP_RETRY_TICKS 30U

static void Log(const char* format, ...);
static void __cdecl TestToggleChangedCallback(MewUIToggleBinding* binding, bool enabled, void* userData);
static void __cdecl TestNavigationChangedCallback(MewUINavigationBinding* binding, uint32_t index, const char* value, void* userData);

static uint32_t CountPointerArray(const char* const* values)
{
    uint32_t count;

    count = 0U;

    if (!values)
    {
        return 0U;
    }

    while (values[count])
    {
        ++count;
    }

    return count;
}

static void InitTestBindings(void)
{
    if (g_testBindingsInitialized)
    {
        return;
    }

    MewUI_InitToggleBindingWithStatePrefixes(&g_testToggle, SCENE_NAME, TEST_TOGGLE_NODE_NAME, TEST_TOGGLE_ROLE_NAME, "off_", "on_", false, TestToggleChangedCallback, NULL);

    MewUI_InitNavigationBinding(&g_testNavigation, SCENE_NAME, TEST_NAV_LEFT_NODE_NAME, TEST_NAV_RIGHT_NODE_NAME, TEST_NAV_VALUE_NODE_NAME, TEST_NAV_LEFT_ROLE_NAME, TEST_NAV_RIGHT_ROLE_NAME, TEST_NAV_VALUE_TEXT_KEY, g_testNavValues, CountPointerArray(g_testNavValues), 0U, TestNavigationChangedCallback, NULL);

    g_testBindingsInitialized = true;
}

// (Tiny logging wrapper)...
static void Log(const char* format, ...)
{
    char buffer[512];
    va_list args;

    if (!format)
    {
        return;
    }

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    MewUI_LogMessage("%s", buffer);
}

static void __cdecl TestToggleChangedCallback(MewUIToggleBinding* binding, bool enabled, void* userData)
{
    void* button;

    (void)userData;

    button = binding ? binding->button : NULL;

    Log("UI test toggle callback: binding=%p enabled=%s button=%p", binding, enabled ? "true" : "false", button);

    // Play a sound event! Yippie!
    if (!MewUI_PlaySoundEventFromComponent(button, BUTTON_CLICK_SOUND_EVENT, 1.0, 1.0, 0.0, 0U))
    {
        Log("UI test toggle sound skipped: event=%s button=%p", BUTTON_CLICK_SOUND_EVENT, button);
    }
}

static void __cdecl TestNavigationChangedCallback(MewUINavigationBinding* binding, uint32_t index, const char* value, void* userData)
{
    (void)userData;

    Log("UI test navigation callback: binding=%p index=%u value='%s' left=%p right=%p", binding, (unsigned int)index, value ? value : "", binding ? binding->left_button : NULL, binding ? binding->right_button : NULL);
}

static void ClearCachedButtons(void)
{
    g_exampleButton = NULL;
    g_testToggle.button = NULL;
    g_testToggle.scene_manager = NULL;
    g_testToggle.visual_synced = 0U;
    g_testNavigation.left_button = NULL;
    g_testNavigation.right_button = NULL;
    g_testNavigation.scene_manager = NULL;
    g_testNavigation.text_synced = 0U;
}

// Clears cached scene state whenever the scene unloads or swaps...
static void ResetSceneBoundState(void)
{
    g_textReady = false;
    g_buttonReady = false;
    g_testToggleReady = false;
    g_testNavigationReady = false;
    ClearCachedButtons();
}

// Runs when the "House" scene loads, unloads, or changes pointers...
static void __cdecl SceneRefreshCallback(MewUISceneBinding* binding, MewUISceneRefreshResult result, void* oldSceneManager, void* newSceneManager, void* userData)
{
    (void)binding;
    (void)userData;

    if (result == MEW_UI_SCENE_REFRESH_LOADED || result == MEW_UI_SCENE_REFRESH_CHANGED || result == MEW_UI_SCENE_REFRESH_UNLOADED)
    {
        ResetSceneBoundState();
    }

    Log("Scene refresh: %s old=%p new=%p", MewUI_GetSceneRefreshResultName(result), oldSceneManager, newSceneManager);
}

static void UpdateExampleButtonText(void)
{
    char clickCountText[32];

    if (!g_exampleButton)
    {
        return;
    }

    snprintf(clickCountText, sizeof(clickCountText), "%u", (unsigned int)g_exampleButtonClicks);
    clickCountText[sizeof(clickCountText) - 1U] = '\0';

    if (!MewUI_SetButtonLabelFromLocalizationKeyValue(g_exampleButton, BUTTON_CLICK_LABEL_KEY, clickCountText))
    {
        Log("Failed to update example button label from localization key '%s' value='%s'", BUTTON_CLICK_LABEL_KEY, clickCountText);
    }
}

// Reports button events...
static void __cdecl ExampleButtonCallback(void* button, MewButtonEvent eventType, MewButtonState oldState, MewButtonState newState, void* userData)
{
    (void)button;
    (void)userData;

    Log("Example button event: %s (%s -> %s)", MewUI_GetButtonEventName(eventType), MewUI_GetButtonStateName(oldState), MewUI_GetButtonStateName(newState));

    if (eventType == MEW_BUTTON_EVENT_CLICK)
    {
        ++g_exampleButtonClicks;
        UpdateExampleButtonText();
        Log("Example button clicked! clicks=%u", (unsigned int)g_exampleButtonClicks);
    }
}

// Sets existing text once the target scene is available...
static void SetExampleText(void)
{
    int textSet;

    if (g_textReady)
    {
        return;
    }

    textSet = MewUI_SetTextFromLocalizationKey(SCENE_NAME, TEXT_NODE_NAME, TEXT_LOCALIZATION_KEY);

    if (!textSet)
    {
        return;
    }

    g_textReady = true;
    Log("Text node '%s' set from localization key '%s'!", TEXT_NODE_NAME, TEXT_LOCALIZATION_KEY);
}

// Creates/sets up an example button and wires a callback...
static void SetupExampleButton(void)
{
    int created;
    void* button;

    if (g_buttonReady)
    {
        return;
    }

    created = 0;

    button = MewUI_SetupButtonFromLocalizationKey(SCENE_NAME, BUTTON_NODE_NAME, BUTTON_ROLE_NAME, BUTTON_LABEL_KEY, ExampleButtonCallback, NULL, &g_exampleButton, &created);

    if (!button)
    {
        g_buttonReady = false;
        g_exampleButton = NULL;
        return;
    }

    if (created)
    {
        Log("Button '%s' created: button=%p", BUTTON_ROLE_NAME, button);
    }
    else if (!g_buttonReady)
    {
        Log("Button '%s' ready: button=%p", BUTTON_ROLE_NAME, button);
    }

    g_buttonReady = true;
    UpdateExampleButtonText();
}

static void SetupTestToggle(void* sceneManager)
{
    int created;
    void* button;

    if (g_testToggleReady || !sceneManager)
    {
        return;
    }

    created = 0;
    button = MewUI_SetupToggleInScene(&g_testToggle, sceneManager, &created);

    if (!button)
    {
        g_testToggleReady = false;
        return;
    }

    if (created)
    {
        Log("UI test toggle '%s' created: button=%p", TEST_TOGGLE_ROLE_NAME, button);
    }
    else if (!g_testToggleReady)
    {
        Log("UI test toggle '%s' ready: button=%p", TEST_TOGGLE_ROLE_NAME, button);
    }

    g_testToggleReady = true;
}

static void SetupTestNavigation(void* sceneManager)
{
    int created;

    if (g_testNavigationReady || !sceneManager)
    {
        return;
    }

    created = 0;

    if (!MewUI_SetupNavigationInScene(&g_testNavigation, sceneManager, &created))
    {
        g_testNavigationReady = false;
        return;
    }

    if (created)
    {
        Log("UI test navigation created: left=%p right=%p", g_testNavigation.left_button, g_testNavigation.right_button);
    }
    else if (!g_testNavigationReady)
    {
        Log("UI test navigation ready: left=%p right=%p", g_testNavigation.left_button, g_testNavigation.right_button);
    }

    g_testNavigationReady = true;
}

// Runs UI related stuff...
static void __cdecl UITick(void* userData)
{
    void* sceneManager;

    (void)userData;

    MewUI_RefreshSceneBinding(&g_houseScene);
    sceneManager = MewUI_GetSceneBindingScene(&g_houseScene);

    if (!sceneManager)
    {
        return;
    }

    SetExampleText();
    SetupExampleButton();

    if (!g_testToggleReady || !g_testNavigationReady)
    {
        ++g_testSetupRetryTick;

        if (g_testSetupRetryTick >= TEST_SETUP_RETRY_TICKS)
        {
            g_testSetupRetryTick = 0U;
            SetupTestToggle(sceneManager);
            SetupTestNavigation(sceneManager);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        InitTestBindings();
        MewUI_InitSceneBinding(&g_houseScene, SCENE_NAME, SceneRefreshCallback, NULL);
        MewUI_SetDebugLogsEnabled(false);
        MewUI_Start(MOD_NAME, MEW_UI_HOOK_PRIORITY, UI_BOOTSTRAP_INTERVAL_MS, UI_TICK_INTERVAL_MS, UITick, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        ResetSceneBoundState();
        MewUI_ClearSceneBinding(&g_houseScene);
        MewUI_Stop();
    }

    return true;
}