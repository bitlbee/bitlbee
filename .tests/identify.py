import btlib

def identify(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.identify_test(clis)
    return ret

btlib.perform_test(identify)
