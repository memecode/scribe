# Windows Registry Work — Current State

## Objective

Modernize Scribe’s Windows email-registry support to follow current standards:

- per-user vendor-qualified ProgIDs,
- HKCU-first lookup,
- machine-wide entries only best-effort when elevated,
- no attempt to write protected `UserChoice`,
- safe cleanup of Scribe-owned registry entries,
- installer uninstall behavior matching the current registry layout,
- help documentation consistent with the current registry and Settings behavior,
- consistency between the C++, NSIS, Python diagnostic, and help documentation.

## Active Files

- `/home/matthew/code/scribe/trunk_os/src/ScribeAppAccounts.cpp`
  - `DefaultClient`
  - `ReportKey()`
  - `CleanRegistry()`
  - `WinInstall()`
  - `WinUninstall()`
  - `InstallClasses()`
  - `InstallMailClient()`
  - `CheckKey()`
  - `CheckString()`
  - `CheckInt()`
  - `DeleteKeyTree()`
  - `SetDefault()`
  - `SetDefaultHandler()`
  - `OpenDefaultAppSettings()`
- `/home/matthew/code/scribe/trunk_os/win/Installer-64bit.nsi.in`
- `/home/matthew/code/scribe/trunk_os/win/print_registry_keys.py`
- `/home/matthew/code/scribe/trunk_os/help/index.html`
- `/home/matthew/code/scribe/trunk_os/help/menu.html`
- `/home/matthew/code/scribe/trunk_os/help/install.html`
- `/home/matthew/code/scribe/trunk_os/help/filters.html`
- `/home/matthew/code/scribe/trunk_os/help/import.html`
- `/home/matthew/code/scribe/trunk_os/help/plugins.html`
- `/home/matthew/code/scribe/trunk_os/help/email.html`
- `/home/matthew/code/scribe/trunk_os/help/calendar.html`
- `/home/matthew/code/scribe/trunk_os/help/contacts.html`
- `/home/matthew/code/scribe/trunk_os/help/scripting.html`
- `/home/matthew/code/scribe/trunk_os/src/ImpExp_Outlook.cpp`
- `/home/matthew/code/scribe/trunk_os/mapi/Code/ScribeMapi.cpp`
- `/home/matthew/code/scribe/trunk_os/CMakeLists.txt`

Inactive older NSIS copy, not the active template:

- `/home/matthew/code/scribe/trunk_os/win/Installer-64bit.nsi`

## Current Design

### Registry layout

- Scribe ProgIDs are vendor-qualified:
  - `Memecode.Scribe.Email`
  - `Memecode.Scribe.Folder`
  - `Memecode.Scribe.Calendar`
  - `Memecode.Scribe.Contact`
  - `Memecode.Scribe.MailStore`
  - `Memecode.Scribe.Mailto`
- `HKCU` is the primary writable location.
- `HKLM` is attempted best-effort.
- `HKCR` is only treated as a merged view in comments or diagnostics.
- The `mailto` ProgID is special:
  - `URL Protocol` belongs on the ProgID key.
  - The extension mapping key for `mailto` maps to `Memecode.Scribe.Mailto`.
  - The protected `UserChoice` key is read-only and owned by Windows.

### Important constants in `src/ScribeAppAccounts.cpp`

```text
sUserHive   = "HKCU"
sSystemHive = "HKLM"
sProgIdPrefix = "Memecode.Scribe"
sMailtoProgId = "Memecode.Scribe.Mailto"
sUserChoice = "HKCU\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\mailto\UserChoice"
```

`CMakeLists.txt` confirms C++17, so `static constexpr const char *` is acceptable.

### Default client behavior

- `DefaultClient::IsDefault()` reads the current handler:
  - first from `UserChoice`,
  - then from `Software\Clients\Mail` in HKCU,
  - then HKLM.
- `SetDefault()` writes only the Simple MAPI `Software\Clients\Mail` default value.
- `OpenDefaultAppSettings()` opens `ms-settings:defaultapps`.
- `SetDefaultHandler()` installs the registry entries and then uses Settings for the actual app association.

## Current Cleanup State

### C++ cleanup model

`DefaultClient::Action` now contains only:

```text
removeKey
removeValue
removeMapping
readOnly
```

`checkValue` was removed because it is not used by `CleanRegistry()` and its old guard wording was awkward for a check action.

Current `CleanRegistry()` fixed pairs:

- `Software\Clients\Mail`
  - default value
  - `removeValue`
