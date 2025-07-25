## Installation and configuration
Inject and play.
### Mods
Install the [Removes Black Layer Mod](https://www.nexusmods.com/readyornot/mods/4229)
Also recommended: [M320 Sights flipped up for better aiming](https://www.nexusmods.com/readyornot/mods/3605)
### In Game settings
- Go to settings and disable ADS ZOOM, or else your weapons will be warping when using ads button(left trigger)
- Abnormal screen ratios can also cause issues, Im using 16:9
- To ensure less glitches at every start of round you should open up tablet once and cylce throgh all guns including the ballistic shield or grenade launcher
- General info concerning body parts not invisbile: USE STANDARD SKIN ONLY, dont wear any extras:
"LSPD long sleeve shirt", "LSPD Tactical Pants", Assault gloves, no styles
## Holsters
- You can use grip to trigger actions from shoulders, hips, chest l/R, Helmet, center lower back
## Troubleshoot
- Body parts visible: Make sure to wear the default clothing as described on Nexus
- Black window: Black layers mod not installed.
- Stretching weapons: Ingame: FOV settings (90 FOV and 16:9 aspect ratio works for me) and disable ADS ZOOMING
- Controls are weird when playing in Multiplayer: go to uevr menu (with l3+r3) and enable "multiplayer support" in Input menu
- Only one eye renders: Dont use SteamVR with Virtual Desktop
## Alternative Gamepad Config
- Crouch(right stick down), Walk when left trigger, Left Ear LTrigger is VOIP
- Download my input ini from [optional files](https://www.nexusmods.com/readyornot/mods/3612?tab=files) and put the ini into AppData\Local\ReadyOrNot\Saved\Config\Windows
## LEFT HANDE MODE: By default Trigger Only is swapped, you can set it to swap all controls in the PhysicalHolsterLeaning.lua inside
\AppData\Roaming\UnrealVRMod\ReadyOrNot-Win64-Shipping\scripts, just set isLeftHandModeTriggerSwitchOnly to false
## PHYSICAL LEANING SYSTEM:
- By default character will adapt to your standing height, to be safe you can hold both Grip buttons for 5s to reset height.
  (you can only check this by equipping a greande for example, grenade should be slightly below your hmd height.)
- You can further freelean to which ever position by holding LeftTrigger. If you let go of the trigger the character will go back to not leaning mode.
  Means if you lean into a door and let go of trigger you will be standing in the door openly.
- Leaning works while moving.
## Changelog:
- 1.16: Added correct Recoil Angle Source and Recovery Time
- 1.17: Improved Recoil effects, e.g. Pistol local rotation recoil added per shot,
	Added positional recoil to the back, seperated scripts for customization
- 1.18: added virtual Holster system
- 1.19: DPAD: Changed to LHand to HEAD mode (you can change it back if you want to)
	Disable Command Menu when hovering on Holsterslots
	Added leftHandGrip to LHip: Taser
	Added Weapon interaction zones for Reloading, attachments, Firemode switch
	LHandLocation                                 Action
	Below gun + LGRIP = RELOADING or Mag Instpection
	slightly above RHand + LTrigger = Firemode switch
	slightly above RHand and in front + LThumbstickButton = Attachment on/off
- 1.19b: fixed 1.19 script issue
- 1.20: added Tablet to LGrip Center Back, added Low ready on Rstick UP, added Lhanded mode Trigger swap only, added melee to both hands.
- 1.21: Fixed Recoil SCript not activating on level change
- 1.22: Physical Leaning System