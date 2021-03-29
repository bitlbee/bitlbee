import btlib

def jabber_connect(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.jabber_login_test(clis)
    return ret

btlib.perform_test(jabber_connect)
