import btlib

def talk_to_bitlbee(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.yes_test(clis)
    return ret

btlib.perform_test(talk_to_bitlbee)
