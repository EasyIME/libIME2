//
//    Copyright (C) 2013 - 2020 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU Library General Public
//    License as published by the Free Software Foundation; either
//    version 2 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Library General Public License for more details.
//
//    You should have received a copy of the GNU Library General Public
//    License along with this library; if not, write to the
//    Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
//    Boston, MA  02110-1301, USA.
//

#include "ImeModule.h"
#include <string>
#include <algorithm>
#include <memory>
#include <ObjBase.h>
#include <msctf.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <assert.h>
#include <VersionHelpers.h>  // Provided by Windows SDK >= 8.1

#include "Window.h"
#include "TextService.h"
#include "DisplayAttributeProvider.h"

using namespace std;

namespace Ime {

// these values are not defined in older TSF SDK (windows xp)
#ifndef TF_IPP_CAPS_IMMERSIVESUPPORT
// for Windows 8
// GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT {13A016DF-560B-46CD-947A-4C3AF1E0E35D}
static const GUID GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT =
{ 0x13A016DF, 0x560B, 0x46CD, { 0x94, 0x7A, 0x4C, 0x3A, 0xF1, 0xE0, 0xE3, 0x5D } };
// GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT {25504FB4-7BAB-4BC1-9C69-CF81890F0EF5}
static const GUID GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT =
{ 0x25504FB4, 0x7BAB, 0x4BC1, { 0x9C, 0x69, 0xCF, 0x81, 0x89, 0x0F, 0x0E, 0xF5 } };
#endif

// refCountMutex needs to be static because it may be accessed after Release() calls the destructor.
std::mutex ImeModule::refCountMutex_;

ImeModule::ImeModule(HMODULE module, const CLSID& textServiceClsid, std::vector<ComPtr<DisplayAttributeInfo>> displayAttrInfos):
    hInstance_(HINSTANCE(module)),
    textServiceClsid_(textServiceClsid),
    displayAttrInfos_(std::move(displayAttrInfos)) {

    Window::registerClass(hInstance_);

    registerDisplayAttributeInfos();
}

ImeModule::~ImeModule(void) {
}

// Dll entry points implementations
HRESULT ImeModule::canUnloadNow() {
    // we own the last reference
    return refCount() <= 1 ? S_OK : S_FALSE;
}

HRESULT ImeModule::getClassObject(REFCLSID rclsid, REFIID riid, void **ppvObj) {
    if (IsEqualIID(riid, IID_IClassFactory) || IsEqualIID(riid, IID_IUnknown)) {
        // increase reference count
        AddRef();
        *ppvObj = (IClassFactory*)this; // our own object implements IClassFactory
        return NOERROR;
    }
    else {
        *ppvObj = NULL;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

static void loadDefaultUserRegistry(const wchar_t* defaultUserRegKey) {
    // The registry settings of all newly created users are based on the content of 
    // "C:\Users\Default User\ntuser.dat", so we need to write our settings to this file so 
    // the HKEY_CURRENT_USER key of newly created users can also contain our settings.
    // In order to do this, we need to load the default "hive" to registry first.
    // Reference: https://msdn.microsoft.com/zh-tw/library/windows/desktop/ms724889(v=vs.85).aspx
    wchar_t *userProfilesDir = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_UserProfiles, 0, NULL, &userProfilesDir))) {
        // get the path of the default ntuser.dat file
        std::wstring defaultRegFile = userProfilesDir;
        defaultRegFile += L"\\Default User\\ntuser.dat";
        if (GetFileAttributesW(defaultRegFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
            defaultRegFile = userProfilesDir;
            defaultRegFile += L"\\Default\\ntuser.dat";
        }
        ::CoTaskMemFree(userProfilesDir);

        // loading registry file requires special privileges SE_RESTORE_NAME and SE_BACKUP_NAME.
        // So let's do privilege elevation for our process.
        HANDLE processToken = NULL;
        ::OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &processToken);
        DWORD bufLen = sizeof(TOKEN_PRIVILEGES) + sizeof(LUID_AND_ATTRIBUTES);
        std::unique_ptr<char> buf(new char[bufLen]);
        TOKEN_PRIVILEGES* privileges = reinterpret_cast<TOKEN_PRIVILEGES*>(buf.get());
        privileges->PrivilegeCount = 2;
        ::LookupPrivilegeValue(NULL, SE_RESTORE_NAME, &privileges->Privileges[0].Luid);
        privileges->Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ::LookupPrivilegeValue(NULL, SE_BACKUP_NAME, &privileges->Privileges[1].Luid);
        privileges->Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;
        ::AdjustTokenPrivileges(processToken, FALSE, privileges, bufLen, NULL, NULL);
        ::CloseHandle(processToken);

        // load the default registry hive under the specified key name
        ::RegLoadKeyW(HKEY_USERS, defaultUserRegKey, defaultRegFile.c_str());
    }
}

