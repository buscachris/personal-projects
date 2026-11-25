# World Install Notes

1. Import `Assets/PatrioticWand` into a VRChat world project.
2. Run `Tools > Patriotic Wand > Create World Pickup Prefab`.
3. Place `Assets/PatrioticWand/Generated/Patriotic_Wand_World_Pickup.prefab` in the scene.
4. Confirm it has:
   - `Rigidbody`
   - `BoxCollider`
   - `VRCPickup` if the VRChat SDK was present during generation
5. If `VRCPickup` was not added automatically, add it manually.
6. Build/Test from the VRChat SDK.

For a simple personal test room, run `Tools > Patriotic Wand > Create Test World Scene`, then add the normal VRChat world descriptor/spawn setup required by your SDK version.

Notes:

- This is the closest creator-supported behavior to a spawned Item, but it only exists in the uploaded world.
- Pre-place or pool multiple wand instances. Do not depend on arbitrary runtime prefab spawning.
- Udon scripts should avoid trying to inspect official platform Items directly; VRChat documents those as protected from Udon references.
