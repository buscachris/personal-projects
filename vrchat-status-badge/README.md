# VRChat Overhead Status Badge

I built an overhead status badge for a VRChat avatar. I use a synced expression parameter to switch the floating label between states such as Busy, AFK, and Recording.

It uses VRChat-supported avatar systems:

- A synced `Int` expression parameter named `StatusMode`
- An expressions submenu named `Status`
- An FX animator layer that enables one status object at a time
- Optional OSC control from your PC

It does not rely on arbitrary runtime C# avatar scripts.

## Status Values

| Value | Status |
| ---: | --- |
| 0 | Hidden |
| 1 | Available |
| 2 | Busy |
| 3 | AFK |
| 4 | Recording |
| 5 | Coding |

## Unity Setup

Option A, install as a local Unity package:

```bash
python tools/install_into_unity_project.py /path/to/YourUnityAvatarProject
```

Option B, copy assets manually:

```bash
cp -r Assets/OverheadStatusBadge /path/to/YourUnityAvatarProject/Assets/
```

Then:

1. Open the avatar scene.
2. Select the avatar root object that has `VRC Avatar Descriptor`.
3. Run `Tools > Overhead Status Badge > Build On Selected Avatar`.
4. Upload the avatar normally through the VRChat SDK.

The builder creates:

- `Assets/OverheadStatusBadge/Generated/OverheadStatus_FX.controller`
- `Assets/OverheadStatusBadge/Generated/OverheadStatus_StatusSubmenu.asset`
- `Assets/OverheadStatusBadge/Generated/OverheadStatus_Parameters.asset`
- A `OverheadStatusBadge` object parented to the avatar head bone

If your avatar already has an FX controller, expression menu, or expression parameters, the builder merges into them.

## OSC Control

VRChat must have OSC enabled.

Set a status:

```bash
python tools/status_osc.py busy
python tools/status_osc.py coding
python tools/status_osc.py hidden
```

Keep it live while VRChat is running:

```bash
python tools/status_live.py available
```

Then change it from another terminal:

```bash
python tools/status_set.py busy
python tools/status_set.py coding
python tools/status_set.py hidden
```

`status_live.py` resends the current status every second, which helps when you
swap into another avatar that also has this package installed.

The OSC script sends:

```text
/avatar/parameters/StatusMode
```
