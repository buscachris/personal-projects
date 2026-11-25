# Avatar Install Notes

1. Import `Assets/PatrioticWand`.
2. Run `Tools > Patriotic Wand > Create Avatar Prop Prefab`, or `Create Quest Safe Avatar Prop Prefab` for a stricter no-light version.
3. Drag `Assets/PatrioticWand/Generated/Patriotic_Wand_Avatar_Prop.prefab` under the avatar's hand bone.
4. Adjust local position/rotation in the Scene view.
5. Make an avatar toggle using a bool parameter, for example `PatrioticWand`.
6. Use an FX layer animation to set the wand GameObject active/inactive.
7. Add the toggle to the avatar Expressions Menu.
8. Build and upload with the VRChat SDK.

Notes:

- Do not add the world-pickup prefab to an avatar; use the avatar-prop prefab.
- For Quest/mobile, prefer `Patriotic_Wand_Avatar_Prop_NoLight.prefab`.
- The trail is baked onto the `TrailRenderer`; it does not require a runtime custom script.
