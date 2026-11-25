# Patriotic Wand Trail for VRChat

This is a reusable Unity source package for a red-white-blue wand trail.

The generated wand uses built-in Unity and VRChat components only. The package's C# is editor-only generation code, so the uploaded wand does not depend on a custom runtime `MonoBehaviour`.

Current VRChat limitation: this cannot become a true global VRChat Item that you can equip with every avatar in every world unless VRChat exposes custom Item publishing for creators. Today, the practical installs are:

- World pickup: import into a VRChat world project, run the prefab generator, place the prefab in the scene, and upload the world.
- Avatar prop: import into an avatar project, generate the prefab, parent it under a hand bone, and toggle it through an Expressions Menu.

## Use

1. Copy `Assets/PatrioticWand` into a Unity VRChat project.
2. Open Unity.
3. Use one of these menu commands:
   - `Tools > Patriotic Wand > Create Avatar Prop Prefab`
   - `Tools > Patriotic Wand > Create Quest Safe Avatar Prop Prefab`
   - `Tools > Patriotic Wand > Create World Pickup Prefab`
   - `Tools > Patriotic Wand > Create Test World Scene`
4. Use the generated files in `Assets/PatrioticWand/Generated`.

From this machine, you can copy it into a project with:

```bash
./install_into_unity_project.sh /path/to/UnityProject
```

If the VRChat SDK3 is present, the generator tries to add `VRCPickup` automatically for world use. If it cannot find the SDK, add a `VRCPickup` component manually in a VRChat world project.

For the closest "works like Items" behavior, put the generated prefab into a small personal world or avatar-testing world as a pickup. For "any avatar" across arbitrary worlds, VRChat's platform Items system is the blocker, not the wand asset itself.

## Track 1: Avatar prop

Generate `Patriotic_Wand_Avatar_Prop.prefab`, parent it under the avatar's hand bone, and drive the prefab active state from an Expressions Menu toggle.

This works in normal VRChat worlds after you upload that avatar. It does not transfer to avatars you do not control.

For Quest/mobile compatibility, use `Create Quest Safe Avatar Prop Prefab`; it omits the point light and keeps only the wand mesh/trail setup.

## Track 2: World pickup

Generate `Patriotic_Wand_World_Pickup.prefab`, place it in a VRChat world scene, and upload the world.

This behaves like a normal grabbable object inside that world. If the SDK is present, the generator attempts to add `VRCPickup`; otherwise add it manually.

If the world needs more than one wand, duplicate the prefab instances in the scene instead of runtime-instantiating them.

## Track 3: Personal test world

Generate `Patriotic_Wand_Test_World.unity`. This is a minimal Unity test scene with a floor, light, camera, and wand. In a real VRChat world project, add the normal VRChat world descriptor/spawn prefab before building and testing.

## Track 4: Official custom Items

The official Items system is the only path that would make this behave like a global VRChat Item across avatars and normal worlds. As of the official docs checked on 2026-07-07, the public creator docs describe Items in worlds and world compatibility settings, but do not show a normal SDK workflow for uploading arbitrary custom Items into the global Item catalog.