// ============================================================================
// Windows 8+ User Profile Language Settings Helpers
//
// Background & Rationale:
// Starting with Windows 8 (and continued in Windows 10 & 11), Windows introduced
// a per-user language profile store located at:
//   HKEY_CURRENT_USER\Control Panel\International\User Profile\<locale_name>
//
// In this key, each installed keyboard layout or TSF Text Service profile is
// represented by a REG_DWORD value:
//   Name: "<lang ID in hex>:{text service CLSID}{profile GUID}"
//   Data: 1-based DWORD sequence number (1, 2, ..., N)
//
// Why this registry manipulation is necessary:
// Microsoft's classic Win32 TSF APIs (ITfInputProcessorProfiles::AddLanguageProfile)
// only register the TIP in HKLM\SOFTWARE\Microsoft\CTF\TIP. They do NOT add the
// profile to the active user's modern Language Profile store. Without writing to
// this key, the input method will NOT show up in the modern Win+Space switcher
// or in Settings until the user manually configures it in Windows Settings.
//
// Windows 11 InputSwitch.dll Crash Prevention:
// The taskbar language switcher (InputSwitch.dll!CtfHandler::GetActiveProfile)
// indexes an internal array sized to the number of installed profiles using these
// 1-based DWORD values. If any value exceeds the total profile count (e.g. from
// counting non-profile metadata like "CachedLanguageName" on install, or leaving
// gaps after uninstalling a profile), InputSwitch.dll dereferences past the array
// bounds and crashes explorer.exe.
//
// To prevent explorer.exe crashes, these helpers ensure:
//   1. All existing profiles under <locale_name> are collected.
//   2. Non-DWORD and non-profile values (like CachedLanguageName) are ignored.
//   3. Surviving/active profiles are sorted by their existing order to preserve
//      the user's layout preferences.
//   4. Orders are strictly renumbered to a contiguous 1-based sequence: 1, 2, ..., N.
//
// Note: Keep these helpers self-contained so they can easily be extracted into
// a dedicated UserProfileRegistry module in a future refactor.
// ============================================================================

