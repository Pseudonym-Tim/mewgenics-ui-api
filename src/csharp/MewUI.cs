using System;
using System.Runtime.InteropServices;
using System.Text;

namespace MewUICSharpMod;

internal enum MewButtonEvent
{
    StateChanged = 0,
    HoverEnter = 1,
    HoverExit = 2,
    PressBegin = 3,
    PressCancel = 4,
    Click = 5,
    Disabled = 6
}

internal enum MewUISceneRefreshResult
{
    Unavailable = 0,
    Unchanged = 1,
    Loaded = 2,
    Changed = 3,
    Unloaded = 4
}

[StructLayout(LayoutKind.Sequential)]
public unsafe readonly struct MewUICSharpApi
{
    public readonly uint Version;
    public readonly uint Size;

    public readonly delegate* unmanaged[Cdecl]<byte*, void> Log;
    public readonly delegate* unmanaged[Cdecl]<int, void> SetDebugLogsEnabled;
    public readonly delegate* unmanaged[Cdecl]<int> IsReady;

    public readonly delegate* unmanaged[Cdecl]<nuint> GetSceneBindingSize;
    public readonly delegate* unmanaged[Cdecl]<void*, byte*, void> InitSceneBinding;
    public readonly delegate* unmanaged[Cdecl]<void*, void> ClearSceneBinding;
    public readonly delegate* unmanaged[Cdecl]<void*, int> RefreshSceneBinding;
    public readonly delegate* unmanaged[Cdecl]<void*, void*> GetSceneBindingScene;
    public readonly delegate* unmanaged[Cdecl]<void*, uint> GetSceneBindingGeneration;
    public readonly delegate* unmanaged[Cdecl]<void*, int> IsSceneBindingActive;
    public readonly delegate* unmanaged[Cdecl]<int, byte*> GetSceneRefreshResultName;

    public readonly delegate* unmanaged[Cdecl]<byte*, byte*, byte*, int> SetTextFromLocalizationKey;
    public readonly delegate* unmanaged[Cdecl]<byte*, byte*, byte*, byte*, int> SetTextFromLocalizationKeyValue;

    public readonly delegate* unmanaged[Cdecl]<byte*, byte*, byte*, byte*, uint, void**, int*, void*> SetupButtonFromLocalizationKey;
    public readonly delegate* unmanaged[Cdecl]<byte*, byte*, uint, void**, void*> HookExistingButtonByNodeName;
    public readonly delegate* unmanaged[Cdecl]<byte*, byte*, uint, void**, void*> HookExistingButtonByNodeNameExclusive;
    public readonly delegate* unmanaged[Cdecl]<void*, int, int> SetButtonEnabled;
    public readonly delegate* unmanaged[Cdecl]<void*, int, int> SetButtonInteractable;
    public readonly delegate* unmanaged[Cdecl]<void*, int> ClearButtonInteractOverride;
    public readonly delegate* unmanaged[Cdecl]<void*, int, int> SetButtonSuppressOriginalActivate;
    public readonly delegate* unmanaged[Cdecl]<int, byte*> GetButtonEventName;
    public readonly delegate* unmanaged[Cdecl]<int, byte*> GetButtonStateName;
}

internal sealed unsafe class MewUISceneBinding : IDisposable
{
    private readonly MewUI _api;
    private void* _binding;

    public MewUISceneBinding(MewUI api, string sceneName)
    {
        _api = api;

        nuint size = _api.Api->GetSceneBindingSize();
        _binding = NativeMemory.AllocZeroed(size);
        _api.WithUtf8(sceneName, sceneNamePtr => _api.Api->InitSceneBinding(_binding, sceneNamePtr));
    }

    public MewUISceneRefreshResult Refresh()
    {
        if (_binding == null)
        {
            return MewUISceneRefreshResult.Unavailable;
        }

        return (MewUISceneRefreshResult)_api.Api->RefreshSceneBinding(_binding);
    }

    public void Dispose()
    {
        if (_binding == null)
        {
            return;
        }

        _api.Api->ClearSceneBinding(_binding);
        NativeMemory.Free(_binding);
        _binding = null;
    }
}

internal unsafe sealed class MewUI
{
    public unsafe delegate T Utf8Func<T>(byte* value);
    public unsafe delegate void Utf8Action(byte* value);

    private readonly MewUICSharpApi* _api;

    public MewUI(MewUICSharpApi* api)
    {
        _api = api;
    }

    public MewUICSharpApi* Api => _api;

    public void Log(string message)
    {
        WithUtf8(message, messagePtr => _api->Log(messagePtr));
    }

    public string ButtonEventName(int eventType)
    {
        return FromUtf8(_api->GetButtonEventName(eventType));
    }

    public string ButtonStateName(int state)
    {
        return FromUtf8(_api->GetButtonStateName(state));
    }

    public int SetTextFromLocalizationKey(string sceneName, string childName, string key)
    {
        return WithUtf8(sceneName, sceneNamePtr =>
            WithUtf8(childName, childNamePtr =>
                WithUtf8(key, keyPtr => _api->SetTextFromLocalizationKey(sceneNamePtr, childNamePtr, keyPtr))));
    }

    public void* SetupButtonFromLocalizationKey(string sceneName, string nodeName, string roleName, string labelKey, uint callbackId, ref void* button, out int created)
    {
        int createdValue = 0;
        void* buttonValue = button;

        void** buttonSlot = stackalloc void*[1];
        int* createdSlot = stackalloc int[1];
        buttonSlot[0] = buttonValue;
        createdSlot[0] = createdValue;

        nint result = WithUtf8<nint>(sceneName, sceneNamePtr =>
            WithUtf8<nint>(nodeName, nodeNamePtr =>
                WithUtf8<nint>(roleName, roleNamePtr =>
                    WithUtf8<nint>(labelKey, labelKeyPtr => (nint)_api->SetupButtonFromLocalizationKey(sceneNamePtr, nodeNamePtr, roleNamePtr, labelKeyPtr, callbackId, buttonSlot, createdSlot)))));

        button = buttonSlot[0];
        created = createdSlot[0];
        return (void*)result;
    }

    public T WithUtf8<T>(string? value, Utf8Func<T> action)
    {
        if (value is null)
        {
            return action(null);
        }

        int byteCount = Encoding.UTF8.GetByteCount(value);
        byte* buffer = stackalloc byte[byteCount + 1];
        fixed (char* chars = value)
        {
            Encoding.UTF8.GetBytes(chars, value.Length, buffer, byteCount);
        }

        buffer[byteCount] = 0;
        return action(buffer);
    }

    public void WithUtf8(string? value, Utf8Action action)
    {
        WithUtf8<int>(value, ptr =>
        {
            action(ptr);
            return 0;
        });
    }

    private static string FromUtf8(byte* value)
    {
        return value == null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)value) ?? string.Empty;
    }
}