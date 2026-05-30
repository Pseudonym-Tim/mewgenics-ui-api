using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace MewUICSharpMod;

public static unsafe class ManagedMod
{
    private const string SceneName = "House";
    private const string TextNodeName = "test_text";
    private const string TextLocalizationKey = "TEST_TEXT";
    private const string ButtonNodeName = "test_button";
    private const string ButtonRoleName = "Example_Button";
    private const string ButtonLabelKey = "TEST_BUTTON_TEXT";
    private const uint ExampleButtonCallbackId = 1;

    private static MewUI? s_api;
    private static MewUISceneBinding? s_houseScene;
    private static void* s_exampleButton;
    private static bool s_textReady;
    private static bool s_buttonReady;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int ManagedMod_Initialize(MewUICSharpApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        if (api->Version != 1)
        {
            return 0;
        }

        s_api = new MewUI(api);
        s_houseScene = new MewUISceneBinding(s_api, SceneName);
        s_api.Log("Managed C# mod initialized.");
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void ManagedMod_Tick(void* userData)
    {
        _ = userData;

        if (s_api is null || s_houseScene is null)
        {
            return;
        }

        MewUISceneRefreshResult refreshResult = s_houseScene.Refresh();

        if (refreshResult is MewUISceneRefreshResult.Loaded or MewUISceneRefreshResult.Changed or MewUISceneRefreshResult.Unloaded)
        {
            ResetSceneBoundState();
            s_api.Log($"Scene refresh: {refreshResult}");
        }

        SetExampleText();
        SetupExampleButton();
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void ManagedMod_Shutdown()
    {
        ResetSceneBoundState();
        s_houseScene?.Dispose();
        s_houseScene = null;

        s_api?.Log("Managed C# mod shutdown.");
        s_api = null;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void ManagedMod_OnButtonEvent(void* button, int eventType, int oldState, int newState, uint callbackId)
    {
        _ = button;

        if (s_api is null || callbackId != ExampleButtonCallbackId)
        {
            return;
        }

        string eventName = s_api.ButtonEventName(eventType);
        string oldStateName = s_api.ButtonStateName(oldState);
        string newStateName = s_api.ButtonStateName(newState);

        s_api.Log($"Example button event: {eventName} ({oldStateName} -> {newStateName})");

        if ((MewButtonEvent)eventType == MewButtonEvent.Click)
        {
            s_api.Log("Example button clicked!");
        }
    }

    private static void ResetSceneBoundState()
    {
        s_textReady = false;
        s_buttonReady = false;
        s_exampleButton = null;
    }

    private static void SetExampleText()
    {
        if (s_api is null || s_textReady)
        {
            return;
        }

        int textSet = s_api.SetTextFromLocalizationKey(SceneName, TextNodeName, TextLocalizationKey);

        if (textSet == 0)
        {
            return;
        }

        s_textReady = true;
        s_api.Log($"Text node '{TextNodeName}' set from localization key '{TextLocalizationKey}'.");
    }

    private static void SetupExampleButton()
    {
        if (s_api is null)
        {
            return;
        }

        void* button = s_api.SetupButtonFromLocalizationKey(SceneName, ButtonNodeName, ButtonRoleName, ButtonLabelKey, ExampleButtonCallbackId, ref s_exampleButton, out int created);

        if (button == null)
        {
            s_buttonReady = false;
            s_exampleButton = null;
            return;
        }

        if (created != 0)
        {
            s_api.Log($"Button '{ButtonRoleName}' created: button=0x{(nuint)button:X}");
        }
        else if (!s_buttonReady)
        {
            s_api.Log($"Button '{ButtonRoleName}' ready: button=0x{(nuint)button:X}");
        }

        s_buttonReady = true;
    }
}