namespace {

// Named constants for well-known Windows service account SIDs and hive suffixes.
// Note: The Windows SDK only defines binary RIDs (e.g. SECURITY_LOCAL_SYSTEM_RID in winnt.h)
// and SDDL 2-letter tokens (e.g. SDDL_LOCAL_SYSTEM "SY" in sddl.h), but does not provide
// string constants for HKEY_USERS subkey names.
constexpr wchar_t kLocalSystemSid[]    = L"S-1-5-18";
constexpr wchar_t kLocalServiceSid[]   = L"S-1-5-19";
constexpr wchar_t kNetworkServiceSid[] = L"S-1-5-20";
constexpr wchar_t kClassesSuffix[]     = L"_Classes";

struct ProfileOrderEntry {
    std::wstring name;
    DWORD order;
    bool isNew = false;
};

// Check if a SID key under HKEY_USERS is a valid user profile hive.
// We must skip:
// - User Classes hives ("_Classes"), which store COM HKCR registrations and should
//   never contain Control Panel settings.
// - Service accounts (S-1-5-18 = LocalSystem, S-1-5-19 = LocalService, S-1-5-20 = NetworkService).
static bool isValidUserProfileSid(const wchar_t* sid) {
    if (!sid || !*sid)
        return false;
    if (wcsstr(sid, kClassesSuffix) != nullptr)
        return false;
    if (_wcsicmp(sid, kLocalSystemSid) == 0 ||
        _wcsicmp(sid, kLocalServiceSid) == 0 ||
        _wcsicmp(sid, kNetworkServiceSid) == 0) {
        return false;
    }
    return true;
}

// Enumerate all valid keyboard/IME layout profile values under a locale key.
// Ignores metadata values (such as "CachedLanguageName" which is REG_SZ).
static std::vector<ProfileOrderEntry> getLocaleProfiles(HKEY localeKey) {
    std::vector<ProfileOrderEntry> entries;
    DWORD valueCount = 0;
    DWORD maxValueNameLen = 0;
    if (::RegQueryInfoKeyW(localeKey, NULL, NULL, NULL, NULL, NULL, NULL, &valueCount, &maxValueNameLen, NULL, NULL, NULL) != ERROR_SUCCESS) {
        return entries;
    }
    std::vector<wchar_t> nameBuf(maxValueNameLen + 2);
    for (DWORD i = 0; i < valueCount; ++i) {
        DWORD nameLen = static_cast<DWORD>(nameBuf.size());
        DWORD type = 0;
        DWORD order = 0;
        DWORD dataSize = sizeof(order);
        if (::RegEnumValueW(localeKey, i, nameBuf.data(), &nameLen, NULL, &type, reinterpret_cast<LPBYTE>(&order), &dataSize) == ERROR_SUCCESS) {
            // Profile values are REG_DWORD and formatted as "<langid>:<layout/guid>" (must contain ':')
            if (type == REG_DWORD && wcschr(nameBuf.data(), L':') != nullptr) {
                entries.push_back({ std::wstring(nameBuf.data(), nameLen), order, false });
            }
        }
    }
    return entries;
}

// Renumber profiles sequentially (1, 2, ..., N), preserving existing relative ordering.
static void renumberLocaleProfiles(HKEY localeKey, std::vector<ProfileOrderEntry>& profiles) {
    std::stable_sort(profiles.begin(), profiles.end(), [](const ProfileOrderEntry& a, const ProfileOrderEntry& b) {
        return a.order < b.order;
    });
    for (size_t i = 0; i < profiles.size(); ++i) {
        DWORD newOrder = static_cast<DWORD>(i + 1);
        if (profiles[i].isNew || profiles[i].order != newOrder) {
            profiles[i].order = newOrder;
            profiles[i].isNew = false;
            ::RegSetValueExW(localeKey, profiles[i].name.c_str(), 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&newOrder), sizeof(newOrder));
        }
    }
}