- `Software\Clients\Mail\Scribe`
  - key tree
  - `removeKey`
- `Software\RegisteredApplications`
  - value `AppName`
  - `removeValue`
- `Software\Microsoft\Windows\Shell\Associations\UrlAssociations\mailto\UserChoice`
  - value `ProgId`
  - `readOnly`

Current ProgID and mapping behavior:

- Scribe ProgIDs are removed as trees.
- Extension mappings are removed only when the default value points at a Scribe ProgID.
- `mailto` is included as a protocol mapping, not as an ordinary file-extension association.
- `CleanRegistry()` checks HKCU before HKLM.

### `ReportKey()` current behavior

- Prints one HTML table row per checked entry.
- The key cell is printed, then the value cell, then the status cell.
- HTML rows are closed consistently with `</td></tr>`.
- `readOnly` prints the value but does not delete.
- Missing or empty values are skipped only for guarded actions.
- Guarded value checks use:
  - `sProgIdPrefix` for `removeMapping`
  - `AppName` for `removeValue`
- `nothing to remove` is only used for `removeMapping` or `removeValue`.
- `removeKey` is unconditional for Scribe-owned keys because those keys are Scribe-owned.
- Successful deletion prints `removed`.
- Failed deletion or read-only result prints `WrapResult(rw)`.
- Write probe only writes when there is an existing value.
- Deletion is attempted only when the write-opened key is valid.

### `DeleteKeyTree()` current behavior

```text
if (!k.IsOk() || !k.Name())
    return false;

if child name is null:
    continue

recursive delete children,
then delete parent key
```

This prevents null-name dereference and skips invalid child keys.

`DeleteKeyTree()` still reports parent-row only in `ReportKey()`. Printing recursive child rows remains a pending design choice.

### Check helpers

- `CheckKey()` now has a null guard before `va_start()`:
  - `if (!Key) return NULL;`
- `CheckInt()` now guards with:
  - `if (!k || !k->IsOk())`
- `CheckString()` now guards with:
  - `if (!k || !k->IsOk())`
- `CheckInt()` failure trace uses `%u` for `uint32_t`.

## Python Diagnostic Script

File: `/home/matthew/code/scribe/trunk_os/win/print_registry_keys.py`

Current behavior:

- Lists vendor-qualified ProgIDs matching `src/ScribeAppAccounts.cpp`.
- Shows:
  - `Software\Clients\Mail`
  - `Software\Clients\Mail\Scribe`
  - `DllPath`
  - `DefaultIcon`
  - shell command
  - capabilities
  - file associations
  - URL associations
  - `RegisteredApplications`
  - per-user and machine ProgIDs
  - `Software\Classes\mailto`
  - `URL Protocol` on the Scribe mailto ProgID
  - protected `UserChoice` `ProgId`
- Handles default values with:
  - `winreg.QueryValueEx(k, name if name is not None else "")`
- `mailto` is excluded from the ordinary file-extension loop.
- `showKey()` now uses `except OSError:` instead of bare `except:`.

Verification status:

- Static review completed.
- `python3 -m py_compile win/print_registry_keys.py` succeeded via background process.
- Runtime registry access cannot be exercised because `winreg` is Windows-specific.
- Direct `code_exec` remains disallowed, but background process execution is available.

## NSIS Uninstall State

File: `/home/matthew/code/scribe/trunk_os/win/Installer-64bit.nsi.in`

Current behavior:

- Uses `SetRegView 64`.
- Product-based uninstall key:
  - `Software\Microsoft\Windows\CurrentVersion\Uninstall\Memecode_Scribe`
- Keeps intentional legacy uninstall key:
  - `Software\Microsoft\Windows\CurrentVersion\Uninstall\Scribe_v2.4`
- Uses unquoted macro arguments for `!insertmacro`.
- Explicit registry paths inside uninstall sections are quoted, including:
  - `DeleteRegKey HKCU "SOFTWARE\Memecode\Scribe"`

Macros:

- `CleanAssoc base prog ext`
  - deletes the extension mapping only when the default value matches the Scribe ProgID.
- `CleanProg base prog`
  - deletes known ProgID values.
  - deletes `shell\open\command`, `shell\open`, `shell`.
  - deletes `DefaultIcon`.
  - deletes the ProgID key.
- `CleanClient base`
  - deletes known client values.
  - deletes shell command subkeys.
  - deletes `DefaultIcon`.
  - deletes `Capabilities`, `FileAssociations`, `UrlAssociations`.
  - deletes the Scribe client key.
