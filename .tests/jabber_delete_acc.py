import btlib

def jabber_delete_acc(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.jabber_login_test(clis)
    ret = ret & btlib.jabber_delete_account_test(clis)
    return ret

btlib.perform_test(jabber_delete_acc)