// Write language profile settings to modern Windows 8+ per-user registry
// ("HKEY_USERS\<sid>\Control Panel\International\User Profile\<locale_name>").
// Ensures all profiles are sequentially renumbered (1..N) to prevent InputSwitch.dll crashes.
static HRESULT registerUserProfiles(const CLSID& textServiceClsid, LangProfileInfo* langs, int langsCount) {
    if (!::IsWindows8OrGreater()) {
        return S_OK;
    }

    DWORD sidCount = 0;
    if (::RegQueryInfoKeyW(HKEY_USERS, NULL, NULL, NULL, &sidCount, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
        // Do not abort on failure; we still want the rest of the registration to succeed.
        sidCount = 0;
    }
    wchar_t* textServiceClsIdStr = nullptr;
    if (FAILED(::StringFromCLSID(textServiceClsid, &textServiceClsIdStr))) {
        return E_FAIL;
    }

    const wchar_t* defaultUserRegKey = L"__PIME_Default_user__";
    loadDefaultUserRegistry(defaultUserRegKey);

    // Write the language settings to user-specific registry.
    wchar_t sid[256];
    for (DWORD iSid = 0; iSid < sidCount; ++iSid) {
        DWORD sidLen = sizeof(sid) / sizeof(wchar_t);
        if (::RegEnumKeyExW(HKEY_USERS, iSid, sid, &sidLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            if (!isValidUserProfileSid(sid)) {
                continue;
            }
            // Write settings of each input module to the user's registry
            for (int i = 0; i < langsCount; ++i) {
                auto& lang = langs[i];
                std::wstring localeRegPath = sid;
                localeRegPath += L"\\Control Panel\\International\\User Profile\\";
                localeRegPath += lang.locale;
                HKEY localeRegKey = NULL;
                DWORD err = ::RegCreateKeyExW(HKEY_USERS, localeRegPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &localeRegKey, NULL);
                if (err == ERROR_SUCCESS) {
                    LCID lcid = LocaleNameToLCID(lang.locale.c_str(), 0);
                    if (lcid == 0 && !lang.fallbackLocale.empty()) { // the conversion fails
                        lcid = LocaleNameToLCID(lang.fallbackLocale.c_str(), 0);  // try the fallback locale name
                    }
                    wchar_t lcid_hex[16];
                    wsprintf(lcid_hex, L"%04x", lcid);
                    std::wstring valueName = lcid_hex;
                    valueName += L":";
                    valueName += textServiceClsIdStr;
                    wchar_t* profileClsIdStr = nullptr;
                    if (SUCCEEDED(::StringFromCLSID(lang.profileGuid, &profileClsIdStr))) {
                        valueName += profileClsIdStr;
                        ::CoTaskMemFree(profileClsIdStr);

                        auto profiles = getLocaleProfiles(localeRegKey);
                        auto it = std::find_if(profiles.begin(), profiles.end(), [&](const ProfileOrderEntry& e) {
                            return _wcsicmp(e.name.c_str(), valueName.c_str()) == 0;
                        });
                        if (it == profiles.end()) {
                            DWORD maxOrder = 0;
                            for (const auto& p : profiles) {
                                if (p.order > maxOrder)
                                    maxOrder = p.order;
                            }
                            profiles.push_back({ valueName, maxOrder + 1, true });
                        }
                        // Compact and renumber all profiles (1..N) to ensure a contiguous sequence
                        renumberLocaleProfiles(localeRegKey, profiles);
                    }
                    ::RegCloseKey(localeRegKey);
                }
            }
        }
    }
    ::CoTaskMemFree(textServiceClsIdStr);

    // Unload the default user registry hive
    ::RegUnLoadKeyW(HKEY_USERS, defaultUserRegKey);
    return S_OK;
}

// Remove language profile settings from modern Windows 8+ per-user registry
// ("HKEY_USERS\<sid>\Control Panel\International\User Profile\<locale_name>").
// Renumbers surviving profiles sequentially (1..K) to eliminate gaps that crash InputSwitch.dll.
static HRESULT unregisterUserProfiles(const CLSID& textServiceClsid) {
    if (!::IsWindows8OrGreater()) {
        return S_OK;
    }

    DWORD sidCount = 0;
    if (::RegQueryInfoKeyW(HKEY_USERS, NULL, NULL, NULL, &sidCount, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
        // Do not abort on failure; we still want the rest of the unregistration to succeed.
        sidCount = 0;
    }
    wchar_t* textServiceClsIdStr = nullptr;
    if (FAILED(::StringFromCLSID(textServiceClsid, &textServiceClsIdStr))) {
        return E_FAIL;
    }

    const wchar_t* defaultUserRegKey = L"__PIME_Default_user__";
    loadDefaultUserRegistry(defaultUserRegKey);

    // Delete the language settings from user-specific registry.
    wchar_t sid[256];
    for (DWORD iSid = 0; iSid < sidCount; ++iSid) {
        DWORD sidLen = sizeof(sid) / sizeof(wchar_t);
        if (::RegEnumKeyExW(HKEY_USERS, iSid, sid, &sidLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            // If this is a _Classes key, clean up any bogus Control Panel entries mistakenly created
            // by older PIME versions that did not filter out _Classes.
            if (wcsstr(sid, kClassesSuffix) != nullptr) {
                std::wstring bogusPath = sid;
                bogusPath += L"\\Control Panel\\International\\User Profile";
                ::SHDeleteKeyW(HKEY_USERS, bogusPath.c_str());
                continue;
            }
            if (!isValidUserProfileSid(sid)) {
                continue;
            }

            // Remove settings of each input module from the user's registry
            std::wstring userRegPath = sid;
            userRegPath += L"\\Control Panel\\International\\User Profile";
            HKEY userKey = NULL;
            if (::RegOpenKeyExW(HKEY_USERS, userRegPath.c_str(), 0, KEY_READ, &userKey) == ERROR_SUCCESS) {
                DWORD localeCount = 0;
                if (::RegQueryInfoKeyW(userKey, NULL, NULL, NULL, &localeCount, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                    // List all locales under this user
                    wchar_t locale[100];
                    for (DWORD iLocale = 0; iLocale < localeCount; ++iLocale) {
                        DWORD localeLen = sizeof(locale) / sizeof(wchar_t);
                        if (::RegEnumKeyExW(userKey, iLocale, locale, &localeLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                            HKEY localeKey = NULL;
                            if (::RegOpenKeyExW(userKey, locale, 0, KEY_ALL_ACCESS | KEY_READ, &localeKey) == ERROR_SUCCESS) {
                                auto profiles = getLocaleProfiles(localeKey);
                                std::vector<ProfileOrderEntry> remainingProfiles;
                                for (const auto& p : profiles) {
                                    if (wcsstr(p.name.c_str(), textServiceClsIdStr) != nullptr) {
                                        // Belongs to us: delete the profile value
                                        ::RegDeleteValueW(localeKey, p.name.c_str());
                                    } else {
                                        remainingProfiles.push_back(p);
                                    }
                                }
                                // Renumber remaining profiles sequentially (1, 2, ..., N) to eliminate gaps
                                // that would otherwise cause InputSwitch.dll to crash explorer.exe!
                                renumberLocaleProfiles(localeKey, remainingProfiles);
                                ::RegCloseKey(localeKey);
                            }
                        }
                    }
                }
                ::RegCloseKey(userKey);
            }
        }
    }
    ::CoTaskMemFree(textServiceClsIdStr);

    // Unload the default user registry hive
    ::RegUnLoadKeyW(HKEY_USERS, defaultUserRegKey);
    return S_OK;
}

} // anonymous namespace

HRESULT ImeModule::registerLangProfiles(LangProfileInfo* langs, int langsCount) {
    // register the language profile
    ComPtr<ITfInputProcessorProfiles> inputProcessProfiles;
    if(CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&inputProcessProfiles) == S_OK) {
        for(int i = 0; i < langsCount; ++i) {
            LangProfileInfo& lang = langs[i];
            if(inputProcessProfiles->Register(textServiceClsid_) == S_OK) {
                LCID lcid = LocaleNameToLCID(lang.locale.c_str(), 0);
                if (lcid == 0 && !lang.fallbackLocale.empty()) { // the conversion fails
                    // The new RFC4646 locale names are not well-supported in Windows 7/Vista, so
                    // here we provide a fallback locale which uses the deprecated RFC 1766 format instead.
                    lcid = LocaleNameToLCID(lang.fallbackLocale.c_str(), 0);
                }
                if (lcid != 0) {
                    LANGID langId = LANGIDFROMLCID(lcid);
                    if (inputProcessProfiles->AddLanguageProfile(textServiceClsid_, langId, lang.profileGuid,
                        lang.name.c_str(), lang.name.length(), lang.iconFile.empty() ? NULL : lang.iconFile.c_str(),
                        lang.iconFile.length(), lang.iconIndex) != S_OK) {
                        return E_FAIL;
                    }
                    // Enable the profile by default, and also for the current user. Reinstalling
                    // does not clear a per-user "Enable = 0" left by an earlier removal, which hides
                    // the input method from the Win+Space switcher while Settings still lists it.
                    inputProcessProfiles->EnableLanguageProfileByDefault(textServiceClsid_, langId, lang.profileGuid, TRUE);
                    inputProcessProfiles->EnableLanguageProfile(textServiceClsid_, langId, lang.profileGuid, TRUE);
                }
                else {
                    return E_FAIL;
                }
            }
        }
    }

    // NOTE: For Windows newer than Windows 8, we have to manually write some settings
    //       to the registry so the input methods can appear in the Windows control panel.
    //
    //       Registry path: "HKEY_CURRENT_USER\Control Panel\International\User Profile\<locale_name>"
    //       Sub key: "<lang ID>:{text service GUID}{input module GUID}"
    //
    //       Unfortunately, this is not documented officially by Microsoft.
    //       We found the values with some registry monitor tools:
    //       These settings are user-specific so they should be written to HKEY_CURRENT_USER of all users.
#ifndef _WIN64  // only do this for the 32-bit version dll
    // The keys under HKCU\Control Panel\ is shared between the x86 and x64 versions and 
    // are not affected by WOW64 redirection. So doing this inside the 32-bit version is enough.
    registerUserProfiles(textServiceClsid_, langs, langsCount);
#endif  // #ifndef _WIN64
    return S_OK;
}

HRESULT ImeModule::registerServer(const wchar_t* imeName, LangProfileInfo* langs, int count) {
    wchar_t modulePath[MAX_PATH];
    DWORD len = GetModuleFileNameW(hInstance_, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return E_FAIL;
    }
    modulePath[len] = L'\0';
    return registerServer(imeName, langs, count, modulePath);
}

HRESULT ImeModule::registerServer(const wchar_t* imeName, LangProfileInfo* langs, int count, const wchar_t* dllPath) {
    if (!dllPath || !*dllPath) {
        return E_FAIL;
    }

    // write info of our COM text service component to the registry
    // path: HKEY_CLASS_ROOT\\CLSID\\{xxxx-xxxx-xxxx-xx....}
    // This reguires Administrator permimssion to write to the registery
    // regsvr32 should be run with Administrator
    // For 64 bit dll, it seems that we need to write the key to
    // a different path to make it coexist with 32 bit version:
    // HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Classes\CLSID\{xxx-xxx-...}
    // Reference: http://stackoverflow.com/questions/1105031/can-my-32-bit-and-64-bit-com-components-co-reside-on-the-same-machine

    HRESULT result = S_OK;

    wstring regPath = L"CLSID\\";
    LPOLESTR clsidStr = NULL;
    if(StringFromCLSID(textServiceClsid_, &clsidStr) != ERROR_SUCCESS)
        return E_FAIL;
    regPath += clsidStr;
    CoTaskMemFree(clsidStr);

    HKEY hkey = NULL;
    if(::RegCreateKeyExW(HKEY_CLASSES_ROOT, regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS) {
        // write name of our IME
        ::RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE*)imeName, sizeof(wchar_t) * (wcslen(imeName) + 1));

        HKEY inProcServer32Key;
        if(::RegCreateKeyExW(hkey, L"InprocServer32", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &inProcServer32Key, NULL) == ERROR_SUCCESS) {
            // store the path of our dll module in the registry
            ::RegSetValueExW(inProcServer32Key, NULL, 0, REG_SZ, (const BYTE*)dllPath, sizeof(wchar_t) * (wcslen(dllPath) + 1));
            // write threading model
            wchar_t apartmentStr[] = L"Apartment";
            ::RegSetValueExW(inProcServer32Key, L"ThreadingModel", 0, REG_SZ, (BYTE*)apartmentStr, 10 * sizeof(wchar_t));
            ::RegCloseKey(inProcServer32Key);
        }
        else
            result = E_FAIL;
        ::RegCloseKey(hkey);
    }
    else
        result = E_FAIL;

    // register language profiles
    if(result == S_OK) {
        result = registerLangProfiles(langs, count);
    }

    // register category
    if(result == S_OK) {
        ITfCategoryMgr *categoryMgr = NULL;
        if(CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&categoryMgr) == S_OK) {
            if(categoryMgr->RegisterCategory(textServiceClsid_, GUID_TFCAT_TIP_KEYBOARD, textServiceClsid_) != S_OK) {
                result = E_FAIL;
            }

            // register ourself as a display attribute provider
            // so later we can set change the look and feels of composition string.
            if(categoryMgr->RegisterCategory(textServiceClsid_, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, textServiceClsid_) != S_OK) {
                result = E_FAIL;
            }

            // enable UI less mode
            if(categoryMgr->RegisterCategory(textServiceClsid_, GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, textServiceClsid_) != S_OK ||
                categoryMgr->RegisterCategory(textServiceClsid_, GUID_TFCAT_TIPCAP_UIELEMENTENABLED, textServiceClsid_) != S_OK) {
                result  = E_FAIL;
            }

            if(::IsWindows8OrGreater()) {
                // for Windows 8 store app support
                // TODO: according to a exhaustive Google search, I found that
                // TF_IPP_CAPS_IMMERSIVESUPPORT is required to make the IME work with Windows 8.
                // http://social.msdn.microsoft.com/Forums/windowsapps/en-US/4c422cf1-ceb4-413b-8a7c-6881946a4c63/how-to-set-a-flag-indicating-tsf-components-compatibility
                // Quote from the page: "To indicate that your IME is compatible with Windows Store apps, call RegisterCategory with GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT."

                // declare supporting immersive mode
                if(categoryMgr->RegisterCategory(textServiceClsid_, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, textServiceClsid_) != S_OK) {
                    result = E_FAIL;
                }

                // declare compatibility with Windows 8 system tray
                if(categoryMgr->RegisterCategory(textServiceClsid_, GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, textServiceClsid_) != S_OK) {
                    result = E_FAIL;
                }
            }

            categoryMgr->Release();
        }
    }
    return result;
}

HRESULT ImeModule::unregisterServer() {
    // unregister the language profile
    ITfInputProcessorProfiles *inputProcessProfiles = NULL;
    if(CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&inputProcessProfiles) == S_OK) {
        inputProcessProfiles->Unregister(textServiceClsid_);
        inputProcessProfiles->Release();
    }

    // unregister categories
    ITfCategoryMgr *categoryMgr = NULL;
    if(CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&categoryMgr) == S_OK) {
        categoryMgr->UnregisterCategory(textServiceClsid_, GUID_TFCAT_TIP_KEYBOARD, textServiceClsid_);
        categoryMgr->UnregisterCategory(textServiceClsid_, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, textServiceClsid_);
        // UI less mode
        categoryMgr->UnregisterCategory(textServiceClsid_, GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, textServiceClsid_);

        if(::IsWindows8OrGreater()) {
            // Windows 8 support
            categoryMgr->UnregisterCategory(textServiceClsid_, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, textServiceClsid_);
            categoryMgr->UnregisterCategory(textServiceClsid_, GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, textServiceClsid_);
        }

        categoryMgr->Release();
    }

    // delete the registry key
    wstring regPath = L"CLSID\\";
    LPOLESTR clsidStr = NULL;
    if(StringFromCLSID(textServiceClsid_, &clsidStr) == ERROR_SUCCESS) {
        regPath += clsidStr;
        CoTaskMemFree(clsidStr);
        ::SHDeleteKey(HKEY_CLASSES_ROOT, regPath.c_str());
    }

#ifndef _WIN64  // only do this for the 32-bit version dll
    // The keys under HKCU\Control Panel\ is shared between the x86 and x64 versions and 
    // are not affected by WOW64 redirection. So doing this inside the 32-bit version is enough.
    unregisterUserProfiles(textServiceClsid_);
#endif  // #ifndef _WIN64
    return S_OK;
}


