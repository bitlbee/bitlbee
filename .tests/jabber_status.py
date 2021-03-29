import btlib

def status(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.jabber_login_test(clis)
    ret = ret & btlib.add_buddy_test(clis)
    ret = ret & btlib.status_test(clis)
    return ret

btlib.perform_test(status)
