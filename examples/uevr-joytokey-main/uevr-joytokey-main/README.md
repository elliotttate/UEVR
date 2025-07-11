# uevr-joytokey
Controller to keypress mapper for UEVR. Maps mouse pointer, mouse buttons, and keyboard presses for games that are mouse and keyboard only to the controllers.

See sample config file joytokey.txt included with the dll.
These go in the game's plugin folder together. Edit the joytokey.txt for your game.

All keys are use the hex code starting with 0x in the windows virtual key list below.
Letters A-Z and 0-9 can be used in place of the hex codes. All others must be the hex code.
Hex must start with 0x. Not 0X, nothing else except 0x

The list is here: 
https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes

For example, to set to A, you can use A or 0x41. To set to 5, use 5 or 0x35. To set to DEL, use 0x2E
The single keys can only be used for A-Z, 0-9. All other keys must use the hex code from the link above.
Some common key codes are:
  Backspace: 	0x08
  Tab: 	0x09
  Enter:	0x0D
  Escape: 	0x1B
  Space:	0x20
  Ins:	0x2D
  Del:	0x2E
  Arrows: (left, up, right, down) 0x25, 0x26, 0x27, 0x28
  Numpad 0 - Numpad 9: 0x60 - 0x69
  F1 - F10:  0x70 - 0x79

Leaving a button unassigned or setting to 0 disables it.

Mouse
You can set mouse clicks using the same link above:
  Left Button: 	0x01
  Right Button: 	0x02
  Middle Button:	0x04
  XButton1: 		0x05
  XButton2: 		0x06

If left unassigned, the right stick maps to the mouse pointer. There is currently
no sensitivity adjustments for this. You can assign the sticks to buttons or directly
to 0 to disable the mouse. Leaving it empty defaults to right stick moving mouse.

Shift Key
GAMEPAD_LB can be shift so leave it unassigned if you wish to use it as such.
This will allow for a second set of keys while holding shift
