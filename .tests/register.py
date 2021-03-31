import btlib

def register(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.jabber_login_test(clis)
    ret = ret & btlib.register_test(clis)
    return ret

btlib.perform_test(register)
