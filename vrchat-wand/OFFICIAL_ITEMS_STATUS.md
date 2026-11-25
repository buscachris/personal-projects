# Official VRChat Items Status

Checked on 2026-07-07.

VRChat's official Items documentation confirms:

- Users can spawn Items in worlds when Items are enabled.
- Items use the `Item` layer.
- World creators can disable Items for a world and update mirrors/layers to handle them.
- Udon world scripts cannot directly reference Items; protected Items appear invalid/null to world Udon logic.

What I did not find in the official creator docs:

- A public SDK workflow for uploading an arbitrary custom Unity prefab as a personal/global VRChat Item.
- A public custom Item marketplace/upload pipeline equivalent to avatar or world uploads.

Practical result:

- Avatar prop: buildable now.
- World pickup: buildable now.
- Personal test world: buildable now.
- True global custom Item: blocked until VRChat exposes an official creator path.

Reference:

- https://creators.vrchat.com/worlds/items/
