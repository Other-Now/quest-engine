// Every Ink feature the game's dialogue uses, checked against inkcpp in
// tests/test_dialogue.cpp: knots, stitches, labelled gathers, once-only (*)
// and sticky (+) choices, conditional choices, conditional text, variable
// assignment, includes and tags.
VAR has_coin = 0
VAR door_open = 0
VAR not_in_store = 5

-> END

=== greet ===
{ door_open:
    The door is open. #speaker:guard
- else:
    Halt. #speaker:guard
}
- (menu)
* [Ask about the door.]
    It stays shut. #speaker:guard
    -> menu
* {has_coin} [Offer a coin.]
    -> bribe
+ [Leave.]
    Bye. #speaker:me
    -> END

= bribe
~ has_coin = 0
~ door_open = 1
~ not_in_store = 6
Fine, go on through. #speaker:guard
-> menu
