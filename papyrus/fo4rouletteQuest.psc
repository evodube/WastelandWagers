Scriptname fo4rouletteQuest extends Quest
{fo4roulette: NPCs at a roulette table. The Dealers and Bettors collections both carry fo4rouletteWalkToSpot
(walk to their linked ref, keyword fo4rouletteLinkSpot, and face it), which outranks the sandbox packages settlers
usually have. The F4SE plugin places a marker where each should stand and calls Join, Walk, Arrived and Leave.
Actors, refs and keywords come in as form IDs, not objects: an NPC with other scripts on them can come through from
the plugin as an object Papyrus won't take as an Actor.}

RefCollectionAlias Property Dealers Auto Const Mandatory
RefCollectionAlias Property Bettors Auto Const Mandatory

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
