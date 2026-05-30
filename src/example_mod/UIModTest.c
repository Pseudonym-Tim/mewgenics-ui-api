#include "mew_ui_api.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static const char* const MOD_NAME = "Set Text And Button Example Mod";
static const char* const SCENE_NAME = "House";
static const char* const TEXT_NODE_NAME = "test_text";
static const char* const TEXT_LOCALIZATION_KEY = "TEST_TEXT";
static const char* const BUTTON_NODE_NAME = "test_button";
static const char* const BUTTON_ROLE_NAME = "Example_Button";
static const char* const BUTTON_LABEL_KEY = "TEST_BUTTON_TEXT";

static const int MEW_UI_HOOK_PRIORITY = 30;
static const uint32_t UI_BOOTSTRAP_INTERVAL_MS = 100U;
static const uint32_t UI_TICK_INTERVAL_MS = 16U; // (60fps)...

static MewUISceneBinding g_houseScene;
static void* g_exampleButton = NULL;
static bool g_textReady = false;
static bool g_buttonReady = false;

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

// Clears cached scene state whenever the scene unloads or swaps...
static void ResetSceneBoundState(void)
{
    g_textReady = false;
    g_buttonReady = false;
    g_exampleButton = NULL;
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

// Reports button events...
static void __cdecl ExampleButtonCallback(void* button, MewButtonEvent eventType, MewButtonState oldState, MewButtonState newState, void* userData)
{
    (void)button;
    (void)userData;

    Log("Example button event: %s (%s -> %s)", MewUI_GetButtonEventName(eventType), MewUI_GetButtonStateName(oldState), MewUI_GetButtonStateName(newState));

    if (eventType == MEW_BUTTON_EVENT_CLICK)
    {
        Log("Example button clicked!");
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
}

// Runs UI related stuff...
static void __cdecl UITick(void* userData)
{
    (void)userData;

    MewUI_RefreshSceneBinding(&g_houseScene);

    SetExampleText();
    SetupExampleButton();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

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

    return TRUE;
}