// display attributes stuff
bool ImeModule::registerDisplayAttributeInfos() {

    // register display attributes
    ComPtr<ITfCategoryMgr> categoryMgr;
    if(::CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&categoryMgr) == S_OK) {
        for (auto& info : displayAttrInfos_) {
            TfGuidAtom atom;
            categoryMgr->RegisterGUID(info->guid(), &atom);
            info->setAtom(atom);
        }
        // categoryMgr->RegisterGUID(g_convertedDisplayAttributeGuid, &atom);
        // convertedAttrib_->setAtom(atom);
        return true;
    }
    return false;
}


// virtual
bool ImeModule::onConfigure(HWND hwndParent, LANGID langid, REFGUID rguidProfile) {
    return true;
}


// COM related stuff

// IUnknown
STDMETHODIMP_(ULONG) ImeModule::AddRef(void) {
    // In the examples of MS TSF, a critical section is used to protect the ref count
    // of the Dll but the document didn't explain the reason. Since in our design
    // the life-cycle of the ImeModule object and the Dll are tied together,
    // we protect the ref count of ImeModule with a mutex.
    std::lock_guard <std::mutex> lock{refCountMutex_};
    return ComObject::AddRef();
}

STDMETHODIMP_(ULONG) ImeModule::Release(void) {
    // In the examples of MS TSF, a critical section is used to protect the ref count
    // of the Dll but the document didn't explain the reason. Since in our design
    // the life-cycle of the ImeModule object and the Dll are tied together,
    // we protect the ref count of ImeModule with a mutex.
    std::lock_guard <std::mutex> lock{ refCountMutex_ };
    return ComObject::Release();
}