- `CleanMailDefault base`
  - deletes the `Software\Clients\Mail` default value only when it is exactly `Scribe`.

Verification status:

- Manual syntax review completed.
- `makensis` is not installed locally.
- Full NSIS compilation cannot be verified in this environment.

## Import/Export State

File: `/home/matthew/code/scribe/trunk_os/src/ImpExp_Outlook.cpp`

Current relevant behavior:

- Windows resolves mail clients per-user first, so both trees are checked in order:
  - `HKEY_CURRENT_USER\SOFTWARE\Clients\Mail`
  - `HKEY_LOCAL_MACHINE\SOFTWARE\Clients\Mail`
- `InitSrcClients()` uses these as the base lookup order.
- `List<char> Clients` owns copied strings with `NewStr()`.
- Destructor calls `Clients.DeleteArrays()` and `DeleteArray(DefClient)`.
- The stray closing-brace block previously after `InitSrcClients()` was removed.
- `InitSrcClients()` now ends cleanly before `void AddPath(char *p)`.

## Documentation State

Current help documentation:

- `help/index.html`
  - External links were normalized from `http://` to `https://`.
- `help/menu.html`
  - Homepage link normalized to `https://`.
- `help/install.html`
  - Windows default-client text says accepting the prompt sets the Simple MAPI default and opens Settings → Apps → Default apps.
  - Uninstall section says Scribe removes:
    - `HKEY_CURRENT_USER\SOFTWARE\Memecode\Scribe`
    - `Software\Clients\Mail` default value when it points to Scribe
    - `Software\Clients\Mail\Scribe`
    - `RegisteredApplications` value `Scribe`
    - Scribe ProgIDs
    - extension mappings that still point at Scribe ProgIDs
  - Documentation says it cannot remove `UserChoice`.
  - External links were normalized to `https://`.
- `help/filters.html`
  - External links were normalized from `http://` to `https://`.
- `help/import.html`
  - External links were normalized from `http://` to `https://`.
- `help/plugins.html`
  - External links were normalized from `http://` to `https://`.
- `help/email.html`
  - External links were normalized from `http://` to `https://`.
- `help/calendar.html`
  - External links were normalized from `http://` to `https://`.
- `help/contacts.html`
  - External links were normalized from `http://` to `https://`.
- `help/scripting.html`
  - External links were normalized from `http://` to `https://`.

`help/install.html` and `help/index.html` remain the key documents for uninstall behavior.

## Completed This Pass

- Removed `checkValue` from `DefaultClient::Action`.
- Removed the old `checkValue` guard behavior from `ReportKey()`.
- Updated `ReportKey()` to close HTML rows consistently.
- Made successful deletion status explicit in `ReportKey()`.
- Added `CheckKey()` null guard before `va_start()`.
- Strictened `CheckInt()` guard with `k->IsOk()`.
- Strictened `CheckString()` guard with `k->IsOk()`.
- Changed `CheckInt()` trace format from `%i` to `%u`.
- Fixed stray closing braces in `src/ImpExp_Outlook.cpp`.
- Quoted HKCU uninstall key in NSIS:
  - `DeleteRegKey HKCU "SOFTWARE\Memecode\Scribe"`
- Changed Python `showKey()` bare `except:` to `except OSError:`.
- Verified Python syntax with:
  - `python3 -m py_compile win/print_registry_keys.py`
- Normalized help HTML external links from `http://` to `https://`.

## Not Yet Verified

- Cannot compile Windows-specific `#if WINNATIVE` code on Linux.
- Cannot compile the full project because it depends on Windows-only registry and UI code.
- Cannot run `makensis`.
- Cannot execute Python against Windows registry from this workspace.
- `python3 -m py_compile` can verify Python syntax, but cannot exercise `winreg` behavior on Linux.

## Pending Choices

1. Decide whether `DeleteKeyTree()` should remain parent-row-only or print recursive child rows in `ReportKey()`.
2. Decide whether a stricter cleanup report should show child-row counts or per-child status.
3. Re-run full NSIS compilation on Windows when available.
4. Re-run Windows registry behavior tests on a Windows machine when available.
5. Review whether `resources/Scribe.lr8` default-client prompt text should be changed without breaking translations; currently intentionally skipped.

## Intentionally Not Changed

- `resources/Scribe.lr8`
  - Default-client prompt text remains as-is because changing it requires many translations.
- Protected `UserChoice`
  - Scribe must not write it; Windows owns and validates it.
