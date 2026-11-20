Scriptname WastelandWagersNative Native Hidden
{WastelandWagers: the F4SE plugin's own functions, for the quest's script}

; a slot machine sitter's hand on the lever (their pull animation's furnitureIdleFlavor1Slave): the lever goes down and
; the reels spin from then
Function LeverDown(Int aiActor) Global Native

; you sat at a slot machine: the furniture's own idle about to pick a flavour (IdleFlavor: the end of the sit-down, and
; now and then in its sitting loop), most of them a pull of the lever: you're held still instead
Function FlavorStarts(Int aiActor) Global Native