// IClassFactory
STDMETHODIMP ImeModule::CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppvObj) {
    *ppvObj = NULL;
    if(::IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
        auto provider = ComPtr<DisplayAttributeProvider>::make(this);
        if(provider) {
            provider->QueryInterface(riid, ppvObj);
        }
    }
    else if(::IsEqualIID(riid, IID_ITfFnConfigure)) {
        // ourselves implement this interface.
        this->QueryInterface(riid, ppvObj);
    }
    else {
        TextService* service = createTextService();
        if(service) {
            service->QueryInterface(riid, ppvObj);
            service->Release();
        }
    }
    return *ppvObj ? S_OK : E_NOINTERFACE;
}

STDMETHODIMP ImeModule::LockServer(BOOL fLock) {
    if (fLock) {
        AddRef();
    }
    else {
        Release();
    }
    return S_OK;
}

// ITfFnConfigure
STDMETHODIMP ImeModule::Show(HWND hwndParent, LANGID langid, REFGUID rguidProfile) {
    return onConfigure(hwndParent, langid, rguidProfile) ? S_OK : E_FAIL;
}

// ITfFunction
STDMETHODIMP ImeModule::GetDisplayName(BSTR *pbstrName) {
    *pbstrName = ::SysAllocString(L"Configuration");
    return S_OK;
}

} // namespace Ime
