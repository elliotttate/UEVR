Most things are explained within the lua file.

IF you want to be able to press keyboard keys you also need markmon´s luakey.dll from his repository or ask him on discord.

Basic config settings in at the start, self explanatory. 

The script is build in this structure:

I. Creating hand and hmd component for tracking, works by itself dont need to edit.
II.XINPUT callback, most of the stuff there can be left alone, you can add your stuff at the end of this
III: PreTick callback, 
  a) Math of components from I., 
  b) Defining of Zoneparameters, you may edit this
  3) Defining what each zone does, you can edit this to trigger events.

Actions:
You can use basic game functions, XINPUT calls or with markmons plugin also trigger keyboard keys

1. A game funciton could look like this: 

      pawn:Reload()

2. A XINPUT CALL is probably best handled in the Xinput secition so you can forward a variable from the Zone section to the Xinput section, e.g.

      if RZone== 1 and rGrabActive then
   
        isButtonY=true
     
      end
   

Now add this to your XINPUT callback:

      if isButtonY then
  
      pressButton(state, XINPUT_GAMEPAD_Y)    
      
      end

3. A Keyboard press can be handeled in many ways, e.g. you create a variable here and trigger the action somewhere else doesnt really matter and is just a question to keep the script readable
 
    if RZone== 1 and rGrabActive then
  
        isKey1=true
      
        SendKeyDown('1') 
      
    end

  More importantly you need to Unpress the key somewhere BEFORE that. the next time the callback is done it unpresses the key if you let go of grip
  So this part is ALWAYS BEFORE the zone definition aboev. It doesnt matter where you call it could be right before it:

    if isKey1==true then
  
      iskey1=false
    
      SendKeyUp('1')
    
    end

  The final Result may look like this:
  
    if isKey1==true then
  
      iskey1=false
    
      SendKeyUp('1')
    
    end
  
    if RZone== 1 and rGrabActive then
  
        isKey1=true
      
        SendKeyDown('1') 
      
    end
