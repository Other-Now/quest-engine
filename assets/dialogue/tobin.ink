=== tobin ===
{ asked_crypt:
    Back again? #speaker:tobin
- else:
    Morning, neighbour. Fine day for it. #speaker:tobin
}
- (menu)
* {not asked_crypt} [What's in the old crypt?]
    What's in the old crypt? #speaker:me
    The Keeper's Amulet, so the story goes. #speaker:tobin
    Gate's been locked since the key fell down the well, years back. #speaker:tobin
    ~ asked_crypt = 1
    -> menu
* {saw_glint && key_in_well && not asked_key} [I saw something shiny down the well.]
    I saw something shiny down the well. #speaker:me
    Ha! That'll be the crypt key. #speaker:tobin
    You'd want a bucket on a good length of rope. There's rope in my shed. #speaker:tobin
    ~ asked_key = 1
    -> menu
+ {tobin_has_axe} [Could I borrow your axe?]
    Could I borrow your axe? #speaker:me
    { has_mushroom:
        -> trade
    }
    Bring me one of those red mushrooms from the orchard for my stew, #speaker:tobin
    and the axe is yours. #speaker:tobin
    -> menu
+ {gate_open} [The crypt is open!]
    The crypt is open! #speaker:me
    Well I never. Mind the dead, now. #speaker:tobin
    -> menu
+ [Bye, Tobin.]
    Bye, Tobin. #speaker:me
    Mind how you go. #speaker:tobin
    -> END

= trade
Is that a mushroom I see? #speaker:tobin
* [Give him the mushroom.]
    ~ has_mushroom = 0
    ~ has_axe = 1
    ~ tobin_has_axe = 0
    Lovely. Here, take the axe. Mind your fingers. #speaker:tobin
    -> menu
* [Not yet.]
    Suit yourself. #speaker:tobin
    -> menu
