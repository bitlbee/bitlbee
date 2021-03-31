import btlib

def unregister(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.unregister_test(clis)
    return ret

btlib.perform_test(unregister)
