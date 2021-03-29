import btlib

FUN = [
"Did I ask you something?",
"Oh yeah, that's right.",
"Alright, alright. Now go back to work.",
"Buuuuuuuuuuuuuuuurp... Excuse me!",
"Yes?",
"No?",
]

def yes_test(clis):
    ret = False
    clis[0].send_priv_msg("&bitlbee", "yes")
    clis[0].receive()
    for x, fun in enumerate(FUN):
        if (clis[0].log.find(fun) != -1):
            ret = True
            if x:
                print("The RNG gods smile upon us")
            break
    return ret

btlib.perform_test(yes_test)
