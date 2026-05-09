# MewUI API

A small C/C++ helper API for [Mewgenics](https://store.steampowered.com/app/686060/Mewgenics/) DLL mods that want to create new in-game UI, modify existing UI, update UI states, or hook into UI systems.

This is designed to work alongside [Mewjector](https://github.com/githubuser508/mewjector) and is a requirement. Essentially this API wraps the common low-level tasks mod authors usually have to do a lot of work to duplicate/pull off and makes it much simpler! Things such as: Waiting for scene-ready timing, finding loaded scenes, replacing localized text, preparing formatted text, creating new buttons from existing UI nodes, hooking game-created buttons, and receiving button events.

The API is intended for Windows DLL mods. Mods should include `mew_ui_api.h`, compile `mew_ui_api.c`, and call `MewUI_Start()` during startup.

## How It Works

MewUI API installs its hooks through Mewjector and does mod UI work during the game's scene-ready update path. When your mod starts the API, it:

1. Stores your mod owner name, hook priority, bootstrap interval, and tick callback
2. Tries to resolve the Mewjector API from `version.dll`
3. Installs scene-ready hooks once Mewjector and the game base are available
4. Checks that the current scene is not destroying, not in transition, and is indeed ready for UI work
5. Calls `MewUI_Tick()` so tracked button state changes stay synchronized
6. Calls your mod's UI tick callback on the game thread
7. Logs through Mewjector using the owner name you passed to `MewUI_Start()`

We keep loader-lock-sensitive work out of `DllMain` and avoid touching UI state before a scene is ready!

## UI API

This API exposes all kinds of scene, text, button, and lifecycle helpers!

| Function | Purpose |
|----------|---------|
| `MewUI_Start` | Starts the API and defers hook installation until Mewjector is ready |
| `MewUI_Stop` | Stops the bootstrap timer and clears API-owned runtime state |
| `MewUI_IsReady` | Returns whether the API finished installing its hooks |
| `MewUI_LogMessage` | Writes to the shared Mewjector log with the current owner name |
| `MewUI_SetDebugLogsEnabled` | Enables or disables internal API debug logging |
| `MewUI_InitSceneBinding` | Initializes a binding for a named scene |
| `MewUI_RefreshSceneBinding` | Refreshes a binding and reports load, unload, or pointer changes |
| `MewUI_GetSceneByName` | Finds a loaded scene by engine scene name |
| `MewUI_SetTextFromLocalizationKey` | Sets an existing text node from a localization key |
| `MewUI_SetTextFromLocalizationKeyValue` | Sets localized text with one `{v0}` replacement |
| `MewUI_SetTextFromLocalizationKeyValues` | Sets localized text with multiple replacement values (`{v0}`, `{v1}`, `{v3}`) |
| `MewUI_PrepareTextFormat` | Caches a localized format for cheap repeated updates |
| `MewUI_SetPreparedTextFormatTypedValues` | Applies typed values to a prepared text format |
| `MewUI_SetupButtonFromLocalizationKey` | Creates or reuses a button and applies a localized label |
| `MewUI_SetupButtonWithoutLabel` | Creates or reuses a button without changing its label |
| `MewUI_HookExistingButtonByNodeName` | Hooks a game-created button while preserving the original click path |
| `MewUI_HookExistingButtonByNodeNameExclusive` | Hooks a game-created button and suppresses the original activate path |
| `MewUI_SetButtonEnabled` | Toggles the component enabled flag and activation gate |
| `MewUI_SetButtonInteractable` | Forces a tracked button to be interactable or non-interactable |
| `MewUI_SetButtonCanInteractCallback` | Lets a callback decide whether a button can interact this frame |
| `MewUI_ClearButtonInteractOverride` | Returns a tracked button to the game's default interactability result |
| `MewUI_GetButtonEventName` | Returns a readable button event name for logs |
| `MewUI_GetButtonStateName` | Returns a readable button state name for logs |

Include `mew_ui_api.h` and compile `mew_ui_api.c` into your mod. The API itself depends on `mewjector.h` for runtime Mewjector resolution.

## SWF/FLA Modification Related Notes

(IMPORTANT: For the sake of brevity, this explanation is going to assume that you know how to extract the game's `resources.gpak`, and are at least aware of how FLA/SWFs work in the context of the game.)

The API works after the game has loaded the required SWF files. So while the DLL side can do a lot of stuff on it's own, for any completely new additions to the UI, you need to author, export, and load your own custom SWF files into the game first.

An example FLA and mod files showcasing `house.swf` UI modification is provided with this project. Use it as the reference for how custom UI should be pulled off, such as how it should be named, structured, and animated.

For new UI elements, you generally start by copying the portion of the existing UI that you want to modify into a new 1280x720 FLA file. From there, edit the layout, graphics, instance names, and animations as needed, then export it as a custom SWF. That SWF must then be loaded by the game through a `swfs/swflist.gon.append` file in your mod before the MewUI API can interact with it.

The most important thing to realize with this API is that the text name/button name you pass into various API functions is the "instance name" set in the FLA/SWF. The name in the code must match EXACTLY what it is in the SWF/FLA, including spelling, underscores, and capitalization.

For any text localization keys that are used for new UI in your code mod, you will need to add them in a `combined.csv.append` file in your mod.

## Scene Bindings

Scene bindings give your UI mod a way to react when a scene loads, unloads, or changes scene-manager pointers. This is the recommended place to clear cached text, button, or prepared-format state or whatever else you need.

```c
static MewUISceneBinding g_houseScene;
static void* g_exampleButton = NULL;
static bool g_textReady = false;
static bool g_buttonReady = false;

static void ResetSceneBoundState(void)
{
    g_textReady = false;
    g_buttonReady = false;
    g_exampleButton = NULL;
}

static void __cdecl SceneRefreshCallback(MewUISceneBinding* binding, MewUISceneRefreshResult result, void* oldSceneManager, void* newSceneManager, void* userData)
{
    (void)binding;
    (void)oldSceneManager;
    (void)newSceneManager;
    (void)userData;

    if (result == MEW_UI_SCENE_REFRESH_LOADED || result == MEW_UI_SCENE_REFRESH_CHANGED || result == MEW_UI_SCENE_REFRESH_UNLOADED)
    {
        ResetSceneBoundState();
    }

    MewUI_LogMessage("Scene refresh: %s", MewUI_GetSceneRefreshResultName(result));
}
```

Initialize the binding once, then refresh it from your UI tick:

```c
static void InitSceneTracking(void)
{
    MewUI_InitSceneBinding(&g_houseScene, "House", SceneRefreshCallback, NULL);
}

static void __cdecl UITick(void* userData)
{
    (void)userData;

    MewUI_RefreshSceneBinding(&g_houseScene);
}
```

## Text Updates

Set an existing text node from a localization key:

```c
static const char* const SCENE_NAME = "House";
static const char* const TEXT_NODE_NAME = "test_text";
static const char* const TEXT_LOCALIZATION_KEY = "TEST_TEXT";

static bool g_textReady = false;

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
    MewUI_LogMessage("Text node '%s' set from localization key '%s'", TEXT_NODE_NAME, TEXT_LOCALIZATION_KEY);
}
```

Set localized text with a single placeholder value:

```c
static void SetHealthText(uint32_t health)
{
    char valueBuffer[32];
    int written;
    int textSet;

    written = snprintf(valueBuffer, sizeof(valueBuffer), "%u", (unsigned int)health);

    if (written < 0)
    {
        return;
    }

    textSet = MewUI_SetTextFromLocalizationKeyValue("House", "health_text", "HEALTH_TEXT", valueBuffer);

    if (!textSet)
    {
        return;
    }
}
```

Example `combined.csv.append` localization text:

```text
HEALTH_TEXT,"Health: {v0}",,,,
```

## Prepared Text Formats

Prepared text format functions can cache the target text node and localized format segments. Use them for values that will update often, such as: Score, health, timers, counters, currency, whatever.

```c
static MewUITextFormat g_scoreText;

static void UpdateScoreText(uint32_t score)
{
    int prepared;
    int updated;

    if (!g_scoreText.prepared)
    {
        prepared = MewUI_PrepareTextFormat("House", "score_text", "SCORE_TEXT", &g_scoreText);

        if (!prepared)
        {
            return;
        }
    }

    updated = MewUI_SetPreparedTextFormatUInt32(&g_scoreText, score);

    if (!updated)
    {
        return;
    }
}

static void ClearPreparedText(void)
{
    MewUI_ClearTextFormat(&g_scoreText);
}
```

Typed multi-value text formatting is supported:

```c
static MewUITextFormat g_statsText;

static void UpdateStatsText(uint32_t level, float accuracy)
{
    MewUITextFormatValue values[2];
    int prepared;
    int updated;

    if (!g_statsText.prepared)
    {
        prepared = MewUI_PrepareTextFormat("House", "stats_text", "STATS_TEXT", &g_statsText);

        if (!prepared)
        {
            return;
        }
    }

    values[0].type = MEW_UI_TEXT_FORMAT_VALUE_UINT32;
    values[0].precision = 0U;
    values[0].data.uint32_value = level;

    values[1].type = MEW_UI_TEXT_FORMAT_VALUE_FLOAT;
    values[1].precision = 2U;
    values[1].data.float_value = accuracy;

    updated = MewUI_SetPreparedTextFormatTypedValues(&g_statsText, values, 2U);

    if (!updated)
    {
        return;
    }
}
```

Example `combined.csv.append` localization text:

```text
STATS_TEXT,"Level: {v0} - Accuracy {v1}%",,,,
```

## Buttons

Create or reuse a button from an existing UI node, apply a localized label, and receive button events:

```c
static const char* const BUTTON_NODE_NAME = "test_button";
static const char* const BUTTON_ROLE_NAME = "Example_Button";
static const char* const BUTTON_LABEL_KEY = "TEST_BUTTON_TEXT";

static void* g_exampleButton = NULL;
static bool g_buttonReady = false;

static void __cdecl ExampleButtonCallback(void* button, MewButtonEvent eventType, MewButtonState oldState, MewButtonState newState, void* userData)
{
    (void)button;
    (void)userData;

    MewUI_LogMessage("Example button event: %s (%s -> %s)", MewUI_GetButtonEventName(eventType), MewUI_GetButtonStateName(oldState), MewUI_GetButtonStateName(newState));

    if (eventType == MEW_BUTTON_EVENT_CLICK)
    {
        MewUI_LogMessage("Example button clicked");
    }
}

static void SetupExampleButton(void)
{
    int created;
    void* button;

    created = 0;
    button = MewUI_SetupButtonFromLocalizationKey("House", BUTTON_NODE_NAME, BUTTON_ROLE_NAME, BUTTON_LABEL_KEY, ExampleButtonCallback, NULL, &g_exampleButton, &created);

    if (!button)
    {
        g_buttonReady = false;
        g_exampleButton = NULL;
        return;
    }

    if (created)
    {
        MewUI_LogMessage("Button '%s' created: button=%p", BUTTON_ROLE_NAME, button);
    }
    else if (!g_buttonReady)
    {
        MewUI_LogMessage("Button '%s' ready: button=%p", BUTTON_ROLE_NAME, button);
    }

    g_buttonReady = true;
}
```

Hook a game-created button without replacing its original behavior:

```c
static void* g_continueButton = NULL;

static void __cdecl ContinueButtonCallback(void* button, MewButtonEvent eventType, MewButtonState oldState, MewButtonState newState, void* userData)
{
    (void)button;
    (void)oldState;
    (void)newState;
    (void)userData;

    if (eventType == MEW_BUTTON_EVENT_CLICK)
    {
        MewUI_LogMessage("Continue button clicked");
    }
}

static void HookContinueButton(void)
{
    void* button;

    button = MewUI_HookExistingButtonByNodeName("Battle", "bMove", ContinueButtonCallback, NULL, &g_continueButton);

    if (!button)
    {
        return;
    }
}
```

Hook a game-created button and suppress the original activation path:

```c
static void HookExclusiveDebugButton(void)
{
    void* button;

    button = MewUI_HookExistingButtonByNodeNameExclusive("Battle", "bMove", ContinueButtonCallback, NULL, &g_continueButton);

    if (!button)
    {
        return;
    }
}
```

## Button State and Interactability

Disable/enable a tracked button:

```c
static void SetExampleButtonEnabled(bool enabled)
{
    if (!g_exampleButton)
    {
        return;
    }

    MewUI_SetButtonEnabled(g_exampleButton, enabled ? 1 : 0);
}
```

Force interactability:

```c
static void SetExampleButtonInteractable(bool interactable)
{
    if (!g_exampleButton)
    {
        return;
    }

    MewUI_SetButtonInteractable(g_exampleButton, interactable ? 1 : 0);
}
```

Use a callback to decide interactability each frame:

```c
static uint8_t __cdecl CanUseExampleButton(void* button, uint8_t originalResult, void* userData)
{
    bool* allowButton;

    (void)button;
    (void)originalResult;

    allowButton = (bool*)userData;

    if (!allowButton)
    {
        return 0U;
    }

    return *allowButton ? 1U : 0U;
}

static void InstallInteractCallback(bool* allowButton)
{
    if (!g_exampleButton)
    {
        return;
    }

    MewUI_SetButtonCanInteractCallback(g_exampleButton, CanUseExampleButton, allowButton);
}
```

Return to the game's default interactability logic:

```c
static void ClearExampleButtonInteractOverride(void)
{
    if (!g_exampleButton)
    {
        return;
    }

    MewUI_ClearButtonInteractOverride(g_exampleButton);
}
```

## Recommended UI Tick

Most mods should handle UI work responsibly and carefully. Do not update something every frame if you don't need to! Cache things and use scene binding refreshes. Helper calls will simply fail until the scene and the target nodes are available. Your `UITick` and `ShutdownUIState(void)` should ideally look as shown below!

```c
static void __cdecl UITick(void* userData)
{
    (void)userData;

    MewUI_RefreshSceneBinding(&g_houseScene);

    SetExampleText();
    SetupExampleButton();
}
```

Clear state during detach:

```c
static void ShutdownUIState(void)
{
    ResetSceneBoundState();
    MewUI_ClearTextFormat(&g_scoreText);
    MewUI_ClearSceneBinding(&g_houseScene);
    MewUI_Stop();
}
```

## Building from Source

Requires MSVC, such as Visual Studio Build Tools or full Visual Studio.

Compile `mew_ui_api.c` together with your mod source:

```bat
cl /LD /O2 /W4 my_mod.c mew_ui_api.c /Fe:MyMod.dll
```

Or add these files to your Visual Studio mod project:

- `mew_ui_api.h`
- `mew_ui_api.c`
- `mewjector.h`

The API is meant to be built into your mod DLL. It does NOT require a separate import library.

## Compatibility

- Windows 10/11 (Ideally)
- Most recent non-beta Mewgenics game build
- Mewjector API v3 or newer
- MSVC-compatible C build environment

## Mewjector Integration

MewUI API uses [Mewjector](https://github.com/githubuser508/mewjector) for shared hook installation, game-base lookup, and logging. Mewjector hook chaining allows multiple mods to hook the same RVA without blindly overwriting each other.

Mods using this API should still be loaded through Mewjector or a Mewjector-compatible setup. If Mewjector is not available, `MewUI_Start()` keeps retrying instead of installing hooks immediately.

## License

[MIT](LICENSE)
