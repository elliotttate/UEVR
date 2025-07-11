## 6dof Attached Items Profile(Right hand Aiming), FOR [NIGHTLY 1046](https://github.com/praydog/UEVR-nightly/releases/download/nightly-01046-5d12735a10c146ea7ff73ac2c37bdd529bc1214d/uevr.zip)!! 
### Install:
1. get  NIGHTLY branch
2. Download [VR ESP files](https://github.com/Pande4360/OblivionVR/raw/refs/heads/main/ESP/VRESPs.zip)
3. Drop ESP into Oblivion Remastered\Content\OblivionRemastered\Content\Dev\ObvData\Data
4. Update the plugin.txt with the new esp names
5. On first game load collisions are being reset and you may float a bit, just wait around 10s. If you still have terrain collision issues hit "ResetScripts" in lua menu
6. Done
### Features:
- Physical BLocking with weapons
- Hands
- Holster System: 
  - RHand: QuickSlot1 - RShoulder, Quickslot2 - Rhip, Quickslot 3 - RChest, 
  - LHand: QuickSlot8 - RShoulder(e.g. Torch), QuickSlot 7- LeftShoulder(bow), QuickSLot6 - LHip, QuickSlot 4 -Chest Right, QuickSlot 5- Chest Left
- Physical swinging for attacks (Collision based detection of dmg)
- Attached melee, shield, bows and torch for now
- Some improved VR control layout: Jumping and Crouching on right stick and sprinting swapped with weapon wheel
- Arrow will shoot into the direction it points
### Controls: 
Holsters:
- RHand: QuickSlot1 - RShoulder, Quickslot2 - Rhip, Quickslot 3 - RChest, 
- LHand: QuickSlot8 - RShoulder(e.g. Torch), QuickSlot 7- LeftShoulder(bow), QuickSLot6 - LHip, QuickSlot 4 -Chest Right, QuickSlot 5- Chest Left
Other Binding changes:
- Jumping and Crouching on right stick
- Sprinting left Grip
- Weapon Wheel: Set to LeftGrip+Y Button
- RShoulder: nothing, X Button: Magic, B Button: Un/sheath weapon
Gestures:
- Weapon and shields have collision now, 
- When hitting soemthing with enough force will damage them, Depending on your force it will be either a light or heavy attack
- Blocking: Parry attacks with shield or weapon based on collision
### **Keysor´s Userscript**:
optional download, put it into the profile´s root folder, if you have perf issues, click on "open global dir" and drop it into the oblivion folder
CheckDiscord for file
### VR controller ICONs by M4l4 : Check Discord for file
### Changes
1.03: 
- Added Holster system
1.04b: 
- increased treshhold for melee attack to trigger ( you can edit this value at the very top of meleescript)
- added "SkyrimVR Blocking Mechanics"
- there can be collision based blocking occuring but it´s not yet reliable
- quiver moved out of sight.
- Hotfix: added check if shield is equipped
1.05: 
- Added VR Bow Shooting
1.06: 
- Switching between bow and other weapons doenst rotate away
- Improved collision based blocking with weapons:
  - face weapon to enemy weapon and it should trigger a block action. To successfully block you have to look at the enemy.
  - note that due to the high weapon ranges. I also increased the blocking collision range.
- resized UI
- hid Scabbard
1.08:
- Full Physical Combat
- Quickslot Wheel when left Grip+Y
- Config UI inside UEVR(Lua UI Menu)
- Some collision workarounds for stairs and obstacles
- When using UI Follow View pressing A will temporarely switch to head aiming  
1.09:
- new UI : UI follows view+ new reticle follows hand (new recommended modlist)
- hand to hand combat (thx to Teddybear082 for help)
- new Melee Power calculation method 
- new collision reset method at start
- Control Layout changes: RShoulder: nothing, X Button: Magic, B Button: Un/sheath weapon
1.10:
- Menus more coherent behaviour
- update reticle on Menu(should it disappera)
- jump fix when quicwheel
- UI follows view LuaUI Checkbox now working.
1.12: 
- new Addon Darker Darks
1.13: 
- new Reticle method
1.15: 
- Bow rotations fixed
- Right Hand invis when close to face
1.16: 
- added visible Body