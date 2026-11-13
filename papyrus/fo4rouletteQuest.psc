Scriptname fo4rouletteQuest extends Quest
{fo4roulette: NPCs at a roulette table. The Dealers and Bettors collections both carry fo4rouletteWalkToSpot
(walk to their linked ref, keyword fo4rouletteLinkSpot, and face it), which outranks the sandbox packages settlers
usually have. The F4SE plugin places a marker where each should stand and calls Join, Walk, Arrived and Leave.
Actors, refs and keywords come in as form IDs, not objects: an NPC with other scripts on them can come through from
the plugin as an object Papyrus won't take as an Actor.}

RefCollectionAlias Property Dealers Auto Const Mandatory
RefCollectionAlias Property Bettors Auto Const Mandatory
RefCollectionAlias Property Sitters Auto Const Mandatory  ; (poker players in their chairs: fo4rouletteSitInSeat)

RefCollectionAlias Function Collection(Bool abDealer)
	if abDealer
		return Dealers
	endif
	return Bettors
EndFunction

Function StartAndWait()
	Start()
	Int tries = 0
	while IsStarting() && tries < 50
		Utility.Wait(0.1)
		tries += 1
	endwhile
EndFunction

; into the dealers (abDealer) or the bettors, linked to aiTarget: their package walks them there. "in ..." if the
; collection holds them afterwards, "NOT in ..." otherwise (for fo4roulette.log)
String Function Join(Int aiActor, Int aiTarget, Int aiLink, Bool abDealer)
	Actor akActor = Game.GetForm(aiActor) as Actor
	ObjectReference akTarget = Game.GetForm(aiTarget) as ObjectReference
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	if akActor == None || akTarget == None || akLink == None
		return "NOT in: actor " + akActor + ", target " + akTarget + ", keyword " + akLink
	endif
	if !IsRunning()
		StartAndWait()
	endif
	RefCollectionAlias mine = Collection(abDealer)
	Collection(!abDealer).RemoveRef(akActor)
	Sitters.RemoveRef(akActor)
	mine.AddRef(akActor)
	if mine.Find(akActor) < 0 && Dealers.GetCount() == 0 && Bettors.GetCount() == 0
		; a save from before the collections existed: a fresh start of the quest, then again
		Stop()
		Int tries = 0
		while IsStopping() && tries < 50
			Utility.Wait(0.1)
			tries += 1
		endwhile
		StartAndWait()
		mine.AddRef(akActor)
	endif
	akActor.SetLinkedRef(None, akLink)
	akActor.EvaluatePackage()
	akActor.SetLinkedRef(akTarget, akLink)
	akActor.EvaluatePackage()
	if mine.Find(akActor) < 0
		return "NOT in (running " + IsRunning() + ", count " + mine.GetCount() + ")"
	endif
	return "in (" + mine.GetCount() + " there)"
EndFunction

; the walk sent again: linked ref off, packages looked at, linked ref on, looked at again (and their AI reset with
; abReset), so a Travel that's stuck starts over
Function Walk(Int aiActor, Int aiTarget, Int aiLink, Bool abReset)
	Actor akActor = Game.GetForm(aiActor) as Actor
	ObjectReference akTarget = Game.GetForm(aiTarget) as ObjectReference
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	if akActor == None || akTarget == None || akLink == None
		return
	endif
	akActor.SetLinkedRef(None, akLink)
	akActor.EvaluatePackage()
	akActor.SetLinkedRef(akTarget, akLink)
	akActor.EvaluatePackage(abReset)
EndFunction

; there: no destination, so the Travel package doesn't keep stepping and turning them (the plugin holds them)
Function Arrived(Int aiActor, Int aiLink)
	Actor akActor = Game.GetForm(aiActor) as Actor
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	if akActor && akLink
		akActor.SetLinkedRef(None, akLink)
		akActor.EvaluatePackage()
	endif
EndFunction

; out of both collections: their own AI again
Function Leave(Int aiActor, Int aiLink)
	Actor akActor = Game.GetForm(aiActor) as Actor
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	if akActor == None
		return
	endif
	Dealers.RemoveRef(akActor)
	Bettors.RemoveRef(akActor)
	Sitters.RemoveRef(akActor)
	akActor.EnableAI(true)  ; (0.5.2 turned it off at the tables: on again for anyone it was left off for)
	akActor.SetHeadTracking(true)
	if akLink
		akActor.SetLinkedRef(None, akLink)
	endif
	akActor.EvaluatePackage()
EndFunction

; everyone out (a save loaded: the plugin doesn't know who's in them)
Function ClearAll(Int aiLink)
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	ClearCollection(Dealers, akLink)
	ClearCollection(Bettors, akLink)
	ClearCollection(Sitters, akLink)
	; (a fresh start of the quest, empty: a save from before the Sitters collection gets it this way)
	if IsRunning()
		Stop()
		Int tries = 0
		while IsStopping() && tries < 50
			Utility.Wait(0.1)
			tries += 1
		endwhile
	endif
	StartAndWait()
EndFunction

Function ClearCollection(RefCollectionAlias akCollection, Keyword akLink)
	Int i = akCollection.GetCount()
	while i > 0
		i -= 1
		ObjectReference r = akCollection.GetAt(i)
		Actor a = r as Actor
		akCollection.RemoveRef(r)
		if a
			if akLink
				a.SetLinkedRef(None, akLink)
			endif
			a.SetRestrained(false)
			a.EnableAI(true)
			a.SetHeadTracking(true)
			a.EvaluatePackage()
		endif
	endwhile
EndFunction

; a vanilla idle (Fallout4.esm IDLE form ID) on someone at the table: their little reactions while they play
Bool Function PlayIdleOn(Int aiActor, Int aiIdle)
	Actor akActor = Game.GetForm(aiActor) as Actor
	Idle akIdle = Game.GetForm(aiIdle) as Idle
	if akActor == None || akIdle == None
		return false
	endif
	return akActor.PlayIdle(akIdle)
EndFunction

; a slot machine's sitter: their hand on the lever. The machine's pull animation (the DLC's) sends
; furnitureIdleFlavor1Slave as the lever goes down; each one goes to the plugin (fo4rouletteNative.LeverDown), which pulls the lever and
; spins the reels then, so they move with the hand
Bool Function WatchLever(Int aiActor, Bool abOn)
	Actor akActor = Game.GetForm(aiActor) as Actor
	if akActor == None
		return false
	endif
	if !abOn
		UnregisterForAnimationEvent(akActor, "furnitureIdleFlavor1Slave")
		return true
	endif
	return RegisterForAnimationEvent(akActor, "furnitureIdleFlavor1Slave")
EndFunction

Event OnAnimationEvent(ObjectReference akSource, String asEventName)
	if akSource != None && asEventName == "furnitureIdleFlavor1Slave"
		fo4rouletteNative.LeverDown(akSource.GetFormID())
	endif
EndEvent

; you at a slot machine: sat in it (its activation's blocked, so nobody passing sits in it: the default processing
; still sits you), and up out of it again (the game's own get-up)
Bool Function SitPlayerIn(Int aiFurniture)
	ObjectReference akFurniture = Game.GetForm(aiFurniture) as ObjectReference
	if akFurniture == None
		return false
	endif
	return akFurniture.Activate(Game.GetPlayer(), true)
EndFunction

Bool Function StandPlayerUp()
	Action akExit = Game.GetForm(0x0002248D) as Action  ; ActionInteractionExit
	if akExit == None
		return false
	endif
	return Game.GetPlayer().PlayIdleAction(akExit)
EndFunction

; ...and if the get-up didn't take: moved off it, to a spot that far from it (world units), facing the way you were
Bool Function MovePlayerOff(Int aiFurniture, Float afX, Float afY)
	ObjectReference akFurniture = Game.GetForm(aiFurniture) as ObjectReference
	if akFurniture == None
		return false
	endif
	Actor akPlayer = Game.GetPlayer()
	Float angle = akPlayer.GetAngleZ()
	akPlayer.MoveTo(akFurniture, afX, afY, 0.0, false)
	akPlayer.SetAngle(0.0, 0.0, angle)
	return true
EndFunction

; sat down in a chair (a poker table's)
Bool Function SitOn(Int aiActor, Int aiChair, Int aiLink, Int aiSeatLink)
	Actor akActor = Game.GetForm(aiActor) as Actor
	ObjectReference akChair = Game.GetForm(aiChair) as ObjectReference
	Keyword akLink = Game.GetForm(aiLink) as Keyword
	Keyword akSeatLink = Game.GetForm(aiSeatLink) as Keyword
	if akActor == None || akChair == None
		return false
	endif
	akChair.SetActorOwner(akActor.GetActorBase())  ; (theirs while they're in it)
	; off the walk (its Travel package got them straight up out of the chair to walk to its end), onto the Sit package
	; with the chair as its furniture: it keeps them in it
	if akSeatLink
		akActor.SetLinkedRef(akChair, akSeatLink)
	endif
	Dealers.RemoveRef(akActor)
	Bettors.RemoveRef(akActor)
	if Sitters.Find(akActor) < 0
		Sitters.AddRef(akActor)
	endif
	if akLink
		akActor.SetLinkedRef(None, akLink)
	endif
	akActor.EvaluatePackage()
	akActor.SnapIntoInteraction(akChair)
	return true
EndFunction

; a poker chair kept for the game: owned by you while nobody's playing in it, so settlers passing by don't sit in it
; (the players are put in it all the same)
Bool Function ClaimChair(Int aiChair)
	ObjectReference akChair = Game.GetForm(aiChair) as ObjectReference
	if akChair == None
		return false
	endif
	akChair.SetActorOwner(Game.GetPlayer().GetActorBase())
	akChair.BlockActivation(true)  ; (you don't sit in it: activating it sits you at the table instead, no Wait prompt)
	return true
EndFunction

; a line of the game's own (a custom-subtype topic, by its keyword: the Combat Zone crowd's cheers and groans, which
; every common voice has)
Bool Function SayOn(Int aiActor, Int aiKeyword)
	Actor akActor = Game.GetForm(aiActor) as Actor
	Keyword akKeyword = Game.GetForm(aiKeyword) as Keyword
	if akActor == None || akKeyword == None
		return false
	endif
	akActor.SayCustom(akKeyword)
	return true
EndFunction
