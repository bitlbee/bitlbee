import btlib

def help(clis):
    ret = True
    ret = ret & btlib.connect_test(clis)
    ret = ret & btlib.help_test(clis)
    return ret

btlib.perform_test(help)
