import btlib

def remove_buddy(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.jabber_login_test(clis)
    ret = ret & btlib.remove_buddy_test(clis)
    return ret

btlib.perform_test(remove_buddy)
