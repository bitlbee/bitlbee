import btlib

def identify_nonexist(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.identify_nonexist_test(clis)
    return ret

btlib.perform_test(identify_nonexist)
